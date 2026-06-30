/**
 * WiFi 配网门户实现：HTTP 门户（esp_http_server）—— v3.2.0 前后端分离
 *
 * 架构（v3.2.0 重构）：
 *   - 静态资源：web/index.html + app.css + app.js 由 scripts/inline_static.py
 *     在编译时内联为 main/web_index_html.h，C 侧 GET / 原样吐出。
 *   - JSON API：21 个 snprintf 拼 HTML 的 handler 改为 14 个 /api/xxx JSON 接口。
 *   - 前端 SPA：app.js 通过 fetch() 调用 /api/xxx 自行渲染，完全脱离 C 代码。
 *
 * 路由表：
 *   GET  /                          静态首页（内联完整 HTML/CSS/JS）
 *   GET  /favicon.ico               204
 *   GET  /api/state                 整合状态 JSON（首页所需全部数据）
 *   POST /api/config                保存配置 + NVS 持久化（不关 WiFi）
 *   POST /api/draft                 仅内存草稿（不持久化）
 *   POST /api/close-wifi            保存配置 + 5s 后关 WiFi 回到 BLE
 *   GET  /api/time                  剩余秒数（纯文本，高频轮询优化）
 *   POST /api/renew                 续期（纯文本返回剩余秒数）
 *   GET  /api/mem_stats             内存统计 JSON
 *   POST /api/ota/check             异步触发版本检查
 *   GET  /api/ota/status            查询版本检查状态
 *   POST /api/ota/upgrade           触发在线升级
 *   POST /api/wifi/scan             异步扫描周边 WiFi
 *   GET  /api/wifi/scan/status      查询扫描结果
 *   POST /api/wifi/save             保存家庭 WiFi 凭证 + 测试连接
 *   GET  /api/wifi/connect/status   查询连接测试状态
 *   Captive Portal 探测端点 × 9（iOS/Android/Windows/Firefox/Kindle）
 *
 * 超时：180s 无续期自动关闭 WiFi 回到 BLE（不重启）；配置通过 power_engine setter 实时生效到 BLE 通知。
 *
 * WiFi SoftAP 启动已收回 radio_ctrl.c 统一管理，本文件只负责 HTTP 路由和 JSON API。
 * 调用方（radio_ctrl）需在调用 wifi_portal_start 前确保 AP 已就绪。
 */
#include "wifi_portal.h"
#include "web_index_html.h"   /* scripts/inline_static.py 自动生成 */
#include "captive_portal.h"
#include "ota_update.h"
#include "ota_downloader.h"
#include "radio_ctrl.h"
#include "nvs_store.h"
#include "ble_bikepower.h"
#include "bikepower_config.h"
#include "profile.h"
#include "json_miniparse.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_wifi.h"   /* wifi_ap_record_t（家庭网络扫描） */
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

static const char *TAG = "PORTAL";

static power_engine_t *s_engine = NULL;
static httpd_handle_t s_httpd = NULL;
static esp_timer_handle_t s_shutdown_timer = NULL;
// 截止时间存为 32 位秒级单调时间：ESP32-C3 是 32 位核，单字读写原子，
// 避免 64 位变量在 httpd 任务续期与 esp_timer 回调读取之间发生撕裂导致误判提前关闭。
static volatile uint32_t s_deadline_sec = 0;
// 配网结束标志：true 表示已进入收尾流程（保存成功或超时），不再续期、不再接受新请求
static volatile bool s_closing = false;
// done_cb 幂等标志：true 表示 portal_done 已调用过 done_cb，防止定时器重复触发投递多个事件。
static volatile bool s_done_called = false;
// 配网结束回调：保存/超时后由 portal 调用，通知 app_main 投递 APP_EVT_WIFI_DONE 切换状态
static void (*s_done_cb)(void) = NULL;

// ============================ 超时管理 ============================

static uint32_t now_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static void arm_deadline(void)
{
    if (s_closing) return;
    s_deadline_sec = now_sec() + (WIFI_SHUTDOWN_MS / 1000);
}

static int remaining_seconds(void)
{
    uint32_t now = now_sec();
    if (now >= s_deadline_sec) return 0;
    return (int)(s_deadline_sec - now);
}

static void portal_done(void);  /* 前向声明 */
static void shutdown_check_cb(void *arg)
{
    (void)arg;
    if (s_done_called) return;
    if (now_sec() >= s_deadline_sec) {
        ESP_LOGI(TAG, "WiFi 配网窗口到期/收尾窗口到期，关闭 WiFi 回到 BLE");
        portal_done();
    }
}

static void portal_done(void)
{
    if (s_done_called) return;
    s_done_called = true;
    s_closing = true;
    ESP_LOGI(TAG, "portal_done 触发，调用 done_cb 投递 APP_EVT_WIFI_DONE");
    if (s_done_cb) s_done_cb();
    else ESP_LOGE(TAG, "s_done_cb 为 NULL，APP_EVT_WIFI_DONE 无法投递");
}

// ============================ 工具函数 ============================

/**
 * 读取完整 HTTP 请求体到动态缓冲区。
 * 在末尾补 '\0' 便于后续按字符串解析。
 * 超过 HTTP_MAX_BODY_LEN 字节时返回 -2，由调用方返回 413。
 */
