/**
 * 应用状态机（对齐 doc 11 §8.1 状态枚举）
 * 运行环境：ESP-IDF v5.5.1
 */
#pragma once

#include "app_events.h"
#include "power_engine.h"

typedef enum {
    ST_BOOTING,
    ST_BLE_ACTIVE,      // BLE 广播/连接中（LED 由连接状态决定）
    ST_WIFI_CONFIRM,    // 长按后等待二次确认（LED 快闪）
    ST_WIFI_CONFIG,     // WiFi 配网模式（阶段5接入）
    ST_SAVING,
    ST_REBOOTING,
    ST_ERROR_RECOVERY,
} app_state_t;

/** 初始化状态机，绑定领域引擎 */
void app_state_init(power_engine_t *engine);

/** 处理一个事件（在主任务上下文调用） */
void app_state_handle_event(const app_evt_t *e);

/** 周期 tick（主循环每拍调用，处理确认窗口超时） */
void app_state_tick(void);

/** 注册 WiFi 配网触发回调（阶段5接入；未注册时二次确认仅打印日志） */
void app_state_set_wifi_trigger(void (*cb)(void));

/** 当前状态 */
app_state_t app_state_get(void);
