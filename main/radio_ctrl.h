/**
 * 射频控制器：BLE/WiFi 互斥的唯一开关（硬约束 WB-MUTEX-1/2）
 * 运行环境：ESP-IDF v5.5.4，目标芯片 ESP32-C3
 *
 * ESP32-C3 单路射频，WiFi 与 BLE 不能同时占用。进入配网必须先停 BLE。
 * v3.0.0 采用方案 A：配网保存/超时后 esp_restart() 重启恢复 BLE，最可靠。
 * 任何模块禁止直接调 esp_wifi_start()/ble_gap_adv_start()，必须经本模块。
 */
#pragma once

#include "power_engine.h"

/**
 * 进入 WiFi 配网模式。
 * 按 WiFi/BLE 互斥约束执行停 BLE、等待射频释放，再启动 SoftAP 和 HTTP 配网门户。
 * engine：配网门户读写配置时使用的功率引擎实例。
 * 返回值：无。
 * 特殊行为：这是唯一允许切换 BLE/WiFi 射频占用的入口，调用后设备会脱离 BLE 模式。
 */
void radio_ctrl_enter_wifi(power_engine_t *engine);
