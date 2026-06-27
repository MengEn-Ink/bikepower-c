/**
 * OTA 升级模块：固件上传 + 自检确认/回滚
 *
 * 职责边界：
 *   - 接收 multipart/form-data 上传的固件 bin，流式写入 OTA 分区
 *   - 校验固件头（esp_ota_end 内置 magic/chip/segment 校验）
 *   - 防降级：比较固件中的 esp_app_desc_t.version 与当前版本
 *   - 切换启动分区并重启
 *   - app_main 启动时检测 PENDING_VERIFY，10s 自检通过则确认，否则回滚
 *
 * 不负责：
 *   - HTTP 路由注册（在 wifi_portal.c 统一注册）
 *   - SoftAP/WiFi 管理（在 radio_ctrl.c）
 *   - BLE 业务（OTA 期间 BLE 仍在线，但 OTA 是独占子流程）
 *
 * 参考：ESP-IDF advanced_ota 示例、esp_app_desc.h（固件偏移 0x20 嵌入 app_desc）
 */
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * OTA 上传 HTTP handler。
 * 接收 multipart/form-data 上传的固件 bin，流式写入下一个 OTA 分区。
 * 成功返回成功页（含 5s 重启倒计时），失败返回错误页。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK（响应已通过 SEND 完整发送）。
 * 特殊行为：处理期间续期配网超时，避免上传中被自动关 WiFi；
 *           上传成功后通过 esp_timer 异步延迟 3s 重启，让响应先发出去。
 */
esp_err_t ota_update_handle_upload(httpd_req_t *req);

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
