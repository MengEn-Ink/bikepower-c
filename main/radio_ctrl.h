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
#include "esp_wifi.h"   // wifi_ap_record_t

/**
 * 进入 WiFi 配网模式（共存版）。
 * BLE 保持在线继续通知，直接启动 SoftAP 和 HTTP 配网门户，配置变更通过 power_engine setter 实时生效。
 * 极端失败兜底：WiFi 启动彻底失败时降级为停 BLE + esp_restart 恢复。
 * engine：配网门户读写配置时使用的功率引擎实例。
 * 返回值：无。
 * 特殊行为：这是唯一允许启动 WiFi SoftAP 的入口，调用后设备进入 ST_WIFI_CONFIG 态，
 *           BLE 仍在线但射频由 coex 时间片仲裁。
 */
void radio_ctrl_enter_wifi(power_engine_t *engine);

/**
 * 退出 WiFi 配网模式（共存版）。
 * 关闭 SoftAP 和 netif 资源，射频带宽归还 BLE；不重启设备，不触碰 BLE 协议栈。
 * 无参数。
 * 返回值：无。
 * 特殊行为：通常由 wifi_portal 保存/超时后通过回调间接触发；调用后状态机回到 ST_BLE_ACTIVE。
 */
void radio_ctrl_exit_wifi(void);

/**
 * 启动 WiFi SoftAP（由 radio_ctrl 独占管理）。
 * 初始化 netif、事件循环、WiFi 驱动并启动 AP 模式，失败时逐步清理并返回错误码。
 * 无参数。
 * 返回值：`ESP_OK` 表示 AP 已成功启动，否则为对应错误码。
 * 特殊行为：BLE 是否在运行由调用方决定；本函数只负责 AP，不负责 HTTP 门户；
 *           成功后会记录 netif/init/event_loop 句柄标志，供 radio_ctrl_stop_wifi_ap 按序释放。
 */
esp_err_t radio_ctrl_start_wifi_ap(void);

/**
 * 关闭 WiFi SoftAP 并释放 netif/事件循环资源。
 * 按 WiFi → netif → event_loop → netif_deinit 顺序释放，与启动顺序严格相反。
 * 无参数。
 * 返回值：无。
 * 特殊行为：仅释放本模块自建的资源；不重启设备，不触碰 BLE；可重复调用（幂等）。
 */
void radio_ctrl_stop_wifi_ap(void);

/**
 * 查询 BLE 是否被射频控制器有意挂起。
 * 无参数。
 * 返回值：true 表示当前处于独占 WiFi/OTA 兜底路径，主循环不应自动重启 BLE。
 * 特殊行为：用于区分 BLE 真故障和 OTA/独占 WiFi 期间的主动停用。
 */
bool radio_ctrl_ble_suspended(void);

// ===== STA 模式（v3.1.6：在线 OTA 下载） =====

/**
 * 在 AP 基础上切 APSTA 共存并连接家庭路由器（检查版本用）。
 * BLE 和 SoftAP 保持在线，STA 临时连路由器获 IP 后可访问互联网。
 * 调用前必须已通过 radio_ctrl_enter_wifi 启动 AP 模式。
 * ssid：家庭路由器 SSID。
 * psk：家庭路由器密码。
 * timeout_ms：连接超时（毫秒），超时返回 ESP_ERR_TIMEOUT。
 * 返回值：ESP_OK 表示 STA 已获 IP 可上网，否则为错误码。
 * 特殊行为：成功后配网页仍可访问（APSTA 共存）；调用方用完必须调 radio_ctrl_sta_disconnect 切回 AP-only。
 */
esp_err_t radio_ctrl_sta_connect(const char *ssid, const char *psk, int timeout_ms);

/**
 * 断开 STA 并切回 AP-only 模式。
 * 断开家庭路由器连接，WiFi 模式从 APSTA 切回 AP，SoftAP 继续服务配网页。
 * 无参数。
 * 返回值：无。
 * 特殊行为：可重复调用（幂等）；STA netif 保留复用，不释放。
 */
void radio_ctrl_sta_disconnect(void);

/**
 * 进入 STA 独占模式（在线 OTA 下载升级用）。
 * 停 SoftAP + 停 BLE 释放射频，启动 STA 连家庭路由器，为 HTTPS 下载固件做准备。
 * 调用前必须已通过 radio_ctrl_enter_wifi 启动 AP 模式（即处于配网态）。
 * ssid：家庭路由器 SSID。
 * psk：家庭路由器密码。
 * 返回值：ESP_OK 表示 STA 已连上路由器可下载，否则为错误码。
 * 特殊行为：成功后射频由 STA 独占，BLE 断开、配网页不可访问；
 *           下载完成后设备会 esp_restart，无需调恢复函数。
 */
esp_err_t radio_ctrl_enter_sta_exclusive(const char *ssid, const char *psk);

/**
 * 扫描周边 WiFi 热点（v3.1.6：家庭网络配置用）。
 * 临时切 APSTA 模式执行主动扫描，扫描完成后切回 AP-only。
 * 调用前必须已通过 radio_ctrl_enter_wifi 启动 AP 模式。
 * aps：调用方分配的 AP 记录数组，用于接收扫描结果。
 * max_num：aps 数组最大容量（建议 <= 16）。
 * out_count：实际扫描到的 AP 数量。
 * 返回值：ESP_OK 表示扫描成功，否则为错误码。
 * 特殊行为：扫描期间 SoftAP 保持在线；耗时约 3-5 秒，禁止在 HTTP 线程调用。
 */
esp_err_t radio_ctrl_wifi_scan(wifi_ap_record_t *aps, int max_num, int *out_count);
