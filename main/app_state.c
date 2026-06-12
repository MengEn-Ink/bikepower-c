/**
 * 应用状态机（对齐 doc 11 §8.1）
 *
 * 消费按钮/连接事件，驱动调功、二次确认窗口与 LED。
 * 按钮语义（与 MicroPython specs/button-handler.md 一致）：
 *   短按 <500ms        -> adjust_power(-20)
 *   长按 >=500ms       -> 每 BTN_AUTO_REPEAT_MS adjust_power(+20)（仅 steady 生效）
 *   长按 >=2000ms 释放 -> 回滚本次自动调功，进入确认窗口（LED 快闪）
 *   确认窗口内再按      -> 触发 WiFi 配网（阶段5接入，经 wifi_trigger 回调）
 *   确认窗口 3s 超时    -> 取消，恢复 BLE_ACTIVE
 */
#include "app_state.h"
#include "led.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bikepower_config.h"

static const char *TAG = "STATE";

static power_engine_t *s_engine;
static app_state_t s_state = ST_BOOTING;
static bool s_connected = false;

static int s_power_before_press = 0;   // 本次按下前基准功率（用于长按回滚）
static bool s_auto_adjusted = false;   // 本次按住是否发生自动调功
static uint32_t s_confirm_start = 0;   // 确认窗口开始时间

static void (*s_wifi_trigger)(void) = NULL;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void update_idle_led(void)
{
    led_set_pattern(s_connected ? LED_SOLID : LED_SLOW);
}

void app_state_set_wifi_trigger(void (*cb)(void))
{
    s_wifi_trigger = cb;
}

void app_state_init(power_engine_t *engine)
{
    s_engine = engine;
    s_state = ST_BLE_ACTIVE;
    s_connected = false;
    update_idle_led();
    ESP_LOGI(TAG, "状态机启动 -> ST_BLE_ACTIVE");
}

app_state_t app_state_get(void)
{
    return s_state;
}

static void enter_confirm_window(void)
{
    if (s_auto_adjusted) {
        power_engine_set_power(s_engine, s_power_before_press);
        ESP_LOGI(TAG, "长按结束，回滚功率到 %dW", s_power_before_press);
    }
    s_state = ST_WIFI_CONFIRM;
    s_confirm_start = now_ms();
    led_set_pattern(LED_FAST);
    ESP_LOGI(TAG, "长按2秒，LED快闪等待二次确认...");
}

static void trigger_wifi(void)
{
    ESP_LOGI(TAG, "二次确认，进入WiFi配网模式");
    if (s_wifi_trigger) {
        s_state = ST_WIFI_CONFIG;
        led_set_pattern(LED_OFF);
        s_wifi_trigger();
    } else {
        ESP_LOGW(TAG, "WiFi 配网未就绪（阶段5接入），取消");
        s_state = ST_BLE_ACTIVE;
        update_idle_led();
    }
}

void app_state_handle_event(const app_evt_t *e)
{
    switch (e->type) {
    case APP_EVT_CONN_CHANGED:
        s_connected = (e->data != 0);
        if (s_state == ST_BLE_ACTIVE) update_idle_led();
        ESP_LOGI(TAG, "连接状态变化 connected=%d", s_connected);
        break;

    case APP_EVT_ERG_TARGET:
        power_engine_set_erg_target(s_engine, e->data);
        ESP_LOGI(TAG, "ERG 目标功率 %dW", (int)e->data);
        break;

    case APP_EVT_BTN_DOWN:
        if (s_state == ST_WIFI_CONFIRM) {
            trigger_wifi();
            break;
        }
        s_power_before_press = s_engine->base_power;
        s_auto_adjusted = false;
        break;

    case APP_EVT_BTN_REPEAT:
        if (s_state != ST_BLE_ACTIVE) break;
        if (e->data < WIFI_BTN_HOLD_MS) {
            power_engine_adjust_power(s_engine, POWER_STEP_W);
            s_auto_adjusted = true;
        }
        break;

    case APP_EVT_BTN_UP:
        if (s_state == ST_WIFI_CONFIG) break;        // 二次确认后的释放，忽略
        if (s_state == ST_WIFI_CONFIRM) break;       // 已在 DOWN 处理
        if (s_state != ST_BLE_ACTIVE) break;
        if (e->data < SHORT_PRESS_MS) {
            int p = power_engine_adjust_power(s_engine, -POWER_STEP_W);
            ESP_LOGI(TAG, "短按 功率-%dW -> %dW", POWER_STEP_W, p);
        } else if (e->data >= WIFI_BTN_HOLD_MS) {
            enter_confirm_window();
        }
        // 500ms~2000ms：长按调功已在 REPEAT 应用，释放不额外动作
        break;
    }
}

void app_state_tick(void)
{
    if (s_state == ST_WIFI_CONFIRM &&
        (now_ms() - s_confirm_start) >= CONFIRM_WINDOW_MS) {
        s_state = ST_BLE_ACTIVE;
        update_idle_led();
        ESP_LOGI(TAG, "确认窗口超时，取消进入配网模式");
    }
}
