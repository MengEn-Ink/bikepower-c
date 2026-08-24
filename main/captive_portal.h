/**
 * Captive Portal：DNS 劫持 + OS 探测端点重定向
 *
 * 职责边界：
 *   - 本模块只负责 DNS 服务器（UDP 53 劫持所有 A 查询返回 SoftAP IP）和
 *     OS captive 探测端点的 HTTP handler（303 重定向到配网首页）。
 *   - SoftAP 启停由 radio_ctrl 统一管理；HTTP 路由注册由 wifi_portal 负责。
 *   - 本模块不触碰 BLE、不读写业务配置。
 *
 * 集成点：
 *   - radio_ctrl_start_wifi_ap 成功后调 captive_portal_start_dns 启动 DNS 劫持
 *   - radio_ctrl_stop_wifi_ap 中调 captive_portal_stop_dns 停止 DNS
 *   - wifi_portal 的 start_httpd 注册各 OS 探测路径，handler 统一用 captive_portal_detect_handler
 *
 * 兼容性：覆盖 iOS/macOS、Android/Chrome、Windows、Firefox、Kindle 等主流系统的探测端点。
 */
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t captive_portal_start_dns(void);

void captive_portal_stop_dns(void);

esp_err_t captive_portal_detect_handler(httpd_req_t *req);