static int recv_body(httpd_req_t *req, char **out)
{
    size_t total = req->content_len;
    if (total == 0) { *out = NULL; return -1; }
    if (total > HTTP_MAX_BODY_LEN) { *out = NULL; return -2; }
    char *buf = malloc(total + 1);
    if (!buf) return -1;
    size_t recv = 0;
    while (recv < total) {
        int r = httpd_req_recv(req, buf + recv, total - recv);
        if (r <= 0) { free(buf); return -1; }
        recv += (size_t)r;
    }
    buf[recv] = '\0';
    *out = buf;
    return (int)recv;
}

static void set_no_store_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
}

/**
 * JSON 字符串转义：把 " 和 \ 转义，控制字符转 \uXXXX，写入 out。
 * out_sz 不足时截断但保证以 '\0' 结尾。
 */
static void json_escape(const char *in, char *out, size_t out_sz)
{
    if (out_sz == 0) return;
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 6 < out_sz; i++) {
        char c = in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = c;
        } else if ((unsigned char)c < 0x20) {
            snprintf(out + o, out_sz - o, "\\u%04x", (unsigned char)c);
            o += 6;
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

/* 检查 JSON 是否含指定 key（粗略匹配，避免 json_get_int 把"未传"和"=0"混淆） */
static bool json_has_key(const char *json, const char *key)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    return strstr(json, pat) != NULL;
}

/* 取 JSON 整数，未找到返回 def（用于保留旧值） */
static int json_get_int_def(const char *json, const char *key, int def)
{
    if (!json_has_key(json, key)) return def;
    return json_get_int(json, key);
}

/* 统一发送 JSON 响应（Connection: close + no-store） */
static void send_json(httpd_req_t *req, const char *body)
{
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "application/json");
    set_no_store_headers(req);
    httpd_resp_sendstr(req, body);
}

/* 统一发送纯文本响应（Connection: close + no-store） */
static void send_text(httpd_req_t *req, const char *body)
{
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "text/plain");
    set_no_store_headers(req);
    httpd_resp_sendstr(req, body);
}

// ============================ 模式映射 ============================

static int mode_from_id(const char *id)
{
    if (strcmp(id, "road") == 0) return RIDE_MODE_ROAD;
    if (strcmp(id, "erg") == 0) return RIDE_MODE_ERG;
    if (strcmp(id, "steady") == 0) return RIDE_MODE_STEADY;
    return -1;
}

static const char *mode_to_id(int mode)
{
    if (mode == RIDE_MODE_ROAD) return "road";
    if (mode == RIDE_MODE_ERG) return "erg";
    return "steady";
}

// ============================ 配置应用 ============================

/**
 * 把 JSON 请求体的字段统一应用到功率引擎。
 * 解析 mode/device_name/手动三项/profile 四项并写入引擎，供 /api/config 与 /api/draft 共用。
 * 未传字段保留旧值（与旧 form 实现一致）。
 * body：JSON 请求体（已 \0 终止）。
 * e：目标引擎实例。
 */
static void apply_json_fields(power_engine_t *e, const char *body)
{
    char val[64];

    if (json_get_str(body, "mode", val, sizeof(val))) {
        int m = mode_from_id(val);
        if (m >= 0) power_engine_set_mode(e, (ride_mode_t)m);
    }
    if (json_get_str(body, "device_name", val, sizeof(val))) {
        power_engine_set_device_name(e, val);
    }
    /* 手动三项：setter 内部仅 STEADY 生效，非 STEADY 自动拒绝 */
    power_engine_set_power(e, json_get_int_def(body, "power", e->base_power));
    power_engine_set_cadence(e, json_get_int_def(body, "cadence", e->cadence_base));
    power_engine_set_heartrate(e, json_get_int_def(body, "heartrate", e->last_heartrate));

    profile_set_weight(&e->profile, json_get_int_def(body, "profile_weight", e->profile.weight));
    profile_set_ftp(&e->profile, json_get_int_def(body, "profile_ftp", e->profile.ftp));
    profile_set_thr_hr(&e->profile, json_get_int_def(body, "profile_thr_hr", e->profile.thr_hr));
    if (json_get_str(body, "profile_ride_type", val, sizeof(val)) && val[0]) {
        profile_set_ride_type_str(&e->profile, val);
    }
}

// ============================ 静态首页 ============================

/**
 * 输出内联完整 HTML（含 CSS/JS，由 scripts/inline_static.py 在编译时打包）。
 * iOS Captive Portal WebView 不能加载外部 .css/.js，必须内联。
 */
static esp_err_t h_home(httpd_req_t *req)
{
    ESP_LOGI(TAG, "首页请求 堆=%lu", (unsigned long)esp_get_free_heap_size());
    arm_deadline();
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    set_no_store_headers(req);
    httpd_resp_send(req, index_html, index_html_len);
    return ESP_OK;
}

// ============================ /api/state ============================

/**
 * 整合状态 JSON：返回首页/更多/关于/OTA/WiFi 视图所需的全部数据。
 * 前端 init() 一次性拉取后缓存到 STATE，后续按视图渲染。
 */
