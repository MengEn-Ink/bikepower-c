/**
 * OTA 升级实现：multipart 流式接收 + esp_ota API + 防降级 + 自检回滚
 *
 * 关键设计：
 *   1. 流式处理 multipart body：边接收边解析 boundary，固件数据直接 esp_ota_write，
 *      不全量缓冲，内存占用 O(块大小 + boundary 长度)。
 *   2. 防降级：固件 bin 偏移 0x20 处嵌入 esp_app_desc_t（ESP-IDF 编译时自动生成），
 *      含 version 字段。上传时先累积前 512 字节，解析 app_desc 比较 version。
 *   3. 上传完成后不立即重启：先返回 HTTP 成功页，esp_timer 异步 3s 后 esp_restart。
 *   4. 自检回滚：app_main 启动调 ota_update_check_and_confirm，10s 一次性定时器到点确认。
 */
#include "ota_update.h"
#include "bikepower_config.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "OTA";

#define OTA_RECV_CHUNK      1024
#define OTA_HEADER_BUF_LEN  512
#define OTA_FIRMWARE_MAX    (0x1C0000 - 0x1000)
#define OTA_SELFTEST_MS     10000
#define OTA_RESTART_DELAY_MS 3000

static esp_timer_handle_t s_selftest_timer = NULL;
static volatile bool s_selftest_done = false;

// ============================ 内部工具 ============================

static int version_compare(const char *remote, const char *current)
{
    int r_major = 0, r_minor = 0, r_patch = 0;
    int c_major = 0, c_minor = 0, c_patch = 0;
    if (sscanf(remote, "%d.%d.%d", &r_major, &r_minor, &r_patch) != 3) {
        return strcmp(remote, current);
    }
    if (sscanf(current, "%d.%d.%d", &c_major, &c_minor, &c_patch) != 3) {
        return strcmp(remote, current);
    }
    if (r_major != c_major) return r_major > c_major ? 1 : -1;
    if (r_minor != c_minor) return r_minor > c_minor ? 1 : -1;
    if (r_patch != c_patch) return r_patch > c_patch ? 1 : -1;
    return 0;
}

static esp_err_t check_firmware_version(const uint8_t *header, size_t len)
{
    if (len < sizeof(esp_app_desc_t) + 0x20) {
        ESP_LOGE(TAG, "固件头数据不足");
        return ESP_ERR_INVALID_SIZE;
    }
    const esp_app_desc_t *desc = (const esp_app_desc_t *)(header + 0x20);
    if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD) {
        ESP_LOGE(TAG, "固件 app_desc magic 不匹配");
        return ESP_ERR_INVALID_VERSION;
    }
    ESP_LOGI(TAG, "上传固件版本=%s 当前版本=%s", desc->version, FIRMWARE_VERSION);
    int cmp = version_compare(desc->version, FIRMWARE_VERSION);
    if (cmp < 0) {
        ESP_LOGE(TAG, "拒绝降级：%s < %s", desc->version, FIRMWARE_VERSION);
        return ESP_ERR_INVALID_VERSION;
    }
    if (cmp == 0) {
        ESP_LOGW(TAG, "同版本重刷 %s", desc->version);
    }
    return ESP_OK;
}

static void restart_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "延迟重启到期，进入新固件");
    esp_restart();
}

static const uint8_t *mem_find(const uint8_t *hay, size_t hay_len,
                               const uint8_t *needle, size_t needle_len)
{
    if (needle_len == 0 || hay_len < needle_len) return NULL;
    for (size_t i = 0; i + needle_len <= hay_len; i++) {
        if (memcmp(hay + i, needle, needle_len) == 0) return hay + i;
    }
    return NULL;
}

// ============================ OTA 上传 handler ============================

