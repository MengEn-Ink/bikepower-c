/**
 * 板型描述与运行时检测
 *
 * 各板型硬件差异集中在 s_board_desc[] 描述表（引脚 / LED 驱动 / 固件包名）。
 * 新增板型：bikepower_config.h 追加枚举值 → 此表补一行，调用方无需改动。
 *
 * 检测策略（仅在 Kconfig 未指定板型时启用）：配置 GPIO3 为输入下拉。
 * - M5Stamp C3：GPIO3 接外部按钮（上拉电阻到 3.3V），内部下拉后分压仍为高电平。
 * - 通用 ESP32-C3 开发板：GPIO3 悬空，下拉后读为低电平。
 * 检测完成后恢复 GPIO3，避免影响后续按钮初始化。
 */
#include "board_detect.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "bikepower_config.h"

static const char *TAG = "BOARD";

// 板型描述表，下标与 board_type_t 枚举一一对应
static const board_desc_t s_board_desc[BOARD_TYPE_COUNT] = {
    [BOARD_GENERIC_C3] = { BTN_PIN_GENERIC, LED_PIN_GENERIC, LED_DRV_GPIO,   BOARD_NAME_GENERIC },
    [BOARD_M5STAMP_C3] = { BTN_PIN_M5STAMP, LED_PIN_M5STAMP, LED_DRV_SK6812, BOARD_NAME_M5STAMP },
};

static board_type_t s_board = BOARD_GENERIC_C3;

board_type_t board_detect(void)
{
#if defined(CONFIG_BOARD_GENERIC_C3)
    s_board = BOARD_GENERIC_C3;
    ESP_LOGI(TAG, "板型：%s（Kconfig 指定）", s_board_desc[s_board].name);
#elif defined(CONFIG_BOARD_M5STAMP_C3)
    s_board = BOARD_M5STAMP_C3;
    ESP_LOGI(TAG, "板型：%s（Kconfig 指定）", s_board_desc[s_board].name);
#else
    // 用 GPIO3 检测：M5Stamp 按钮上拉 → 高电平；通用板悬空 → 下拉后低电平
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BTN_PIN_M5STAMP,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    vTaskDelay(pdMS_TO_TICKS(1));

    int level = gpio_get_level(BTN_PIN_M5STAMP);

    gpio_reset_pin(BTN_PIN_M5STAMP);

    s_board = (level == 1) ? BOARD_M5STAMP_C3 : BOARD_GENERIC_C3;
    ESP_LOGI(TAG, "自动检测板型：%s（GPIO3=%d）", s_board_desc[s_board].name, level);
#endif

    return s_board;
}

board_type_t board_get_type(void)
{
    return s_board;
}

const board_desc_t *board_get_desc(void)
{
    return &s_board_desc[s_board];
}

int board_btn_pin(void)
{
    return s_board_desc[s_board].btn_pin;
}

int board_led_pin(void)
{
    return s_board_desc[s_board].led_pin;
}

led_drv_t board_led_drv(void)
{
    return s_board_desc[s_board].led_drv;
}