static esp_err_t h_api_state(httpd_req_t *req)
{
    arm_deadline();
    power_engine_t *e = s_engine;

    char dn_esc[DEVICE_NAME_MAX * 2 + 1] = {0};
    json_escape(e->device_name, dn_esc, sizeof(dn_esc));

    char saved_ssid[HOME_WIFI_SSID_MAX] = {0};
    bool has_wifi = nvs_store_load_wifi_cred(saved_ssid, sizeof(saved_ssid), NULL, 0);
    char ssid_esc[HOME_WIFI_SSID_MAX * 2 + 1] = {0};
    if (has_wifi) json_escape(saved_ssid, ssid_esc, sizeof(ssid_esc));

    int ride_type_idx = (e->profile.ride_type >= 0 && e->profile.ride_type < PACER_COUNT)
                        ? e->profile.ride_type : 0;

    char buf[1536];
    int off = snprintf(buf, sizeof(buf),
        "{\"device_name\":\"%s\",\"firmware_version\":\"%s\","
        "\"power\":%d,\"cadence\":%d,\"heartrate\":%d,\"mode\":\"%s\","
        "\"profile_weight\":%d,\"profile_ftp\":%d,\"profile_thr_hr\":%d,"
        "\"profile_ride_type\":\"%s\","
        "\"remaining_sec\":%d,\"closing\":%s,\"home_wifi_ssid\":\"%s\","
        "\"pacers\":[",
        dn_esc, FIRMWARE_VERSION,
        e->base_power, e->cadence_base, e->last_heartrate, mode_to_id((int)e->mode),
        e->profile.weight, e->profile.ftp, e->profile.thr_hr,
        PROFILE_RIDE_TYPE_OPTIONS[ride_type_idx],
        remaining_seconds(), s_closing ? "true" : "false", ssid_esc);

    for (int i = 0; i < PACER_COUNT && off < (int)sizeof(buf) - 96; i++) {
        off += snprintf(buf + off, sizeof(buf) - off,
            "%s{\"id\":\"%s\",\"name\":\"%s\",\"wkg_low\":%d.%d,\"wkg_high\":%d.%d}",
            i == 0 ? "" : ",",
            PROFILE_RIDE_TYPE_OPTIONS[i], ROAD_PACERS[i].name,
            ROAD_PACERS[i].wkg_low / 10, ROAD_PACERS[i].wkg_low % 10,
            ROAD_PACERS[i].wkg_high / 10, ROAD_PACERS[i].wkg_high % 10);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "]}");

    send_json(req, buf);
    return ESP_OK;
}

// ============================ /api/config /api/draft /api/close-wifi ============================

/**
 * 保存配置到 NVS（不关 WiFi），实时生效到 BLE 通知。
 * 成功返回 {"ok":true}，失败返回 {"ok":false,"error":"..."}。
 */
static esp_err_t h_api_config(httpd_req_t *req)
{
    if (s_closing) {
        send_json(req, "{\"ok\":false,\"error\":\"配网收尾中\"}");
        return ESP_OK;
    }
    arm_deadline();

    char *body = NULL;
    int len = recv_body(req, &body);
    if (len == -2) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "request body too large");
        return ESP_FAIL;
    }
    if (len < 0) {
        send_json(req, "{\"ok\":false,\"error\":\"读取请求体失败\"}");
        return ESP_OK;
    }

    /* 写临时副本：NVS 失败时丢弃，s_engine 完全不变，BLE 不会读到中间值 */
    power_engine_t next;
    memcpy(&next, s_engine, sizeof(power_engine_t));
    apply_json_fields(&next, body);
    free(body);

    if (nvs_store_save(&next)) {
        bool name_changed = strcmp(s_engine->device_name, next.device_name) != 0;
        memcpy(s_engine, &next, sizeof(power_engine_t));
        power_engine_clear_dirty(s_engine);
        if (name_changed) ble_bikepower_refresh_device_name();
        ESP_LOGI(TAG, "配置已保存 p=%dW c=%dRPM mode=%d name=%s",
                 s_engine->base_power, s_engine->cadence_base,
                 (int)s_engine->mode, s_engine->device_name);
        send_json(req, "{\"ok\":true}");
    } else {
        ESP_LOGE(TAG, "配置保存失败，s_engine 保持原值");
        arm_deadline();
        send_json(req, "{\"ok\":false,\"error\":\"NVS 保存失败\"}");
    }
    return ESP_OK;
}

/**
 * 草稿保存：仅写内存不落盘，不持久化不重启。
 * apply_json_fields 会置 dirty，立即清除避免主循环心跳自动 nvs_store_save 把草稿误存进 NVS。
 */
static esp_err_t h_api_draft(httpd_req_t *req)
{
    if (s_closing) {
        send_json(req, "{\"ok\":false,\"error\":\"配网收尾中\"}");
        return ESP_OK;
    }
    arm_deadline();

    char *body = NULL;
    int len = recv_body(req, &body);
    if (len == -2) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "request body too large");
        return ESP_FAIL;
    }
    if (len >= 0) {
        apply_json_fields(s_engine, body);
        power_engine_clear_dirty(s_engine);
        power_engine_t *e = s_engine;
        ESP_LOGI(TAG, "草稿保存（仅内存，未落盘）mode=%d p=%d c=%d hr=%d name=%s w=%d ftp=%d thr=%d ride=%d",
                 (int)e->mode, e->base_power, e->cadence_base, e->last_heartrate,
                 e->device_name, e->profile.weight, e->profile.ftp,
                 e->profile.thr_hr, e->profile.ride_type);
        free(body);
    } else {
        send_json(req, "{\"ok\":false,\"error\":\"读取请求体失败\"}");
        return ESP_OK;
    }
    send_json(req, "{\"ok\":true}");
    return ESP_OK;
}

/**
 * 保存配置 + 5s 后关闭 WiFi 回到 BLE。
 * 成功后设置 5s 收尾窗口，由 shutdown_check_cb 触发 portal_done 关 WiFi。
 */
