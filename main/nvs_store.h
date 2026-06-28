/**
 * NVS 配置桥接（ESP-IDF 专用）
 * 把领域层 config_data_t 与 power_engine/profile 状态在 NVS（namespace "bikepower"）间读写。
 * schema 校验/迁移/默认值由领域层 config_store 负责，本模块只做 NVS I/O 与字段搬运。
 */
#pragma once

#include "power_engine.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * 从 NVS 读取配置并注入 power_engine。
 * 负责把持久化配置加载到引擎和 profile 中。
 * e：待写入配置的功率引擎实例。
 * 返回值：无。
 * 特殊行为：NVS 缺失或版本不匹配时会使用默认值并回写，越界字段会先被校验夹紧。
 */
void nvs_store_load(power_engine_t *e);

/**
 * 把当前引擎配置写回 NVS。
 * 负责把 power_engine 和 profile 的当前状态落盘。
 * e：待保存的功率引擎实例。
 * 返回值：`true` 表示保存成功，`false` 表示保存失败。
 * 特殊行为：只负责当前快照写入，不主动清除领域层 dirty 标记。
 */
bool nvs_store_save(power_engine_t *e);

// ===== 家庭 WiFi 凭证（v3.1.6 在线 OTA，独立 namespace "wifica"） =====

/**
 * 保存家庭 WiFi 凭证到独立 namespace（NVS_NAMESPACE_WIFI）。
 * 与主配置 schema 隔离，不影响 config_data_t 版本迁移。
 * ssid：路由器 SSID（最长 HOME_WIFI_SSID_MAX 字节）。
 * psk：路由器密码（最长 HOME_WIFI_PSK_MAX 字节）。
 * 返回值：`true` 表示保存成功，`false` 表示失败。
 * 特殊行为：长度超限会截断；NVS 写入失败仅记日志不中断。
 */
bool nvs_store_save_wifi_cred(const char *ssid, const char *psk);

/**
 * 从独立 namespace 加载家庭 WiFi 凭证。
 * ssid：输出缓冲区，调用方分配（建议大小 HOME_WIFI_SSID_MAX）。
 * ssid_sz：ssid 缓冲区大小。
 * psk：输出缓冲区，可为 NULL（只加载 ssid，用于页面回显不暴露密码）。
 * psk_sz：psk 缓冲区大小，psk 为 NULL 时忽略。
 * 返回值：`true` 表示加载成功且 ssid 非空，`false` 表示无记录或失败。
 * 特殊行为：无记录时缓冲区置空字符串；长度超缓冲区会截断。
 */
bool nvs_store_load_wifi_cred(char *ssid, size_t ssid_sz, char *psk, size_t psk_sz);
