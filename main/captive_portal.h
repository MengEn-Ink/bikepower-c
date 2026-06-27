/**
 * Captive Portal：DNS 劫持 + OS 探测端点重定向
 *
 * 职责边界：
 *   - 本模块只负责 DNS 服务器（UDP 53 劫持所有 A 查询返回 SoftAP IP）和
 *     OS captive 探测端点的 HTTP handler（302 重定向到配网首页）。
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

/**
 * 启动 DNS 劫持服务器。
 * 在 UDP 53 端口监听，把所有 A 记录查询应答为 SoftAP IP（192.168.4.1），
 * 使手机连上 SoftAP 后任意域名都指向配网入口，配合 OS 探测触发自动弹窗。
 * 无参数。
 * 返回值：ESP_OK 表示 DNS 服务器已启动；失败仅记录日志，不阻断配网（降级为手动输 IP）。
 * 特殊行为：内部创建独立任务处理 DNS 查询，可重复调用（幂等，已运行时直接返回）。
 */
esp_err_t captive_portal_start_dns(void);

/**
 * 停止 DNS 劫持服务器。
 * 关闭 socket 并删除 DNS 任务，释放资源。
 * 无参数。
 * 返回值：无。
 * 特殊行为：可重复调用（幂等）；与 captive_portal_start_dns 配对使用。
 */
void captive_portal_stop_dns(void);

/**
 * Captive Portal 探测端点统一 HTTP handler。
 * 对各 OS 的连通性探测请求统一返回 302 重定向到配网首页 http://192.168.4.1/，
 * 触发手机系统弹出 Captive Portal 登录窗口。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK。
 * 特殊行为：不续期配网超时（探测请求来自系统后台，不应算作用户活跃）。
 */
esp_err_t captive_portal_detect_handler(httpd_req_t *req);