static esp_err_t h_api_close_wifi(httpd_req_t *req)
{
    if (s_closing) {
        send_json(req, "{\"ok\":false,\"error\":\"配网收尾中\"}");
        return ESP_OK;
    }
    arm_deadline();

    char *body = NULL;
    int len = recv_body(req, &body);
    if (len == -2) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "request body too large");
        return ESP_FAIL;
    }
    if (len < 0) {
        send_json(req, "{\"ok\":false,\"error\":\"读取请求体失败\"}");
        return ESP_OK;
    }

    power_engine_t next;
    memcpy(&next, s_engine, sizeof(power_engine_t));
    apply_json_fields(&next, body);
    free(body);

    if (nvs_store_save(&next)) {
        bool name_changed = strcmp(s_engine->device_name, next.device_name) != 0;
        memcpy(s_engine, &next, sizeof(power_engine_t));
        power_engine_clear_dirty(s_engine);
        if (name_changed) ble_bikepower_refresh_device_name();
        ESP_LOGI(TAG, "配置已保存并准备关闭 WiFi p=%dW c=%dRPM mode=%d name=%s",
                 s_engine->base_power, s_engine->cadence_base,
                 (int)s_engine->mode, s_engine->device_name);
        /* 5s 收尾窗口：让前端切到 #/closing 页面后关闭 WiFi */
        s_closing = true;
        s_deadline_sec = now_sec() + WIFI_PORTAL_REBOOT_DELAY_SEC;
        send_json(req, "{\"ok\":true}");
    } else {
        ESP_LOGE(TAG, "配置保存失败，s_engine 保持原值");
        arm_deadline();
        send_json(req, "{\"ok\":false,\"error\":\"NVS 保存失败\"}");
    }
    return ESP_OK;
}

// ============================ /api/time /api/renew ============================

static esp_err_t h_api_time(httpd_req_t *req)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", remaining_seconds());
    send_text(req, buf);
    return ESP_OK;
}

static esp_err_t h_api_renew(httpd_req_t *req)
{
    arm_deadline();
    return h_api_time(req);
}

// ============================ /api/mem_stats ============================

static esp_err_t h_api_mem_stats(httpd_req_t *req)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"free\":%u,\"min_free\":%u}",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
    send_json(req, buf);
    return ESP_OK;
}

// ============================ /api/ota/* ============================

/* OTA 版本检查状态（异步任务状态机，由 ota_check_task 推进） */
typedef enum {
    OTA_CHECK_IDLE = 0,
    OTA_CHECK_RUNNING,
    OTA_CHECK_DONE,
    OTA_CHECK_ERROR,
} ota_check_state_t;

static ota_check_state_t s_ota_check_state = OTA_CHECK_IDLE;
static char s_ota_check_version[32] = {0};
static char s_ota_check_changelog[128] = {0};
static char s_ota_check_error[64] = {0};
static bool s_ota_check_has_update = false;
static int s_ota_check_size = 0;

/**
 * 异步版本检查任务：连 STA+SNTP+拉 latest.json，结果写入全局变量。
 */
static void ota_check_task(void *arg)
{
    (void)arg;
    char ssid[HOME_WIFI_SSID_MAX] = {0};
    char psk[HOME_WIFI_PSK_MAX] = {0};

    if (!nvs_store_load_wifi_cred(ssid, sizeof(ssid), psk, sizeof(psk))) {
        snprintf(s_ota_check_error, sizeof(s_ota_check_error), "无 WiFi 凭证");
        s_ota_check_state = OTA_CHECK_ERROR;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "异步检查：STA 连接 %.32s", ssid);
    esp_err_t ret = radio_ctrl_sta_connect(ssid, psk, OTA_STA_CONNECT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "异步检查：STA 连接失败 ret=%d", ret);
        snprintf(s_ota_check_error, sizeof(s_ota_check_error), "WiFi连接失败");
        s_ota_check_state = OTA_CHECK_ERROR;
        vTaskDelete(NULL);
        return;
    }
    esp_task_wdt_reset();

    ESP_LOGI(TAG, "异步检查：STA 连接成功，拉 latest.json");
    ota_version_info_t info;
    ret = ota_downloader_check_version(OTA_VERSION_URL, &info);
    radio_ctrl_sta_disconnect();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "异步检查：版本拉取失败 ret=%d", ret);
        const char *reason = "无法获取版本信息";
        if (ret == ESP_ERR_NO_MEM) reason = "内存不足";
        snprintf(s_ota_check_error, sizeof(s_ota_check_error), "%s(err=%d)", reason, ret);
        s_ota_check_state = OTA_CHECK_ERROR;
        vTaskDelete(NULL);
        return;
    }

    s_ota_check_has_update = ota_version_compare(info.version, FIRMWARE_VERSION) > 0;
    s_ota_check_size = info.size;
    strncpy(s_ota_check_version, info.version, sizeof(s_ota_check_version) - 1);
    strncpy(s_ota_check_changelog, info.changelog, sizeof(s_ota_check_changelog) - 1);
    s_ota_check_state = OTA_CHECK_DONE;
    ESP_LOGI(TAG, "异步检查完成：当前=%s 最新=%s 有更新=%d",
             FIRMWARE_VERSION, info.version, s_ota_check_has_update);
    vTaskDelete(NULL);
}

/**
 * 异步 OTA 升级任务：停 AP+停 BLE+启 STA+下载+重启。
 * 任务结束前必 esp_restart，任何失败均重启回 BLE 模式。
 */