static void send_ota_result(httpd_req_t *req, bool success, const char *msg)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (success) {
        httpd_resp_sendstr(req,
            "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>升级成功</title></head>"
            "<body style=\"background:#f5f5f7;min-height:100vh;margin:0;padding:24px;display:flex;justify-content:center;align-items:center;font-family:-apple-system,sans-serif;color:#1d1d1f\">"
            "<div style=\"max-width:360px;background:#fff;border-radius:26px;padding:28px;text-align:center;box-shadow:0 12px 32px rgba(0,0,0,.08)\">"
            "<div style=\"width:64px;height:64px;border-radius:50%;margin:0 auto 14px;background:#e9f8ef;color:#15803d;font-size:38px;display:flex;align-items:center;justify-content:center\">+</div>"
            "<h1 style=\"font-size:22px;margin:0 0 8px\">固件升级成功</h1>"
            "<p style=\"color:#6e6e73;font-size:13px;margin:0 0 16px\">设备将在 3 秒后重启进入新固件，请勿断电。</p>"
            "<p style=\"color:#86868b;font-size:12px;margin:0\" id=\"cnt\">3</p>"
            "<script>var s=3;setInterval(function(){s--;document.getElementById('cnt').textContent=s},1000)</script>"
            "</div></body></html>");
    } else {
        char buf[1280];
        snprintf(buf, sizeof(buf),
            "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>升级失败</title></head>"
            "<body style=\"background:#f5f5f7;min-height:100vh;margin:0;padding:24px;display:flex;justify-content:center;align-items:center;font-family:-apple-system,sans-serif;color:#1d1d1f\">"
            "<div style=\"max-width:360px;background:#fff;border-radius:26px;padding:28px;text-align:center;box-shadow:0 12px 32px rgba(0,0,0,.08)\">"
            "<div style=\"width:64px;height:64px;border-radius:50%%;margin:0 auto 14px;background:#fef2f2;color:#dc2626;font-size:38px;display:flex;align-items:center;justify-content:center\">x</div>"
            "<h1 style=\"font-size:22px;margin:0 0 8px\">升级失败</h1>"
            "<p style=\"color:#6e6e73;font-size:13px;margin:0 0 16px\">%s</p>"
            "<a href=\"/ota\" style=\"display:inline-block;padding:12px 24px;background:#007aff;color:#fff;border-radius:16px;text-decoration:none;font-weight:800\">返回重试</a>"
            "</div></body></html>",
            msg ? msg : "未知错误");
        httpd_resp_sendstr(req, buf);
    }
}

