/**
 * 应用事件定义 — 按钮任务/主循环向状态机投递事件
 * 运行环境：ESP-IDF v5.5.1，FreeRTOS Queue 通信
 */
#pragma once

#include <stdint.h>

typedef enum {
    APP_EVT_BTN_DOWN,       // 按钮按下（下降沿确认）
    APP_EVT_BTN_REPEAT,     // 长按自动重复（data=已按住毫秒）
    APP_EVT_BTN_UP,         // 按钮释放（data=按住毫秒）
    APP_EVT_CONN_CHANGED,   // BLE 连接状态变化（data=已连接 0/1）
    APP_EVT_ERG_TARGET,     // FTMS ERG 目标功率（data=目标瓦特）
} app_evt_type_t;

typedef struct {
    app_evt_type_t type;
    int32_t data;
} app_evt_t;
