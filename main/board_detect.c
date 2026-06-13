/**
 * 板型运行时自动检测
 *
 * 检测策略：配置 GPIO12 为输入下拉。
 * - 通用 ESP32-C3 开发板：GPIO12 接 LED 电路，有上拉电阻，读为高电平。
 * - M5Stamp C3：GPIO12 悬空，下拉后读为低电平。
 *
 * 检测完成后恢复 GPIO12 为默认状态，避免影响后续 LED 初始化。
 */
#include "board_detect.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "bikepower_config.h"

static const char *TAG = "BOARD";

static board_type_t s_board = BOARD_GENERIC_C3;

board_type_t board_detect(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << LED_PIN_GENERIC,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    vTaskDelay(pdMS_TO_TICKS(1));

    int level = gpio_get_level(LED_PIN_GENERIC);

    gpio_reset_pin(LED_PIN_GENERIC);

    if (level == 1) {
        s_board = BOARD_GENERIC_C3;
        ESP_LOGI(TAG, "检测到通用 ESP32-C3 开发板（GPIO12=%d）", level);
    } else {
        s_board = BOARD_M5STAMP_C3;
        ESP_LOGI(TAG, "检测到 M5Stamp C3（GPIO12=%d）", level);
    }

    return s_board;
}

board_type_t board_get_type(void)
{
    return s_board;
}

int board_btn_pin(void)
{
    return (s_board == BOARD_M5STAMP_C3) ? BTN_PIN_M5STAMP : BTN_PIN_GENERIC;
}

int board_led_pin(void)
{
    return (s_board == BOARD_M5STAMP_C3) ? LED_PIN_M5STAMP : LED_PIN_GENERIC;
}