esp_err_t ota_update_handle_upload(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA 上传开始 Content-Length=%u 堆=%lu",
             (unsigned)req->content_len, (unsigned long)esp_get_free_heap_size());

    if (req->content_len == 0) {
        send_ota_result(req, false, "请求体为空");
        return ESP_OK;
    }
    if (req->content_len > OTA_FIRMWARE_MAX) {
        send_ota_result(req, false, "固件过大");
        return ESP_OK;
    }

    // 解析 boundary
    size_t ct_len = httpd_req_get_hdr_value_len(req, "Content-Type");
    if (ct_len == 0 || ct_len >= 256) {
        send_ota_result(req, false, "无法读取 Content-Type");
        return ESP_OK;
    }
    char *ct = malloc(ct_len + 1);
    if (!ct) { send_ota_result(req, false, "内存不足"); return ESP_OK; }
    if (httpd_req_get_hdr_value_str(req, "Content-Type", ct, ct_len + 1) != ESP_OK) {
        free(ct);
        send_ota_result(req, false, "读取 Content-Type 失败");
        return ESP_OK;
    }
    char boundary[80] = {0};
    const char *bp = strstr(ct, "boundary=");
    if (!bp) {
        free(ct);
        send_ota_result(req, false, "无 boundary");
        return ESP_OK;
    }
    bp += 9;
    if (*bp == '"') bp++;
    size_t bl = strlen(bp);
    while (bl > 0 && (bp[bl-1] == '"' || bp[bl-1] == ' ' || bp[bl-1] == '\r')) bl--;
    if (bl == 0 || bl + 3 >= sizeof(boundary)) {
        free(ct);
        send_ota_result(req, false, "boundary 非法");
        return ESP_OK;
    }
    boundary[0] = '-'; boundary[1] = '-';
    memcpy(boundary + 2, bp, bl);
    boundary[bl + 2] = '\0';
    free(ct);
    size_t boundary_len = strlen(boundary);

    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        send_ota_result(req, false, "无可用 OTA 分区");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "目标分区 %s offset=0x%x", target->label, target->address);

    esp_ota_handle_t handle;
    esp_err_t ret = esp_ota_begin(target, OTA_SIZE_UNKNOWN, &handle);
    if (ret != ESP_OK) {
        send_ota_result(req, false, "OTA 开始失败");
        return ESP_OK;
    }

    uint8_t *chunk = malloc(OTA_RECV_CHUNK);
    uint8_t *residual = malloc(boundary_len + 8);
    uint8_t header_buf[OTA_HEADER_BUF_LEN];
    size_t header_filled = 0;
    bool header_done = false;
    bool in_data = false;
    bool firmware_written = false;
    bool upload_complete = false;
    esp_err_t err = ESP_OK;
    size_t firmware_bytes = 0;
    size_t total_remaining = req->content_len;

    if (!chunk || !residual) {
        if (chunk) free(chunk);
        if (residual) free(residual);
        esp_ota_abort(handle);
        send_ota_result(req, false, "内存不足");
        return ESP_OK;
    }
    size_t residual_len = 0;

    while (total_remaining > 0 && err == ESP_OK && !upload_complete) {
        size_t want = total_remaining > OTA_RECV_CHUNK ? OTA_RECV_CHUNK : total_remaining;
        int got = httpd_req_recv(req, (char *)chunk, want);
        if (got <= 0) {
            if (got == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "HTTP 接收失败 got=%d", got);
            err = ESP_FAIL;
            break;
        }
        size_t chunk_len = (size_t)got;
        total_remaining -= chunk_len;

        // 拼接 residual + chunk → work
        size_t work_len = residual_len + chunk_len;
        uint8_t *work = malloc(work_len);
        if (!work) { err = ESP_ERR_NO_MEM; break; }
        memcpy(work, residual, residual_len);
        memcpy(work + residual_len, chunk, chunk_len);
        residual_len = 0;

        size_t pos = 0;
        while (pos < work_len && err == ESP_OK && !upload_complete) {
            if (!in_data) {
                // 找 \r\n\r\n 进入数据区
                const uint8_t *sep = mem_find(work + pos, work_len - pos,
                                              (const uint8_t *)"\r\n\r\n", 4);
                if (sep) {
                    pos = (size_t)(sep - work) + 4;
                    in_data = true;
                } else {
                    // 跨 chunk，保留尾部
                    size_t keep = work_len - pos;
                    if (keep > 8) keep = 8;
                    memcpy(residual, work + pos, keep);
                    residual_len = keep;
                    pos = work_len;
                    break;
                }
            }

            if (in_data) {
                // 找结束 boundary
                const uint8_t *bpos = mem_find(work + pos, work_len - pos,
                                               (const uint8_t *)boundary, boundary_len);
                if (bpos) {
                    size_t data_end = (size_t)(bpos - work);
                    // 去掉前导 \r\n
                    if (data_end >= 2 && work[data_end-2] == '\r' && work[data_end-1] == '\n') {
                        data_end -= 2;
                    }
                    size_t data_len = data_end - pos;
                    if (data_len > 0) {
                        if (!header_done) {
                            size_t to_copy = OTA_HEADER_BUF_LEN - header_filled;
                            if (to_copy > data_len) to_copy = data_len;
                            memcpy(header_buf + header_filled, work + pos, to_copy);
                            header_filled += to_copy;
                            if (header_filled >= OTA_HEADER_BUF_LEN) header_done = true;
                        }
                        ret = esp_ota_write(handle, work + pos, data_len);
                        if (ret != ESP_OK) {
                            ESP_LOGE(TAG, "esp_ota_write 失败 ret=%d", ret);
                            err = ret;
                            break;
                        }
                        firmware_bytes += data_len;
                        firmware_written = true;
                    }
                    upload_complete = true;
                    ESP_LOGI(TAG, "固件数据接收完毕 bytes=%u", (unsigned)firmware_bytes);
                    break;
                } else {
                    // 未找到，写入安全区（保留尾部 boundary_len 防 cross-chunk）
                    size_t safe = work_len - pos;
                    if (safe > boundary_len) {
                        size_t write_len = safe - boundary_len;
                        if (!header_done) {
                            size_t to_copy = OTA_HEADER_BUF_LEN - header_filled;
                            if (to_copy > write_len) to_copy = write_len;
                            memcpy(header_buf + header_filled, work + pos, to_copy);
                            header_filled += to_copy;
                            if (header_filled >= OTA_HEADER_BUF_LEN) header_done = true;
                        }
                        ret = esp_ota_write(handle, work + pos, write_len);
                        if (ret != ESP_OK) {
                            ESP_LOGE(TAG, "esp_ota_write 失败 ret=%d", ret);
                            err = ret;
                            break;
                        }
                        firmware_bytes += write_len;
                        firmware_written = true;
                        pos += write_len;
                        // 保留尾部 boundary_len 字节
                        memcpy(residual, work + pos, boundary_len);
                        residual_len = boundary_len;
                        break;
                    } else {
                        memcpy(residual, work + pos, safe);
                        residual_len = safe;
                        break;
                    }
                }
            }
        }
        free(work);
    }

    free(chunk);
    free(residual);

    if (err != ESP_OK || !firmware_written) {
        esp_ota_abort(handle);
        send_ota_result(req, false, "固件接收失败");
        return ESP_OK;
    }

    // 校验版本（防降级）
    if (header_filled >= sizeof(esp_app_desc_t) + 0x20) {
        ret = check_firmware_version(header_buf, header_filled);
        if (ret != ESP_OK) {
            esp_ota_abort(handle);
            send_ota_result(req, false, "版本校验失败（拒绝降级或固件无效）");
            return ESP_OK;
        }
    }

    ret = esp_ota_end(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end 失败 ret=%d", ret);
        send_ota_result(req, false, "固件校验失败（损坏或非有效固件）");
        return ESP_OK;
    }

    ret = esp_ota_set_boot_partition(target);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition 失败 ret=%d", ret);
        send_ota_result(req, false, "切换启动分区失败");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "OTA 完成 bytes=%u 3s 后重启", (unsigned)firmware_bytes);
    send_ota_result(req, true, NULL);

    const esp_timer_create_args_t targs = { .callback = restart_timer_cb, .name = "ota_restart" };
    esp_timer_handle_t restart_timer = NULL;
    if (esp_timer_create(&targs, &restart_timer) == ESP_OK) {
        esp_timer_start_once(restart_timer, OTA_RESTART_DELAY_MS * 1000);
    } else {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    return ESP_OK;
}

