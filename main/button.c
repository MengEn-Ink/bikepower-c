/**
 * 中断驱动按钮（GPIO9，内部上拉，按下为低电平）
 *
 * ISR 仅释放信号量唤醒 button_task（中断上下文禁止调用 BLE/日志/队列阻塞 API）。
 * button_task 负责去抖、按住计时、长按自动重复，并向 app_queue 投递语义事件：
 *   BTN_DOWN            按下确认
 *   BTN_REPEAT(dur_ms)  按住超过短按阈值后，每 BTN_AUTO_REPEAT_MS 一次
 *   BTN_UP(dur_ms)      释放，附带本次按住总时长
 */
#include "button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "bikepower_config.h"
#include "app_events.h"

static const char *TAG = "BTN";

#define BTN_POLL_MS     20
#define BTN_DEBOUNCE_MS 30

static QueueHandle_t s_app_queue;
static SemaphoreHandle_t s_wake;

/**
 * 按钮 GPIO 中断服务函数。
 * 仅通过信号量唤醒 button_task，不在中断上下文做去抖、日志或队列发送。
 * arg：未使用的中断参数。
 * 返回值：无。
 * 特殊行为：必须保持 IRAM 安全和极短执行路径。
 */
static void IRAM_ATTR btn_isr(void *arg)
{
    (void)arg;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_wake, &hp);
    if (hp) portYIELD_FROM_ISR();
}

/**
 * 读取按钮是否处于按下状态。
 * 根据 GPIO 电平判断 GPIO9 当前是否被按下。
 * 无参数。
 * 返回值：非 0 表示已按下，0 表示未按下。
 * 特殊行为：按钮使用内部上拉，低电平表示按下。
 */
static int btn_pressed(void)
{
    return gpio_get_level(BTN_PIN) == 0;
}

/**
 * 获取当前单调时间的毫秒值。
 * 作为按钮去抖和按压时长统计的统一时间基准。
 * 无参数。
 * 返回值：当前毫秒时间戳。
 * 特殊行为：基于 esp_timer，不依赖 RTC 或真实时间。
 */
static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/**
 * 向应用队列投递一个按钮事件。
 * 把事件类型和附带数据封装为 app_evt_t 后以非阻塞方式发送到主循环。
 * type：事件类型。
 * data：附加数据，通常为按住时长。
 * 返回值：无。
 * 特殊行为：发送失败时不会重试，保持按钮任务无阻塞。
 */
static void post(app_evt_type_t type, int32_t data)
{
    app_evt_t e = { .type = type, .data = data };
    xQueueSend(s_app_queue, &e, 0);
}

/**
 * 按钮后台任务。
 * 负责等待中断唤醒、完成去抖、统计按住时长并产生 DOWN/REPEAT/UP 事件。
 * arg：未使用的任务参数。
 * 返回值：无，本任务不返回。
 * 特殊行为：空闲时阻塞等待信号量，避免轮询浪费 CPU。
 */
static void button_task(void *arg)
{
    (void)arg;
    while (1) {
        // 空闲：等待中断唤醒
        xSemaphoreTake(s_wake, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));
        if (!btn_pressed()) continue;       // 抖动/误触，回到空闲

        uint32_t press_start = now_ms();
        post(APP_EVT_BTN_DOWN, 0);
        uint32_t last_repeat = press_start;

        // 按住：轮询直到释放
        while (btn_pressed()) {
            uint32_t held = now_ms() - press_start;
            if (held >= SHORT_PRESS_MS &&
                (now_ms() - last_repeat) >= BTN_AUTO_REPEAT_MS) {
                // 超过短按阈值后开始周期投递 REPEAT，供状态机执行长按自动调功。
                last_repeat = now_ms();
                post(APP_EVT_BTN_REPEAT, (int32_t)held);
            }
            vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
        }

        uint32_t held = now_ms() - press_start;
        post(APP_EVT_BTN_UP, (int32_t)held);
        // 清掉释放沿产生的多余唤醒
        xSemaphoreTake(s_wake, 0);
    }
}

/**
 * 初始化按钮模块。
 * 配置 GPIO9 输入中断、安装 ISR 服务并创建按钮后台任务。
 * app_queue：按钮事件要投递到的应用队列。
 * 返回值：无。
 * 特殊行为：模块使用二值信号量作为 ISR 到任务的唤醒桥梁。
 */
void button_init(QueueHandle_t app_queue)
{
    s_app_queue = app_queue;
    s_wake = xSemaphoreCreateBinary();

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BTN_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BTN_PIN, btn_isr, NULL);

    xTaskCreate(button_task, "button_task", 2560, NULL, 5, NULL);
    ESP_LOGI(TAG, "按钮任务启动 GPIO%d（中断驱动）", BTN_PIN);
}
