/**
 * OTA 在线下载实现：esp_http_client 检查版本 + esp_https_ota 下载升级
 *
 * 关键设计：
 *   1. check_version：用 esp_http_client GET latest.json，手写最小 JSON 解析（避免引入 cJSON）。
 *   2. download_and_upgrade：用 esp_https_ota 组件一站式下载+写入+切换，cert_pem=NULL 用证书包。
 *   3. 网络异常时返回错误码，由调用方决定重试或回退；本模块不重启设备。
 */
#include "ota_downloader.h"
#include "bikepower_config.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "OTA_DL";

#define OTA_JSON_BUF_MAX  1024   // latest.json 最大缓冲（实际 < 300 字节）

// ============================ SNTP 时间同步 ============================

/**
 * 同步系统时间：HTTPS 证书验证需要正确时间，ESP32 启动默认 1970 年会校验失败。
 * 阻塞等待最多 5s 获取时间，失败也继续（证书校验会报错，但日志可见）。
 * 返回值：true 已同步，false 未同步（继续流程让 SSL 报错）。
 */
static bool ota_sync_time(void)
{
    time_t now = time(NULL);
    if (now > 1700000000) {   // 已是 2023 年之后，认为时间有效
        return true;
    }
    ESP_LOGI(TAG, "时间未同步，启动 SNTP");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();

    int retry = 0;
    while (time(NULL) < 1700000000 && retry < 10) {
        vTaskDelay(pdMS_TO_TICKS(500));
        retry++;
    }
    if (time(NULL) > 1700000000) {
        ESP_LOGI(TAG, "SNTP 同步成功 retry=%d time=%ld", retry, (long)time(NULL));
        return true;
    }
    ESP_LOGW(TAG, "SNTP 同步失败，HTTPS 证书校验可能失败");
    return false;
}

// ============================ JSON 最小解析 ============================

/**
 * 从 JSON 字符串中提取指定键的字符串值。
 * 查找 "key":"value" 模式，返回 value 部分（不解析转义）。
 * json：完整 JSON 字符串。
 * key：待提取的键名（不带引号）。
 * out：输出缓冲区。
 * out_sz：输出缓冲区大小。
 * 返回值：true 表示找到并写入，false 表示未找到。
 * 特殊行为：遇到第一个闭合引号即结束，不处理转义（latest.json 由工具生成，无转义）。
 */
static bool json_get_str(const char *json, const char *key, char *out, size_t out_sz)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    const char *end = strchr(p, '"');
    if (!end) return false;
    size_t len = (size_t)(end - p);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

/**
 * 从 JSON 字符串中提取指定键的整数值。
 * 查找 "key":数字 模式，解析为 int。
 * json：完整 JSON 字符串。
 * key：待提取的键名。
 * 返回值：解析后的整数值，未找到返回 0。
 */
static int json_get_int(const char *json, const char *key)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    return (int)strtol(p, NULL, 10);
}

// ============================ 版本检查 ============================

esp_err_t ota_downloader_check_version(const char *json_url, ota_version_info_t *info)
{
    if (json_url == NULL || info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(info, 0, sizeof(*info));

    ota_sync_time();   // 同步时间确保 HTTPS 证书校验通过

    esp_http_client_config_t cfg = {
        .url = json_url,
        .timeout_ms = OTA_DOWNLOAD_TIMEOUT_MS,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,   // 启用证书包验证
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "http_client_init 失败");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "http_client_open 失败 ret=%d", ret);
        esp_http_client_cleanup(client);
        return ret;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        // chunked 传输时返回 -1，按上限读取
        content_length = OTA_JSON_BUF_MAX;
    } else if (content_length > OTA_JSON_BUF_MAX) {
        content_length = OTA_JSON_BUF_MAX;
    }

    int http_status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "latest.json HTTP %d content_length=%d", http_status, content_length);

    char *buf = malloc(OTA_JSON_BUF_MAX);
    if (buf == NULL) {
        ESP_LOGE(TAG, "JSON 缓冲分配失败");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    int total_read = 0;
    while (total_read < content_length) {
        int r = esp_http_client_read(client, buf + total_read, OTA_JSON_BUF_MAX - total_read);
        if (r <= 0) break;
        total_read += r;
    }
    buf[total_read < OTA_JSON_BUF_MAX ? total_read : OTA_JSON_BUF_MAX - 1] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "latest.json 读取 %d 字节", total_read);

    if (http_status != 200) {
        ESP_LOGE(TAG, "latest.json HTTP 状态码 %d", http_status);
        free(buf);
        return ESP_FAIL;
    }

    if (total_read <= 0) {
        ESP_LOGE(TAG, "latest.json 读取为空");
        free(buf);
        return ESP_FAIL;
    }

    // 解析 JSON 字段
    bool got_ver = json_get_str(buf, "version", info->version, sizeof(info->version));
    bool got_url = json_get_str(buf, "url", info->url, sizeof(info->url));
    info->size = json_get_int(buf, "size");
    json_get_str(buf, "changelog", info->changelog, sizeof(info->changelog));
    free(buf);

    if (!got_ver || !got_url) {
        ESP_LOGE(TAG, "JSON 字段缺失 ver=%d url=%d", got_ver, got_url);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "最新版本=%s 大小=%d URL=%s", info->version, info->size, info->url);
    return ESP_OK;
}

// ============================ 下载升级 ============================

esp_err_t ota_downloader_download_and_upgrade(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "开始 HTTPS 下载升级 %s 堆=%lu", url, (unsigned long)esp_get_free_heap_size());

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = OTA_DOWNLOAD_TIMEOUT_MS,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,   // 启用证书包验证
        .keep_alive_enable = true,
        .buffer_size = OTA_BUF_SIZE,
        .buffer_size_tx = 1024,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
        .bulk_flash_erase = false,        // 逐块擦除，降低峰值内存
        .partial_http_download = false,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota 失败 ret=%d 堆=%lu", ret, (unsigned long)esp_get_free_heap_size());
        return ret;
    }

    ESP_LOGI(TAG, "固件下载写入完成，待重启进入新固件 堆=%lu", (unsigned long)esp_get_free_heap_size());
    return ESP_OK;
}
