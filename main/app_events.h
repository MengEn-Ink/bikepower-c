/**
 * 应用事件定义 — 按钮任务/主循环向状态机投递事件
 * 运行环境：ESP-IDF v5.5.4，FreeRTOS Queue 通信
 */
#pragma once

#include <stdint.h>

/**
 * 应用层事件类型。
 * 表示按钮任务和主循环可能投递给状态机的输入事件。
 * 无参数，具体附加数据由 app_evt_t.data 承载。
 * 无返回值。
 */
typedef enum {
    APP_EVT_BTN_DOWN,       // 按钮按下（下降沿确认）
    APP_EVT_BTN_REPEAT,     // 长按自动重复（data=已按住毫秒）
    APP_EVT_BTN_UP,         // 按钮释放（data=按住毫秒）
    APP_EVT_CONN_CHANGED,   // BLE 连接状态变化（data=已连接 0/1）
    APP_EVT_ERG_TARGET,     // FTMS ERG 目标功率（data=目标瓦特）
} app_evt_type_t;

/**
 * 应用层事件载体。
 * 封装事件类型和一个整型附加数据，供状态机统一消费。
 * type：事件类型。
 * data：事件附加数据，语义由 type 决定。
 * 特殊行为：不同事件对 data 的解释不同，例如按住时长或连接状态。
 */
typedef struct {
    app_evt_type_t type;
    int32_t data;
} app_evt_t;
