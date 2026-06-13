/**
 * LED 状态指示任务（GPIO12，高电平有效）
 * 由 app_state 状态机通过 led_set_pattern 驱动，任务内部按节拍翻转电平。
 */
#include "led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "bikepower_config.h"

static const char *TAG = "LED";

#define LED_TICK_MS 20

static volatile led_pattern_t s_pattern = LED_SLOW;

/**
 * 设置 LED 当前目标灯效。
 * 更新后台任务读取的共享模式值。
 * pattern：新的灯效模式。
 * 返回值：无。
 * 特殊行为：函数本身不直接操作 GPIO，真正输出由 led_task 周期刷新。
 */
void led_set_pattern(led_pattern_t pattern)
{
    s_pattern = pattern;
}

/**
 * 向 LED GPIO 输出指定电平。
 * 把逻辑电平写到 LED 引脚。
 * level：目标输出电平，1 为亮，0 为灭。
 * 返回值：无。
 * 特殊行为：GPIO12 为高电平有效。
 */
static void led_write(int level)
{
    gpio_set_level(LED_PIN, level);
}

/**
 * LED 后台任务。
 * 依据当前模式维护闪烁节拍并持续刷新 GPIO 电平。
 * arg：未使用的任务参数。
 * 返回值：无，本任务不返回。
 * 特殊行为：任务已订阅 TWDT，需要持续周期喂狗。
 */
static void led_task(void *arg)
{
    (void)arg;
    int elapsed = 0;
    int on = 0;
    led_pattern_t last = LED_SLOW;

    esp_task_wdt_add(NULL);

    while (1) {
        led_pattern_t p = s_pattern;
        if (p != last) {
            last = p;
            elapsed = 0;
            // 切模式时重置节拍，避免从旧模式残留的半周期位置继续闪烁。
            on = (p == LED_SOLID) ? 1 : 0;
        }

        switch (p) {
        case LED_OFF:
            on = 0;
            break;
        case LED_SOLID:
            on = 1;
            break;
        case LED_SLOW:
            if (elapsed >= LED_BLINK_SLOW_MS) { on = !on; elapsed = 0; }
            break;
        case LED_FAST:
            if (elapsed >= LED_BLINK_FAST_MS) { on = !on; elapsed = 0; }
            break;
        }

        led_write(on);
        elapsed += LED_TICK_MS;
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

/**
 * 初始化 LED 模块。
 * 配置 GPIO12 为输出模式，默认灭灯，并启动 LED 后台任务。
 * 无参数。
 * 返回值：无。
 * 特殊行为：启动后灯效状态由 s_pattern 初始值和后续状态机调用共同决定。
 */
void led_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << LED_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(LED_PIN, 0);

    xTaskCreate(led_task, "led_task", 2048, NULL, 3, NULL);
    ESP_LOGI(TAG, "LED 任务启动 GPIO%d", LED_PIN);
}
