/**
 * LED 状态指示任务
 * 通用板：GPIO12 高电平有效普通 LED。
 * M5Stamp C3：GPIO2 SK6812 RGB LED（RMT 驱动），按状态映射颜色。
 * 由 app_state 状态机通过 led_set_pattern 驱动，任务内部按节拍刷新。
 */
#include "led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "bikepower_config.h"
#include "board_detect.h"

static const char *TAG = "LED";

#define LED_TICK_MS 20

static volatile led_pattern_t s_pattern = LED_SLOW;
static board_type_t s_board;

// ===== SK6812 RGB LED 驱动（M5Stamp C3） =====

static rmt_channel_handle_t s_rmt_chan;
static rmt_encoder_handle_t s_rmt_enc;

// 各状态对应的 RGB 颜色（G, R, B 顺序，SK6812 协议）
static const uint8_t s_color_off[]   = {0, 0, 0};        // 灭
static const uint8_t s_color_slow[]  = {0, 0, 0x30};     // 蓝色（BLE 广播中）
static const uint8_t s_color_solid[] = {0x30, 0, 0};     // 绿色（BLE 已连接）
static const uint8_t s_color_fast[]  = {0x30, 0x30, 0};  // 黄色（等待确认）

/**
 * 通过 RMT 发送 SK6812 一帧数据。
 * pixel：GRB 颜色数组 [G, R, B]，3 字节，bytes_encoder 自动按 bit 编码。
 */
static void sk6812_send(const uint8_t pixel[3])
{
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
        .flags = { .eot_level = 0 },
    };
    rmt_transmit(s_rmt_chan, s_rmt_enc, pixel, 3, &tx_cfg);
    rmt_tx_wait_all_done(s_rmt_chan, -1);
}

/**
 * 初始化 SK6812 RMT 发送通道和编码器。
 */
static void sk6812_init(void)
{
    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num = board_led_pin(),
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,  // 10 MHz → 100ns 分辨率
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&chan_cfg, &s_rmt_chan));

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = {
            .level0 = 1,
            .duration0 = SK6812_T0H_NS / 100,
            .level1 = 0,
            .duration1 = SK6812_T0L_NS / 100,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = SK6812_T1H_NS / 100,
            .level1 = 0,
            .duration1 = SK6812_T1L_NS / 100,
        },
        .flags = { .msb_first = 1 },
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&enc_cfg, &s_rmt_enc));

    rmt_enable(s_rmt_chan);

    // 初始灭灯
    sk6812_send(s_color_off);
    ESP_LOGI(TAG, "SK6812 RGB LED 初始化完成 GPIO%d", board_led_pin());
}

// ===== 通用 LED 驱动 =====

/**
 * 向 LED GPIO 输出指定电平（通用板）。
 */
static void led_write_gpio(int level)
{
    gpio_set_level(board_led_pin(), level);
}

// ===== 统一 LED 输出 =====

/**
 * 根据板型输出 LED 状态。
 * on=1 时亮灯（通用板高电平 / M5Stamp C3 对应颜色），on=0 时灭灯。
 * pattern：当前灯效模式，用于 M5Stamp C3 颜色映射。
 * on：亮灭标志。
 */
static void led_output(led_pattern_t pattern, int on)
{
    if (s_board == BOARD_M5STAMP_C3) {
        if (on) {
            switch (pattern) {
            case LED_OFF:   sk6812_send(s_color_off);   break;
            case LED_SLOW:  sk6812_send(s_color_slow);  break;
            case LED_SOLID: sk6812_send(s_color_solid); break;
            case LED_FAST:  sk6812_send(s_color_fast);  break;
            }
        } else {
            sk6812_send(s_color_off);
        }
    } else {
        led_write_gpio(on);
    }
}

// ===== 公开接口 =====

void led_set_pattern(led_pattern_t pattern)
{
    s_pattern = pattern;
}

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

        led_output(p, on);
        elapsed += LED_TICK_MS;
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

void led_init(void)
{
    s_board = board_get_type();

    if (s_board == BOARD_M5STAMP_C3) {
        sk6812_init();
    } else {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << board_led_pin(),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        gpio_set_level(board_led_pin(), 0);
        ESP_LOGI(TAG, "LED 任务启动 GPIO%d", board_led_pin());
    }

    xTaskCreate(led_task, "led_task", 2048, NULL, 3, NULL);
}
