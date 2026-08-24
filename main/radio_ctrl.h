/**
 * 射频控制器：WiFi/BLE 共存配网（方案 C：不停 BLE 不重启）
 * 运行环境：ESP-IDF v5.5.4，目标芯片 ESP32-C3
 *
 * ESP32-C3 coex 模块按时间片仲裁 WiFi/BLE 射频（sdkconfig 已启用 SW_COEXIST）。
 * v3.1.3 起采用方案 C：配网期间 BLE 保持在线继续通知，配置变更实时生效，无需重启设备。
 * 极端失败兜底：仅在 WiFi 资源彻底无法初始化时降级为停 BLE + esp_restart。
 * 任何模块禁止直接调 esp_wifi_start()/ble_gap_adv_start()，必须经本模块。
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "power_engine.h"

void radio_ctrl_enter_wifi(power_engine_t *engine);

void radio_ctrl_exit_wifi(void);

bool radio_ctrl_ble_suspended(void);
