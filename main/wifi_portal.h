/**
 * WiFi 配网门户：esp_wifi SoftAP + esp_http_server 配置面板
 * 运行环境：ESP-IDF v5.5.1，目标芯片 ESP32-C3
 *
 * 调用前必须已停用 BLE（由 radio_ctrl 保证）。
 * 路由：GET /（配置页）、POST /config（保存并重启）、GET /time（剩余秒数）。
 * 180s 无续期自动超时 -> esp_restart() 恢复 BLE。
 */
#pragma once

#include "power_engine.h"

/** 启动 SoftAP + HTTP server + 超时定时器。engine 用于读取当前值与写入配置。 */
void wifi_portal_start(power_engine_t *engine);
