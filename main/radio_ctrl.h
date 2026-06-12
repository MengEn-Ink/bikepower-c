/**
 * 射频控制器：BLE/WiFi 互斥的唯一开关（硬约束 WB-MUTEX-1/2）
 * 运行环境：ESP-IDF v5.5.1，目标芯片 ESP32-C3
 *
 * ESP32-C3 单路射频，WiFi 与 BLE 不能同时占用。进入配网必须先停 BLE。
 * v3.0.0 采用方案 A：配网保存/超时后 esp_restart() 重启恢复 BLE，最可靠。
 * 任何模块禁止直接调 esp_wifi_start()/ble_gap_adv_start()，必须经本模块。
 */
#pragma once

#include "power_engine.h"

/**
 * 进入 WiFi 配网模式：停 BLE -> 释放射频 -> 启动 AP + HTTP 配网服务。
 * 由 app_state 的 wifi_trigger 回调触发（用户二次确认后）。
 * 注册的 power_engine 用于 POST /config 写入配置并持久化。
 */
void radio_ctrl_enter_wifi(power_engine_t *engine);