// ============================ 自检回滚 ============================

static void selftest_timer_cb(void *arg)
{
    (void)arg;
    if (s_selftest_done) return;
    s_selftest_done = true;
    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "新固件自检通过，已确认有效");
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGD(TAG, "非 PENDING_VERIFY，无需确认");
    } else {
        ESP_LOGW(TAG, "mark_valid ret=%d", ret);
    }
    if (s_selftest_timer) {
        esp_timer_delete(s_selftest_timer);
        s_selftest_timer = NULL;
    }
}

void ota_update_check_and_confirm(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) return;
    esp_ota_img_states_t state;
    esp_err_t ret = esp_ota_get_state_partition(running, &state);
    if (ret != ESP_OK) return;
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGD(TAG, "运行分区 %s 状态=%d 正常启动", running->label, state);
        return;
    }

    ESP_LOGI(TAG, "新固件首次启动 PENDING_VERIFY，启动 10s 自检");
    s_selftest_done = false;
    const esp_timer_create_args_t targs = { .callback = selftest_timer_cb, .name = "ota_selftest" };
    if (esp_timer_create(&targs, &s_selftest_timer) != ESP_OK) {
        ESP_LOGE(TAG, "自检定时器创建失败，立即确认");
        esp_ota_mark_app_valid_cancel_rollback();
        return;
    }
    if (esp_timer_start_once(s_selftest_timer, OTA_SELFTEST_MS * 1000) != ESP_OK) {
        ESP_LOGE(TAG, "自检定时器启动失败，立即确认");
        esp_timer_delete(s_selftest_timer);
        s_selftest_timer = NULL;
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

void ota_update_mark_invalid_and_rollback(void)
{
    if (s_selftest_done) return;
    s_selftest_done = true;
    ESP_LOGE(TAG, "自检失败，标记无效并回滚重启");
    if (s_selftest_timer) {
        esp_timer_stop(s_selftest_timer);
        esp_timer_delete(s_selftest_timer);
        s_selftest_timer = NULL;
    }
    esp_ota_mark_app_invalid_rollback_and_reboot();
}
