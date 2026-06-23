/**
 * WiFi 配网门户：HTTP 配置面板（esp_http_server）
 * 运行环境：ESP-IDF v5.5.4，目标芯片 ESP32-C3
 *
 * SoftAP 启动由 radio_ctrl 统一管理；本模块只负责 HTTP 路由和页面。
 * 调用前必须已停用 BLE 并已启动 SoftAP（由 radio_ctrl 保证）。
 * 路由：GET /（配置页）、POST /config（保存并重启）、GET /time（剩余秒数）。
 * 180s 无续期自动超时 -> esp_restart() 恢复 BLE。
 */
#pragma once

#include "power_engine.h"
#include "esp_err.h"

/**
 * 启动 WiFi 配网门户（HTTP 部分）。
 * 启动 HTTP 服务和超时定时器，提供浏览器配置页面。
 * engine：当前功率引擎实例，供网页读取与写入配置。
 * 返回值：ESP_OK 表示门户已就绪，否则为错误码。
 * 特殊行为：调用前必须已由 radio_ctrl 停止 BLE 并启动 SoftAP；HTTP 启动失败时返回错误码，由调用方决定是否重启恢复 BLE。
 */
esp_err_t wifi_portal_start(power_engine_t *engine);
