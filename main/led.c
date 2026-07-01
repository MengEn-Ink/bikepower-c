/**
 * LED 状态指示任务
 * 通用板：GPIO12 高电平有效普通 LED。
 * M5Stamp C3：GPIO2 SK6812 RGB LED（RMT 驱动），按状态映射颜色。
 * 由 app_state 状态机通过 led_apply_scene 驱动，任务内部按节拍刷新。
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
#include "led_pattern.h"

static const char *TAG = "LED";

static volatile uint8_t s_mode = LED_MODE_SLOW;
static volatile uint8_t s_color = LED_COLOR_BLUE;
static led_drv_t s_led_drv;

// ===== SK6812 RGB LED 驱动（M5Stamp C3） =====

static rmt_channel_handle_t s_rmt_chan;
static rmt_encoder_handle_t s_rmt_enc;

// SK6812 灭灯帧（颜色映射已抽到领域层 led_pattern.c）
static const uint8_t s_color_off[]   = {0, 0, 0};        // 灭

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
 * 释放 SK6812 RMT 通道和编码器资源。
 */
static void sk6812_deinit(void)
{
    if (s_rmt_enc) {
        rmt_del_encoder(s_rmt_enc);
        s_rmt_enc = NULL;
    }
    if (s_rmt_chan) {
        rmt_disable(s_rmt_chan);
        rmt_del_channel(s_rmt_chan);
        s_rmt_chan = NULL;
    }
}

/**
 * 初始化 SK6812 RMT 发送通道和编码器。
 */
static esp_err_t sk6812_init(void)
{
    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num = board_led_pin(),
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_CLK_HZ,  // 10 MHz → 100ns 分辨率
        .mem_block_symbols = RMT_MEM_BLOCK_SYMBOLS,
        .trans_queue_depth = RMT_TX_QUEUE_DEPTH,
    };
    esp_err_t ret = rmt_new_tx_channel(&chan_cfg, &s_rmt_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel 失败 ret=%d", ret);
        return ret;
    }

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
    ret = rmt_new_bytes_encoder(&enc_cfg, &s_rmt_enc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder 失败 ret=%d", ret);
        rmt_del_channel(s_rmt_chan);
        s_rmt_chan = NULL;
        return ret;
    }

    ret = rmt_enable(s_rmt_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable 失败 ret=%d", ret);
        rmt_del_encoder(s_rmt_enc);
        s_rmt_enc = NULL;
        rmt_del_channel(s_rmt_chan);
        s_rmt_chan = NULL;
        return ret;
    }

    // 初始灭灯
    sk6812_send(s_color_off);
    ESP_LOGI(TAG, "SK6812 RGB LED 初始化完成 GPIO%d", board_led_pin());
    return ESP_OK;
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
 * 根据板型输出 LED 强度。
 * 通用板只使用 scale>0 的亮灭；RGB 板按 color 和 scale 缩放全局亮度。
 */
static void led_output(uint8_t color, uint8_t scale)
{
    if (s_led_drv == LED_DRV_SK6812) {
        if (scale > 0) {
            uint8_t rgb[3];
            uint8_t brightness = (uint8_t)((uint32_t)SK6812_BRIGHTNESS * scale / 255u);
            led_color_to_rgb((led_color_t)color, brightness, rgb);
            sk6812_send(rgb);
        } else {
            sk6812_send(s_color_off);
        }
    } else {
        led_write_gpio(scale > 0 ? 1 : 0);
    }
}

// ===== 公开接口 =====

void led_apply_scene(led_scene_t scene, const led_config_t *config)
{
    led_effect_t effect = led_resolve(scene, config, s_led_drv == LED_DRV_SK6812);
    s_mode = effect.mode;
    s_color = effect.color;
}

static void led_task(void *arg)
{
    (void)arg;
    uint32_t elapsed = 0;
    uint8_t last_mode = LED_MODE_SLOW;
    uint8_t last_color = LED_COLOR_BLUE;

    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err != ESP_OK) {
        ESP_LOGW(TAG, "TWDT 订阅失败(%s)，LED 任务看门狗保护未生效", esp_err_to_name(wdt_err));
    }

    while (1) {
        uint8_t mode = s_mode;
        uint8_t color = s_color;
        if (mode != last_mode || color != last_color) {
            last_mode = mode;
            last_color = color;
            elapsed = 0;
        }

        uint32_t period = led_mode_period_ms((led_mode_t)mode, LED_BLINK_SLOW_MS, LED_BLINK_FAST_MS);
        uint32_t phase = period > 0 ? elapsed % period : 0;
        uint8_t scale = led_mode_scale((led_mode_t)mode, phase, LED_BLINK_SLOW_MS, LED_BLINK_FAST_MS);
        led_output(color, scale);
        elapsed += LED_TICK_MS;
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(LED_TICK_MS));
    }
}

esp_err_t led_init(void)
{
    s_led_drv = board_led_drv();

    if (s_led_drv == LED_DRV_SK6812) {
        esp_err_t ret = sk6812_init();
        if (ret != ESP_OK) {
            return ret;
        }
    } else {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << board_led_pin(),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t ret = gpio_config(&io);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "gpio_config LED 失败 ret=%d", ret);
            return ret;
        }
        gpio_set_level(board_led_pin(), 0);
        ESP_LOGI(TAG, "LED GPIO 初始化完成 GPIO%d", board_led_pin());
    }

    BaseType_t fr = xTaskCreate(led_task, "led_task", TASK_STACK_LED, NULL, TASK_PRIO_LED, NULL);
    if (fr != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate led_task 失败");
        // 创建任务失败时释放已初始化的硬件资源
        if (s_led_drv == LED_DRV_SK6812) {
            sk6812_deinit();
        }
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
