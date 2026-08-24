/**
 * 中断驱动按钮
 * 通用板：GPIO9（BOOT 按钮），M5Stamp C3：GPIO3（用户按钮 SW）。
 * 均为内部上拉，按下为低电平。
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
#include "board_detect.h"

static const char *TAG = "BTN";

static QueueHandle_t s_app_queue;
static SemaphoreHandle_t s_wake;
static int s_btn_pin;

static void IRAM_ATTR btn_isr(void *arg)
{
    (void)arg;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_wake, &hp);
    if (hp) portYIELD_FROM_ISR();
}

static int btn_pressed(void)
{
    return gpio_get_level(s_btn_pin) == 0;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void post(app_evt_type_t type, int32_t data)
{
    app_evt_t e = { .type = type, .data = data };
    // 队列满时丢弃事件（保持按钮任务无阻塞），但记录告警留痕：
    // 丢失 BTN_DOWN 会导致 UP/REPEAT 与 DOWN 不配对，状态机回滚目标错乱，需可追查。
    if (xQueueSend(s_app_queue, &e, 0) != pdTRUE) {
        ESP_LOGW(TAG, "事件队列满，丢弃按钮事件 type=%d data=%ld", (int)type, (long)data);
    } else {
        // 诊断日志：确认 ISR→button_task→app_queue 链路通畅。
        // BTN_REPEAT 高频路径降级为 DEBUG，避免长按期间日志刷屏。
        // 看不到本日志说明 ISR 未触发或 button_task 未被唤醒（硬件接线/引脚配置问题）。
        if (type == APP_EVT_BTN_REPEAT) {
            ESP_LOGD(TAG, "事件 type=%d data=%ld", (int)type, (long)data);
        } else {
            ESP_LOGI(TAG, "事件 type=%d data=%ld", (int)type, (long)data);
        }
    }
}

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
            // 释放沿去抖：读到松开后再延时复读，机械抖动期间的瞬时高电平不会被误判为释放，
            // 否则长按过程中的抖动会被切成一次短按 UP，错触发调功或打断二次确认流程。
            if (!btn_pressed()) {
                vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));
                if (!btn_pressed()) break;
            }
        }

        uint32_t held = now_ms() - press_start;
        post(APP_EVT_BTN_UP, (int32_t)held);
        // 清掉释放沿产生的多余唤醒
        xSemaphoreTake(s_wake, 0);
    }
}

esp_err_t button_init(QueueHandle_t app_queue)
{
    s_app_queue = app_queue;
    s_btn_pin = board_btn_pin();

    s_wake = xSemaphoreCreateBinary();
    if (!s_wake) {
        ESP_LOGE(TAG, "xSemaphoreCreateBinary 失败");
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << s_btn_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t ret = gpio_config(&io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config 失败 ret=%d", ret);
        vSemaphoreDelete(s_wake);
        s_wake = NULL;
        return ret;
    }

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service 失败 ret=%d", ret);
        vSemaphoreDelete(s_wake);
        s_wake = NULL;
        return ret;
    }
    ret = gpio_isr_handler_add(s_btn_pin, btn_isr, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add 失败 ret=%d", ret);
        vSemaphoreDelete(s_wake);
        s_wake = NULL;
        return ret;
    }

    BaseType_t fr = xTaskCreate(button_task, "button_task", TASK_STACK_BTN, NULL, TASK_PRIO_BTN, NULL);
    if (fr != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate button_task 失败");
        gpio_isr_handler_remove(s_btn_pin);
        vSemaphoreDelete(s_wake);
        s_wake = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "按钮任务启动 GPIO%d（中断驱动）", s_btn_pin);
    return ESP_OK;
}