static void ota_upgrade_task(void *arg)
{
    (void)arg;
    char ssid[HOME_WIFI_SSID_MAX] = {0};
    char psk[HOME_WIFI_PSK_MAX] = {0};
    if (!nvs_store_load_wifi_cred(ssid, sizeof(ssid), psk, sizeof(psk))) {
        ESP_LOGE(TAG, "OTA 升级：无家庭 WiFi 凭证，放弃");
        vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_AFTER_UPGRADE_MS));
        esp_restart();
    }

    ESP_LOGI(TAG, "OTA 升级任务启动，延迟 3s 等 HTTP 响应发出");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_task_wdt_reset();

    esp_err_t ret = radio_ctrl_enter_sta_exclusive(ssid, psk);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "STA 独占失败 ret=%d，重启回 BLE", ret);
        vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_AFTER_UPGRADE_MS));
        esp_restart();
    }
    esp_task_wdt_reset();

    ota_version_info_t info;
    ret = ota_downloader_check_version(OTA_VERSION_URL, &info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "版本检查失败 ret=%d，重启回 BLE", ret);
        vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_AFTER_UPGRADE_MS));
        esp_restart();
    }
    esp_task_wdt_reset();

    ret = ota_downloader_download_and_upgrade(info.url, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "下载升级失败 ret=%d，重启回 BLE", ret);
        vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_AFTER_UPGRADE_MS));
        esp_restart();
    }

    ESP_LOGI(TAG, "升级完成，%dms 后重启", OTA_REBOOT_AFTER_UPGRADE_MS);
    vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_AFTER_UPGRADE_MS));
    esp_restart();
}

/**
 * POST /api/ota/check：异步触发版本检查，立即返回 started/running/error。
 */
static esp_err_t h_api_ota_check(httpd_req_t *req)
{
    if (s_closing) {
        send_json(req, "{\"status\":\"error\",\"error\":\"配网收尾中\"}");
        return ESP_OK;
    }
    arm_deadline();
    ESP_LOGI(TAG, "POST /api/ota/check 异步触发版本检查");

    char ssid[HOME_WIFI_SSID_MAX] = {0};
    char psk[HOME_WIFI_PSK_MAX] = {0};
    if (!nvs_store_load_wifi_cred(ssid, sizeof(ssid), psk, sizeof(psk)) || ssid[0] == '\0') {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "{\"status\":\"error\",\"error\":\"请先在 WiFi 页配置家庭网络\",\"current\":\"%s\"}",
                 FIRMWARE_VERSION);
        send_json(req, buf);
        return ESP_OK;
    }

    if (s_ota_check_state != OTA_CHECK_RUNNING) {
        s_ota_check_state = OTA_CHECK_IDLE;
        s_ota_check_version[0] = '\0';
        s_ota_check_changelog[0] = '\0';
        s_ota_check_error[0] = '\0';
        s_ota_check_has_update = false;
        s_ota_check_size = 0;
    } else {
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "{\"status\":\"running\",\"current\":\"%s\"}", FIRMWARE_VERSION);
        send_json(req, buf);
        return ESP_OK;
    }

    s_ota_check_state = OTA_CHECK_RUNNING;
    BaseType_t ok = xTaskCreate(ota_check_task, "ota_chk", TASK_STACK_OTA_CHECK, NULL, TASK_PRIO_NET, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "异步检查任务创建失败");
        s_ota_check_state = OTA_CHECK_ERROR;
        snprintf(s_ota_check_error, sizeof(s_ota_check_error), "任务创建失败");
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "{\"status\":\"error\",\"error\":\"任务创建失败\",\"current\":\"%s\"}",
                 FIRMWARE_VERSION);
        send_json(req, buf);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "异步检查任务已启动");
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"started\",\"current\":\"%s\"}", FIRMWARE_VERSION);
    send_json(req, buf);
    return ESP_OK;
}

/**
 * GET /api/ota/status：查询版本检查状态，前端轮询。
 */
static esp_err_t h_api_ota_status(httpd_req_t *req)
{
    arm_deadline();

    const char *status_str;
    switch (s_ota_check_state) {
        case OTA_CHECK_RUNNING: status_str = "running"; break;
        case OTA_CHECK_DONE:    status_str = "done";    break;
        case OTA_CHECK_ERROR:   status_str = "error";   break;
        default:                status_str = "idle";    break;
    }

    char ver_esc[64] = {0};
    char log_esc[256] = {0};
    char err_esc[128] = {0};
    json_escape(s_ota_check_version, ver_esc, sizeof(ver_esc));
    json_escape(s_ota_check_changelog, log_esc, sizeof(log_esc));
    json_escape(s_ota_check_error, err_esc, sizeof(err_esc));

    char resp[768];
    if (s_ota_check_state == OTA_CHECK_DONE) {
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"done\",\"has_update\":%s,\"version\":\"%s\",\"size\":%d,\"changelog\":\"%s\",\"current\":\"%s\"}",
                 s_ota_check_has_update ? "true" : "false",
                 ver_esc, s_ota_check_size, log_esc, FIRMWARE_VERSION);
    } else if (s_ota_check_state == OTA_CHECK_ERROR) {
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"error\",\"error\":\"%s\",\"current\":\"%s\"}",
                 err_esc, FIRMWARE_VERSION);
    } else {
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"%s\",\"current\":\"%s\"}", status_str, FIRMWARE_VERSION);
    }
    send_json(req, resp);
    return ESP_OK;
}

