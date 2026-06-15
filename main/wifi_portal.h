/**
 * WiFi 配网门户：esp_wifi SoftAP + esp_http_server 配置面板
 * 运行环境：ESP-IDF v5.5.4，目标芯片 ESP32-C3
 *
 * 调用前必须已停用 BLE（由 radio_ctrl 保证）。
 * 路由：GET /（配置页）、POST /config（保存并重启）、GET /time（剩余秒数）。
 * 180s 无续期自动超时 -> esp_restart() 恢复 BLE。
 */
#pragma once

#include "power_engine.h"

/**
 * 启动 WiFi 配网门户。
 * 初始化 SoftAP、HTTP 服务和超时定时器，提供浏览器配置页面。
 * engine：当前功率引擎实例，供网页读取与写入配置。
 * 返回值：无。
 * 特殊行为：调用前必须已由 radio_ctrl 停止 BLE，保存成功或超时后会由门户触发重启恢复 BLE。
 */
void wifi_portal_start(power_engine_t *engine);
