/**
 * OTA 在线下载模块（v3.1.6）：HTTPS 检查版本 + 下载升级
 *
 * 职责边界：
 *   - 从 OTA_VERSION_URL 拉取 latest.json，解析最新版本号/下载 URL/更新说明
 *   - 用 esp_https_ota 组件流式下载固件 bin 并写入 OTA 分区
 *   - 下载完成后切换启动分区（重启由调用方触发）
 *
 * 不负责：
 *   - WiFi STA 连接（由 radio_ctrl 负责）
 *   - 防降级校验（esp_ota_end 内置 magic 校验；版本比较由调用方决定）
 *   - HTTP 路由注册（在 wifi_portal.c 统一注册）
 *
 * 前置条件：
 *   - 设备已通过 radio_ctrl_sta_connect 或 radio_ctrl_enter_sta_exclusive 连上路由器
 *   - sdkconfig 已启用 CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL（含 Gitee 根证书）
 *
 * latest.json 格式（放仓库 ota/latest.json，raw 访问需仓库公开）：
 *   {"version":"3.1.6","url":"https://gitee.com/.../bikepower-c.bin","size":1048576,"changelog":"..."}
 */
#pragma once

#include "esp_err.h"

/**
 * 最新版本信息（从 latest.json 解析）。
 * 用途：承载检查版本结果，供调用方判断是否有更新并展示给用户。
 */
typedef struct {
    char version[16];     // 最新版本号，如 "3.1.6"
    char url[256];        // 固件 bin 下载 URL（HTTPS）
    int  size;            // 固件大小（字节，0 表示未知）
    char changelog[128];  // 更新说明
} ota_version_info_t;

/**
 * 从远程拉取 latest.json 并解析版本信息。
 * 用 esp_http_client GET 指定 URL，解析 JSON 后填充 info 结构。
 * json_url：latest.json 的完整 URL（通常用 OTA_VERSION_URL 宏）。
 * info：输出参数，填充解析后的版本信息。
 * 返回值：ESP_OK 表示成功获取并解析，否则为错误码。
 * 特殊行为：调用前必须已连上路由器（STA 获 IP）；JSON 字段缺失时对应字段留空。
 */
esp_err_t ota_downloader_check_version(const char *json_url, ota_version_info_t *info);

/**
 * HTTPS 流式下载固件并写入 OTA 分区，完成后切换启动分区。
 * 用 esp_https_ota 组件一站式下载+写入+切换，证书用 mbedTLS 证书包。
 * url：固件 bin 的完整 HTTPS URL。
 * 返回值：ESP_OK 表示下载并写入成功（已 set_boot_partition，待重启），否则为错误码。
 * 特殊行为：调用前必须已连上路由器；成功后调用方需 esp_restart 进入新固件；
 *           下载过程阻塞，可能耗时 30-120s（取决于固件大小和网络）。
 */
esp_err_t ota_downloader_download_and_upgrade(const char *url);
