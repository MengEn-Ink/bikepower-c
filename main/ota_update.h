/**
 * OTA 升级模块：自检确认/回滚 + 版本比较工具
 *
 * 职责边界：
 *   - app_main 启动时检测 PENDING_VERIFY，10s 自检通过则确认，否则回滚
 *   - 提供 semantic 版本比较工具（供在线 OTA 防降级使用）
 *
 * 不负责：
 *   - HTTP 路由注册（在 wifi_portal.c 统一注册）
 *   - 固件下载/写入（在 ota_downloader.c）
 *   - SoftAP/WiFi 管理（在 radio_ctrl.c）
 *
 * 参考：ESP-IDF advanced_ota 示例、esp_app_desc.h（固件偏移 0x20 嵌入 app_desc）
 */
#pragma once

#include "esp_err.h"

/**
 * 语义版本比较（major.minor.patch）。
 * remote：远端版本字符串。
 * current：当前版本字符串。
 * 返回值：>0 表示 remote 更新，==0 相同，<0 表示 remote 更旧（降级）。
 * 特殊行为：非 x.y.z 格式回退到 strcmp。
 */
int ota_version_compare(const char *remote, const char *current);

/**
 * OTA 启动自检：检测当前固件是否处于 PENDING_VERIFY。
 * 若是，启动 10s 一次性定时器，到期后确认有效；
 * 期间若调用 ota_update_mark_invalid() 则标记无效并回滚重启。
 * 无参数。
 * 返回值：无。
 * 特殊行为：仅在 OTA 升级后首次启动调用一次；正常启动直接返回。
 */
void ota_update_check_and_confirm(void);

/**
 * 标记当前固件自检失败并回滚。
 * 在自检窗口内发现关键模块初始化失败时调用，立即回滚到上一可用固件。
 * 无参数。
 * 返回值：无。
 * 特殊行为：调用后设备会立即重启，函数不返回。
 */
void ota_update_mark_invalid_and_rollback(void);
