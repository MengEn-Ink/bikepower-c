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

#include "esp_err.h"
#include "power_engine.h"

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