/**
 * POST /api/ota/upgrade：触发在线升级。
 * 成功返回 {"ok":true}，前端切到等待重启页面；任务执行约 2 分钟后 esp_restart。
 */
static esp_err_t h_api_ota_upgrade(httpd_req_t *req)
{
    if (s_closing) {
        send_json(req, "{\"ok\":false,\"error\":\"配网收尾中\"}");
        return ESP_OK;
    }
    arm_deadline();
    ESP_LOGI(TAG, "POST /api/ota/upgrade 在线升级请求");

    char ssid[HOME_WIFI_SSID_MAX] = {0};
    char psk[HOME_WIFI_PSK_MAX] = {0};
    if (!nvs_store_load_wifi_cred(ssid, sizeof(ssid), psk, sizeof(psk)) || ssid[0] == '\0') {
        send_json(req, "{\"ok\":false,\"error\":\"尚未配置家庭 WiFi\"}");
        return ESP_OK;
    }

    s_closing = true;
    ESP_LOGI(TAG, "OTA 升级已触发，启动异步任务");

    BaseType_t ok = xTaskCreate(ota_upgrade_task, "ota_up", TASK_STACK_OTA_UPGRADE, NULL, TASK_PRIO_NET, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "OTA 升级任务创建失败");
        s_closing = false;
        send_json(req, "{\"ok\":false,\"error\":\"任务创建失败\"}");
        return ESP_OK;
    }

    send_json(req, "{\"ok\":true}");
    return ESP_OK;
}

// ============================ /api/wifi/* ============================

#define WIFI_SCAN_MAX_APS       10      /* 最多返回前 10 个信号最强的热点 */
#define WIFI_CONNECT_TIMEOUT_MS 15000   /* 测试连接超时 */

typedef enum {
    WIFI_SCAN_IDLE = 0,
    WIFI_SCAN_RUNNING,
    WIFI_SCAN_DONE,
    WIFI_SCAN_ERROR,
} wifi_scan_state_t;

static wifi_scan_state_t s_wifi_scan_state = WIFI_SCAN_IDLE;
static wifi_ap_record_t s_scan_aps[WIFI_SCAN_MAX_APS];  /* 静态预分配，避免任务内 malloc */
static int s_scan_count = 0;
static char s_scan_error[64] = {0};

typedef enum {
    WIFI_CONN_IDLE = 0,
    WIFI_CONN_RUNNING,
    WIFI_CONN_DONE,
    WIFI_CONN_ERROR,
} wifi_conn_state_t;

static wifi_conn_state_t s_wifi_conn_state = WIFI_CONN_IDLE;
static char s_wifi_conn_error[64] = {0};
/* 测试连接成功后保存的 SSID/IP（连接测试任务填充，h_api_wifi_connect_status 读取） */
static char s_wifi_conn_ssid[HOME_WIFI_SSID_MAX] = {0};

static void wifi_scan_task(void *arg)
{
    (void)arg;
    int count = 0;
    esp_err_t ret = radio_ctrl_wifi_scan(s_scan_aps, WIFI_SCAN_MAX_APS, &count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 扫描失败 ret=%d", ret);
        snprintf(s_scan_error, sizeof(s_scan_error), "扫描失败");
        s_wifi_scan_state = WIFI_SCAN_ERROR;
        vTaskDelete(NULL);
        return;
    }
    s_scan_count = count;
    s_wifi_scan_state = WIFI_SCAN_DONE;
    ESP_LOGI(TAG, "异步扫描完成，发现 %d 个热点", count);
    vTaskDelete(NULL);
}

static void wifi_connect_task(void *arg)
{
    (void)arg;
    char ssid[HOME_WIFI_SSID_MAX] = {0};
    char psk[HOME_WIFI_PSK_MAX] = {0};
    if (!nvs_store_load_wifi_cred(ssid, sizeof(ssid), psk, sizeof(psk))) {
        snprintf(s_wifi_conn_error, sizeof(s_wifi_conn_error), "无凭证");
        s_wifi_conn_state = WIFI_CONN_ERROR;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "异步测试连接：%.32s", ssid);
    esp_err_t ret = radio_ctrl_sta_connect(ssid, psk, WIFI_CONNECT_TIMEOUT_MS);
    if (ret == ESP_OK) {
        /* 保存 SSID 供 /api/wifi/connect/status 返回展示 */
        strncpy(s_wifi_conn_ssid, ssid, sizeof(s_wifi_conn_ssid) - 1);
        radio_ctrl_sta_disconnect();
        s_wifi_conn_state = WIFI_CONN_DONE;
        ESP_LOGI(TAG, "异步测试连接成功");
    } else {
        snprintf(s_wifi_conn_error, sizeof(s_wifi_conn_error), "连接失败");
        s_wifi_conn_state = WIFI_CONN_ERROR;
        ESP_LOGE(TAG, "异步测试连接失败 ret=%d", ret);
    }
    vTaskDelete(NULL);
}

