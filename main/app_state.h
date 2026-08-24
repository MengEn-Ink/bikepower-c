/**
 * 应用状态机（对齐 doc 11 §8.1 状态枚举）
 * 运行环境：ESP-IDF v5.5.4
 */
#pragma once

#include "app_events.h"
#include "power_engine.h"

/**
 * 应用状态机状态枚举。
 * 表示设备从启动、BLE 工作、WiFi 确认到异常恢复的高层运行阶段。
 * 无参数。
 * 无返回值。
 */
typedef enum {
    ST_BOOTING,
    ST_BLE_ACTIVE,      // BLE 广播/连接中（LED 由连接状态决定）
    ST_WIFI_CONFIRM,    // 长按后等待二次确认（LED 快闪）
    ST_WIFI_CONFIG,     // WiFi 配网模式
    ST_SAVING,
    ST_REBOOTING,
    ST_ERROR_RECOVERY,
} app_state_t;

void app_state_init(power_engine_t *engine);

void app_state_handle_event(const app_evt_t *e);

void app_state_tick(void);

void app_state_refresh_led(void);

void app_state_set_wifi_trigger(void (*cb)(void));

void app_state_set_wifi_exit(void (*cb)(void));

app_state_t app_state_get(void);

void app_state_set_error(void);

void app_state_clear_error(void);

#ifdef PORTAL_TEST_AUTO_CONFIG
void app_state_enter_wifi_config_for_test(void);
#endif
