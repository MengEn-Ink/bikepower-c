/**
 * WiFi 配网门户：HTTP 配置面板（esp_http_server）
 * 运行环境：ESP-IDF v5.5.4，目标芯片 ESP32-C3
 *
 * SoftAP 启动由 radio_ctrl 统一管理；本模块只负责 HTTP 路由和页面。
 * 调用前必须已由 radio_ctrl 启动 SoftAP（v3.1.3 起为共存模式，BLE 仍在线）。
 * 路由：GET /（配置页）、POST /config（保存并实时生效）、GET /time（剩余秒数）。
 * 180s 无续期自动关闭 WiFi 回到 BLE（不重启）；配置通过 power_engine setter 实时生效到 BLE 通知。
 */
#pragma once

#include "power_engine.h"
#include "esp_err.h"

esp_err_t wifi_portal_start(power_engine_t *engine);

void wifi_portal_stop(void);

void wifi_portal_set_done_callback(bool (*cb)(void));

bool wifi_portal_consume_ble_restart_requested(void);

bool wifi_portal_ble_restart_requested(void);