static esp_err_t h_api_wifi_scan(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/wifi/scan state=%d closing=%d", s_wifi_scan_state, s_closing ? 1 : 0);
    if (s_closing) {
        send_json(req, "{\"status\":\"error\",\"error\":\"配网收尾中\"}");
        return ESP_OK;
    }
    arm_deadline();

    if (s_wifi_scan_state == WIFI_SCAN_RUNNING) {
        ESP_LOGI(TAG, "POST /api/wifi/scan 扫描已在进行，返回 running");
        send_json(req, "{\"status\":\"running\"}");
        return ESP_OK;
    }
    s_wifi_scan_state = WIFI_SCAN_IDLE;
    s_scan_count = 0;
    s_scan_error[0] = '\0';

    s_wifi_scan_state = WIFI_SCAN_RUNNING;
    BaseType_t ok = xTaskCreate(wifi_scan_task, "wifi_scan", TASK_STACK_OTA_CHECK, NULL, TASK_PRIO_NET, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "扫描任务创建失败");
        s_wifi_scan_state = WIFI_SCAN_ERROR;
        snprintf(s_scan_error, sizeof(s_scan_error), "任务创建失败");
        send_json(req, "{\"status\":\"error\",\"error\":\"任务创建失败\"}");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "POST /api/wifi/scan 异步扫描已启动");
    send_json(req, "{\"status\":\"started\"}");
    return ESP_OK;
}

static esp_err_t h_api_wifi_scan_status(httpd_req_t *req)
{
    arm_deadline();

    if (s_wifi_scan_state == WIFI_SCAN_RUNNING) {
        send_json(req, "{\"status\":\"running\"}");
        return ESP_OK;
    }
    if (s_wifi_scan_state == WIFI_SCAN_ERROR) {
        ESP_LOGW(TAG, "GET /api/wifi/scan/status 返回 error: %s", s_scan_error);
        char buf[160];
        snprintf(buf, sizeof(buf), "{\"status\":\"error\",\"error\":\"%s\"}", s_scan_error);
        send_json(req, buf);
        return ESP_OK;
    }
    if (s_wifi_scan_state != WIFI_SCAN_DONE) {
        send_json(req, "{\"status\":\"idle\"}");
        return ESP_OK;
    }

    /* 拼接扫描结果，按 RSSI 降序（radio_ctrl 已保证） */
    char buf[2048];
    int off = snprintf(buf, sizeof(buf),
        "{\"status\":\"done\",\"count\":%d,\"networks\":[", s_scan_count);
    for (int i = 0; i < s_scan_count && off < (int)sizeof(buf) - 96; i++) {
        char ssid_esc[HOME_WIFI_SSID_MAX * 2 + 1] = {0};
        json_escape((const char *)s_scan_aps[i].ssid, ssid_esc, sizeof(ssid_esc));
        off += snprintf(buf + off, sizeof(buf) - off,
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%d}",
            i == 0 ? "" : ",", ssid_esc,
            s_scan_aps[i].rssi, s_scan_aps[i].authmode);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "]}");
    ESP_LOGI(TAG, "GET /api/wifi/scan/status 返回 done count=%d", s_scan_count);
    send_json(req, buf);
    return ESP_OK;
}

static esp_err_t h_api_wifi_save(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/wifi/save closing=%d", s_closing ? 1 : 0);
    if (s_closing) {
        send_json(req, "{\"status\":\"error\",\"error\":\"配网收尾中\"}");
        return ESP_OK;
    }
    arm_deadline();

    char *body = NULL;
    int len = recv_body(req, &body);
    if (len < 0) {
        send_json(req, "{\"status\":\"error\",\"error\":\"读取请求体失败\"}");
        return ESP_FAIL;
    }

    char ssid[HOME_WIFI_SSID_MAX] = {0};
    char psk[HOME_WIFI_PSK_MAX] = {0};
    json_get_str(body, "ssid", ssid, sizeof(ssid));
    json_get_str(body, "psk", psk, sizeof(psk));
    free(body);

    if (ssid[0] == '\0') {
        send_json(req, "{\"status\":\"error\",\"error\":\"请输入 WiFi 名称\"}");
        return ESP_OK;
    }
    nvs_store_save_wifi_cred(ssid, psk);
    ESP_LOGI(TAG, "POST /api/wifi/save 凭证已保存 ssid=%.32s，启动测试连接", ssid);

    if (s_wifi_conn_state == WIFI_CONN_RUNNING) {
        send_json(req, "{\"status\":\"running\"}");
        return ESP_OK;
    }
    s_wifi_conn_state = WIFI_CONN_IDLE;
    s_wifi_conn_error[0] = '\0';
    s_wifi_conn_ssid[0] = '\0';
    s_wifi_conn_state = WIFI_CONN_RUNNING;
    BaseType_t ok = xTaskCreate(wifi_connect_task, "wifi_conn", TASK_STACK_OTA_CHECK, NULL, TASK_PRIO_NET, NULL);
    if (ok != pdPASS) {
        s_wifi_conn_state = WIFI_CONN_ERROR;
        snprintf(s_wifi_conn_error, sizeof(s_wifi_conn_error), "任务创建失败");
        send_json(req, "{\"status\":\"error\",\"error\":\"任务创建失败\"}");
        return ESP_OK;
    }
    send_json(req, "{\"status\":\"started\"}");
    return ESP_OK;
}

static esp_err_t h_api_wifi_connect_status(httpd_req_t *req)
{
    arm_deadline();

    if (s_wifi_conn_state == WIFI_CONN_RUNNING) {
        send_json(req, "{\"status\":\"running\"}");
        return ESP_OK;
    }
    if (s_wifi_conn_state == WIFI_CONN_DONE) {
        char ssid_esc[HOME_WIFI_SSID_MAX * 2 + 1] = {0};
        json_escape(s_wifi_conn_ssid, ssid_esc, sizeof(ssid_esc));
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "{\"status\":\"done\",\"connected\":true,\"ssid\":\"%s\"}",
                 ssid_esc);
        send_json(req, buf);
        return ESP_OK;
    }
    if (s_wifi_conn_state == WIFI_CONN_ERROR) {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "{\"status\":\"error\",\"connected\":false,\"error\":\"%s\"}",
                 s_wifi_conn_error);
        send_json(req, buf);
        return ESP_OK;
    }
    send_json(req, "{\"status\":\"idle\"}");
    return ESP_OK;
}

// ============================ favicon ============================

static esp_err_t h_favicon(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ============================ 启动 ============================

static esp_err_t start_httpd(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = WIFI_PORT;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = HTTP_MAX_URI_HANDLERS;
    cfg.stack_size = TASK_STACK_HTTPD;
    cfg.max_open_sockets = 7;
    cfg.keep_alive_enable = false;

    esp_err_t ret = httpd_start(&s_httpd, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd 启动失败 ret=%d", ret);
        return ret;
    }
    httpd_uri_t routes[] = {
        /* 静态首页 */
        { .uri = "/",                .method = HTTP_GET,  .handler = h_home },
        { .uri = "/favicon.ico",     .method = HTTP_GET,  .handler = h_favicon },
        /* 状态与计时 */
        { .uri = "/api/state",       .method = HTTP_GET,  .handler = h_api_state },
        { .uri = "/api/time",        .method = HTTP_GET,  .handler = h_api_time },
        { .uri = "/api/renew",       .method = HTTP_POST, .handler = h_api_renew },
        { .uri = "/api/mem_stats",   .method = HTTP_GET,  .handler = h_api_mem_stats },
        /* 配置 */
        { .uri = "/api/config",      .method = HTTP_POST, .handler = h_api_config },
        { .uri = "/api/draft",       .method = HTTP_POST, .handler = h_api_draft },
        { .uri = "/api/close-wifi",  .method = HTTP_POST, .handler = h_api_close_wifi },
        /* OTA */
        { .uri = "/api/ota/check",   .method = HTTP_POST, .handler = h_api_ota_check },
        { .uri = "/api/ota/status",  .method = HTTP_GET,  .handler = h_api_ota_status },
        { .uri = "/api/ota/upgrade", .method = HTTP_POST, .handler = h_api_ota_upgrade },
        /* WiFi 扫描与连接测试 */
        { .uri = "/api/wifi/scan",           .method = HTTP_POST, .handler = h_api_wifi_scan },
        { .uri = "/api/wifi/scan/status",    .method = HTTP_GET,  .handler = h_api_wifi_scan_status },
        { .uri = "/api/wifi/save",           .method = HTTP_POST, .handler = h_api_wifi_save },
        { .uri = "/api/wifi/connect/status", .method = HTTP_GET,  .handler = h_api_wifi_connect_status },
        /* Captive Portal 探测端点：各 OS 连通性检测，统一 302 重定向到配网首页触发自动弹窗 */
        { .uri = "/generate_204",               .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/gen_204",                    .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/hotspot-detect.html",        .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/library/test/success.html",  .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/ncsi.txt",                   .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/connecttest.txt",            .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/canonical.html",             .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/success.txt",                .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/redirect",                   .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/kindle-wifi/wifistub.html",  .method = HTTP_GET, .handler = captive_portal_detect_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ret = httpd_register_uri_handler(s_httpd, &routes[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "路由注册失败 uri=%s ret=%d", routes[i].uri, ret);
            httpd_stop(s_httpd);
            s_httpd = NULL;
            return ret;
        }
    }
    ESP_LOGI(TAG, "HTTP 配网服务就绪 端口=%d 路由数=%zu", WIFI_PORT,
             sizeof(routes) / sizeof(routes[0]));
    return ESP_OK;
}

esp_err_t wifi_portal_start(power_engine_t *engine)
{
    s_engine = engine;
    s_closing = false;
    s_done_called = false;
    ESP_LOGI(TAG, "配网启动前 堆=%lu", (unsigned long)esp_get_free_heap_size());

    esp_err_t ret = start_httpd();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 启动失败，主动触发 done_cb 让状态机回 BLE");
        portal_done();
        return ret;
    }
    arm_deadline();

    const esp_timer_create_args_t targs = { .callback = shutdown_check_cb, .name = "portal_to" };
    ret = esp_timer_create(&targs, &s_shutdown_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create 失败 ret=%d", ret);
        httpd_stop(s_httpd);
        s_httpd = NULL;
        portal_done();
        return ret;
    }
    ret = esp_timer_start_periodic(s_shutdown_timer, WIFI_PORTAL_CHECK_INTERVAL_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic 失败 ret=%d", ret);
        esp_timer_delete(s_shutdown_timer);
        s_shutdown_timer = NULL;
        httpd_stop(s_httpd);
        s_httpd = NULL;
        portal_done();
        return ret;
    }

    ESP_LOGI(TAG, "WiFi 配网模式就绪（BLE 共存），%ds 后自动超时", WIFI_SHUTDOWN_MS / 1000);
    return ESP_OK;
}

void wifi_portal_stop(void)
{
    if (s_shutdown_timer) {
        esp_timer_stop(s_shutdown_timer);
        esp_timer_delete(s_shutdown_timer);
        s_shutdown_timer = NULL;
    }
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    s_closing = false;
    s_done_called = false;
    ESP_LOGI(TAG, "HTTP 配网门户已停止 堆=%lu", (unsigned long)esp_get_free_heap_size());
}

void wifi_portal_set_done_callback(void (*cb)(void))
{
    s_done_cb = cb;
}
