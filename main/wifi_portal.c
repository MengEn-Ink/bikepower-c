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
 *   POST /api/close-wifi            保存配置 + 5s 后关 WiFi 回到 BLE
 *   GET  /api/time                  剩余秒数（纯文本，高频轮询优化）
 *   POST /api/renew                 续期（纯文本返回剩余秒数）
 *   GET  /api/mem_stats             内存统计 JSON
 *   Captive Portal 探测端点 × 10（iOS/Android/Windows/Firefox/Kindle）
 *
 * 超时：180s 无续期自动关闭 WiFi 回到 BLE（不重启）；配置通过 power_engine setter 实时生效到 BLE 通知。
 *
 * WiFi SoftAP 启动已收回 radio_ctrl.c 统一管理，本文件只负责 HTTP 路由和 JSON API。
 * 调用方（radio_ctrl）需在调用 wifi_portal_start 前确保 AP 已就绪。
 */
#include "wifi_portal.h"
#include "web_index_html.h"   /* scripts/inline_static.py 自动生成 */
#include "captive_portal.h"
#include "board_detect.h"
#include "radio_ctrl.h"
#include "nvs_store.h"
#include "device_template_store.h"
#include "ble_bikepower.h"
#include "ble_scanner.h"
#include "app_state.h"
#include "bikepower_config.h"
#include "profile.h"
#include "json_miniparse.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
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
static volatile bool s_ble_restart_requested = false;
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

bool wifi_portal_consume_ble_restart_requested(void)
{
    bool requested = s_ble_restart_requested;
    s_ble_restart_requested = false;
    return requested;
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

static int sport_from_id(const char *id)
{
    if (strcmp(id, "running") == 0) return SPORT_PROFILE_RUNNING;
    if (strcmp(id, "cycling") == 0) return SPORT_PROFILE_CYCLING;
    return -1;
}

static const char *sport_to_id(int sport)
{
    if (sport == SPORT_PROFILE_RUNNING) return "running";
    return "cycling";
}

// ============================ 配置应用 ============================

/**
 * 把 JSON 请求体的字段统一应用到功率引擎。
 * 解析 mode/device_name/手动三项/profile 四项并写入引擎，供 /api/config 与 /api/draft 共用。
 * 未传字段保留旧值（与旧 form 实现一致）。
 * body：JSON 请求体（已 \0 终止）。
 * e：目标引擎实例。
 */
static bool apply_json_fields(power_engine_t *e, const char *body, bool allow_sport_switch)
{
    char val[64];
    int new_val, old_val;
    bool sport_changed = false;

    if (json_get_str(body, "mode", val, sizeof(val))) {
        int m = mode_from_id(val);
        if (m >= 0) {
            ride_mode_t old_mode = e->mode;
            power_engine_set_mode(e, (ride_mode_t)m);
            if (old_mode != e->mode)
                ESP_LOGI(TAG, "  mode: %s → %s", mode_to_id((int)old_mode), mode_to_id(m));
        }
    }
    if (json_get_str(body, "device_name", val, sizeof(val))) {
        ESP_LOGI(TAG, "  device_name: %s → %s", e->device_name, val);
        power_engine_set_device_name(e, val);
    }
    if (allow_sport_switch && json_get_str(body, "sport_profile", val, sizeof(val))) {
        int sport = sport_from_id(val);
        if (sport >= 0) {
            sport_profile_t old_sport = e->sport_profile;
            power_engine_set_sport_profile(e, (sport_profile_t)sport);
            if (old_sport != e->sport_profile) {
                sport_changed = true;
                ESP_LOGI(TAG, "  sport: %s → %s", sport_to_id((int)old_sport), sport_to_id(sport));
            }
        }
    }
    if (json_has_key(body, "broadcast_mask")) {
        old_val = e->broadcast_mask;
        new_val = json_get_int_def(body, "broadcast_mask", old_val);
        power_engine_set_broadcast_mask(e, new_val);
        if (old_val != e->broadcast_mask)
            ESP_LOGI(TAG, "  broadcast_mask: %d → %d", old_val, e->broadcast_mask);
    }
    if (json_get_str(body, "custom_pacer_name", val, sizeof(val))) {
        ESP_LOGI(TAG, "  custom_pacer_name: %s → %s", e->profile.custom_pacer_name, val);
        profile_set_custom_name(&e->profile, val);
    }
    /* 手动三项：setter 内部仅 STEADY 生效，非 STEADY 自动拒绝 */
    old_val = e->base_power;
    new_val = json_get_int_def(body, "power", old_val);
    power_engine_set_power(e, new_val);
    if (new_val != old_val && e->base_power == new_val)
        ESP_LOGI(TAG, "  power: %dW → %dW", old_val, new_val);

    old_val = e->cadence_base;
    new_val = json_get_int_def(body, "cadence", old_val);
    power_engine_set_cadence(e, new_val);
    if (new_val != old_val && e->cadence_base == new_val)
        ESP_LOGI(TAG, "  cadence: %dRPM → %dRPM", old_val, new_val);

    /* 浮动幅度三项：任意模式接受设置（仅固定模式生效），setter 内部夹紧范围 */
    old_val = e->power_float;
    new_val = json_get_int_def(body, "power_float", old_val);
    power_engine_set_power_float(e, new_val);
    if (old_val != e->power_float)
        ESP_LOGI(TAG, "  power_float: %d → %d", old_val, e->power_float);

    old_val = e->cadence_float;
    new_val = json_get_int_def(body, "cadence_float", old_val);
    power_engine_set_cadence_float(e, new_val);
    if (old_val != e->cadence_float)
        ESP_LOGI(TAG, "  cadence_float: %d → %d", old_val, e->cadence_float);

    old_val = e->hr_float;
    new_val = json_get_int_def(body, "hr_float", old_val);
    power_engine_set_hr_float(e, new_val);
    if (old_val != e->hr_float)
        ESP_LOGI(TAG, "  hr_float: %d → %d", old_val, e->hr_float);

    /* 调功步进：任意模式接受设置，setter 内部夹紧到 [POWER_STEP_MIN, POWER_STEP_MAX] */
    old_val = e->power_step;
    new_val = json_get_int_def(body, "power_step", old_val);
    power_engine_set_power_step(e, new_val);
    if (old_val != e->power_step)
        ESP_LOGI(TAG, "  power_step: %d → %d", old_val, e->power_step);

    old_val = e->running.speed_x100_kmh;
    new_val = json_get_int_def(body, "run_speed_x100", old_val);
    power_engine_set_run_speed(e, new_val);
    if (old_val != e->running.speed_x100_kmh)
        ESP_LOGI(TAG, "  run_speed_x100: %d → %d", old_val, e->running.speed_x100_kmh);

    old_val = e->running.pace_float_sec;
    new_val = json_get_int_def(body, "run_pace_float_sec", old_val);
    power_engine_set_run_pace_float(e, new_val);
    if (old_val != e->running.pace_float_sec)
        ESP_LOGI(TAG, "  run_pace_float_sec: %d → %d", old_val, e->running.pace_float_sec);

    old_val = e->running.cadence_spm;
    new_val = json_get_int_def(body, "run_cadence", old_val);
    power_engine_set_run_cadence(e, new_val);
    if (old_val != e->running.cadence_spm)
        ESP_LOGI(TAG, "  run_cadence: %d → %d", old_val, e->running.cadence_spm);

    old_val = e->running.cadence_float_spm;
    new_val = json_get_int_def(body, "run_cadence_float", old_val);
    power_engine_set_run_cadence_float(e, new_val);
    if (old_val != e->running.cadence_float_spm)
        ESP_LOGI(TAG, "  run_cadence_float: %d → %d", old_val, e->running.cadence_float_spm);

    old_val = e->running.incline_x10;
    new_val = json_get_int_def(body, "run_incline_x10", old_val);
    power_engine_set_run_incline(e, new_val);
    if (old_val != e->running.incline_x10)
        ESP_LOGI(TAG, "  run_incline_x10: %d → %d", old_val, e->running.incline_x10);

    /* 心率锁定：hr_lock=1 锁定到 heartrate 值，hr_lock=0 解锁随功率联动 */
    if (json_has_key(body, "hr_lock")) {
        if (json_get_int(body, "hr_lock")) {
            old_val = e->has_user_hr ? e->user_heartrate : 0;
            new_val = json_get_int_def(body, "heartrate", e->last_heartrate);
            power_engine_set_heartrate(e, new_val);
            ESP_LOGI(TAG, "  hr_lock: ON heartrate=%dBPM (was %d)", new_val, old_val);
        } else {
            power_engine_clear_user_hr(e);
            ESP_LOGI(TAG, "  hr_lock: OFF");
        }
    } else if (json_has_key(body, "heartrate")) {
        new_val = json_get_int_def(body, "heartrate", e->last_heartrate);
        power_engine_set_heartrate(e, new_val);
        ESP_LOGI(TAG, "  heartrate: %dBPM", new_val);
    }

    old_val = e->profile.weight;
    new_val = json_get_int_def(body, "profile_weight", old_val);
    profile_set_weight(&e->profile, new_val);
    if (old_val != e->profile.weight)
        ESP_LOGI(TAG, "  weight: %d → %d kg", old_val, e->profile.weight);

    old_val = e->profile.ftp;
    new_val = json_get_int_def(body, "profile_ftp", old_val);
    profile_set_ftp(&e->profile, new_val);
    if (old_val != e->profile.ftp)
        ESP_LOGI(TAG, "  ftp: %d → %dW", old_val, e->profile.ftp);

    old_val = e->profile.thr_hr;
    new_val = json_get_int_def(body, "profile_thr_hr", old_val);
    profile_set_thr_hr(&e->profile, new_val);
    if (old_val != e->profile.thr_hr)
        ESP_LOGI(TAG, "  thr_hr: %d → %dBPM", old_val, e->profile.thr_hr);

    /* 自定义档 W/kg 范围：先于 ride_type 设置，保证切到自定义档时范围已就绪 */
    if (json_has_key(body, "custom_wkg_low") || json_has_key(body, "custom_wkg_high")) {
        int cl = json_get_int_def(body, "custom_wkg_low", e->profile.custom_wkg_low);
        int ch = json_get_int_def(body, "custom_wkg_high", e->profile.custom_wkg_high);
        profile_set_custom_wkg(&e->profile, cl, ch);
        ESP_LOGI(TAG, "  custom_wkg: %d.%d~%d.%d",
                 e->profile.custom_wkg_low / 10, e->profile.custom_wkg_low % 10,
                 e->profile.custom_wkg_high / 10, e->profile.custom_wkg_high % 10);
    }
    if (json_get_str(body, "profile_ride_type", val, sizeof(val)) && val[0]) {
        int old_rt = e->profile.ride_type;
        profile_set_ride_type_str(&e->profile, val);
        if (old_rt != e->profile.ride_type)
            ESP_LOGI(TAG, "  ride_type: %d → %d", old_rt, e->profile.ride_type);
    }

    if (json_has_key(body, "led_silent")) {
        e->led.silent = (uint8_t)json_get_int(body, "led_silent");
        ESP_LOGI(TAG, "  led_silent: %d", e->led.silent);
    }
#define APPLY_LED_SCENE(prefix, scene_id) do { \
        if (json_has_key(body, prefix "_mode")) { \
            e->led.scenes[(scene_id)].mode = (uint8_t)json_get_int(body, prefix "_mode"); \
        } \
        if (json_has_key(body, prefix "_color")) { \
            e->led.scenes[(scene_id)].color = (uint8_t)json_get_int(body, prefix "_color"); \
        } \
    } while (0)
    APPLY_LED_SCENE("led_wait", LED_SCENE_WAIT_CONN);
    APPLY_LED_SCENE("led_conn", LED_SCENE_CONNECTED);
    APPLY_LED_SCENE("led_wifi", LED_SCENE_WIFI_CONFIG);
    APPLY_LED_SCENE("led_confirm", LED_SCENE_WIFI_CONFIRM);
    APPLY_LED_SCENE("led_error", LED_SCENE_ERROR);
#undef APPLY_LED_SCENE
    led_config_validate(&e->led);
    return sport_changed;
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

static esp_err_t h_captive_portal_api(httpd_req_t *req)
{
    const char body[] = "{\"captive\":true,\"user-portal-url\":\"http://" CAPTIVE_PORTAL_HOST "/\"}";
    ESP_LOGI(TAG, "Captive Portal API 请求 uri=%s", req && req->uri ? req->uri : "?");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "application/captive+json");
    set_no_store_headers(req);
    httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ============================ /api/state ============================

/**
 * 整合状态 JSON：返回首页/更多/关于视图所需的全部数据。
 * 前端 init() 一次性拉取后缓存到 STATE，后续按视图渲染。
 */
static esp_err_t h_api_state(httpd_req_t *req)
{
    arm_deadline();
    power_engine_t *e = s_engine;

    char dn_esc[DEVICE_NAME_MAX * 2 + 1] = {0};
    json_escape(e->device_name, dn_esc, sizeof(dn_esc));
    char custom_name_esc[CUSTOM_PACER_NAME_MAX * 2 + 1] = {0};
    json_escape(e->profile.custom_pacer_name, custom_name_esc, sizeof(custom_name_esc));
    char fw_name[96];
    char fw_name_esc[192] = {0};
    board_firmware_name(fw_name, sizeof(fw_name));
    json_escape(fw_name, fw_name_esc, sizeof(fw_name_esc));

    int ride_type_idx = (e->profile.ride_type >= 0 && e->profile.ride_type < RIDE_TYPE_COUNT)
                        ? e->profile.ride_type : 0;

    char buf[2432];
    int off = snprintf(buf, sizeof(buf),
        "{\"device_name\":\"%s\",\"firmware_version\":\"%s\",\"firmware_name\":\"%s\","
        "\"board\":\"%s\",\"stable_version\":\"%s\",\"fix_version\":\"%s\",\"git_sha\":\"%s\","
        "\"sport_profile\":\"%s\",\"broadcast_mask\":%d,"
        "\"power\":%d,\"cadence\":%d,\"heartrate\":%d,\"mode\":\"%s\","
        "\"run_speed_x100\":%d,\"run_pace_float_sec\":%d,"
        "\"run_cadence\":%d,\"run_cadence_float\":%d,\"run_incline_x10\":%d,"
        "\"power_float\":%d,\"cadence_float\":%d,\"hr_float\":%d,"
        "\"power_step\":%d,"
        "\"profile_weight\":%d,\"profile_ftp\":%d,\"profile_thr_hr\":%d,"
        "\"profile_ride_type\":\"%s\","
        "\"custom_wkg_low\":%d,\"custom_wkg_high\":%d,\"custom_pacer_name\":\"%s\","
        "\"led_silent\":%d,"
        "\"led_wait_mode\":%d,\"led_wait_color\":%d,"
        "\"led_conn_mode\":%d,\"led_conn_color\":%d,"
        "\"led_wifi_mode\":%d,\"led_wifi_color\":%d,"
        "\"led_confirm_mode\":%d,\"led_confirm_color\":%d,"
        "\"led_error_mode\":%d,\"led_error_color\":%d,"
        "\"remaining_sec\":%d,\"closing\":%s,"
        "\"hr_locked\":%s,"
        "\"pacers\":[",
        dn_esc, FIRMWARE_VERSION, fw_name_esc,
        board_get_desc()->name, FIRMWARE_STABLE_VERSION, FIRMWARE_FIX_VERSION, BIKEPOWER_GIT_SHA,
        sport_to_id((int)e->sport_profile), e->broadcast_mask,
        e->base_power, e->cadence_base, e->last_heartrate, mode_to_id((int)e->mode),
        e->running.speed_x100_kmh, e->running.pace_float_sec,
        e->running.cadence_spm, e->running.cadence_float_spm, e->running.incline_x10,
        e->power_float, e->cadence_float, e->hr_float,
        e->power_step,
        e->profile.weight, e->profile.ftp, e->profile.thr_hr,
        PROFILE_RIDE_TYPE_OPTIONS[ride_type_idx],
        e->profile.custom_wkg_low, e->profile.custom_wkg_high, custom_name_esc,
        e->led.silent,
        e->led.scenes[LED_SCENE_WAIT_CONN].mode, e->led.scenes[LED_SCENE_WAIT_CONN].color,
        e->led.scenes[LED_SCENE_CONNECTED].mode, e->led.scenes[LED_SCENE_CONNECTED].color,
        e->led.scenes[LED_SCENE_WIFI_CONFIG].mode, e->led.scenes[LED_SCENE_WIFI_CONFIG].color,
        e->led.scenes[LED_SCENE_WIFI_CONFIRM].mode, e->led.scenes[LED_SCENE_WIFI_CONFIRM].color,
        e->led.scenes[LED_SCENE_ERROR].mode, e->led.scenes[LED_SCENE_ERROR].color,
        remaining_seconds(), s_closing ? "true" : "false",
        e->has_user_hr ? "true" : "false");

    for (int i = 0; i < PACER_COUNT && off < (int)sizeof(buf) - 96; i++) {
        off += snprintf(buf + off, sizeof(buf) - off,
            "%s{\"id\":\"%s\",\"name\":\"%s\",\"wkg_low\":%d.%d,\"wkg_high\":%d.%d}",
            i == 0 ? "" : ",",
            PROFILE_RIDE_TYPE_OPTIONS[i], ROAD_PACERS[i].name,
            ROAD_PACERS[i].wkg_low / 10, ROAD_PACERS[i].wkg_low % 10,
            ROAD_PACERS[i].wkg_high / 10, ROAD_PACERS[i].wkg_high % 10);
    }
    /* 追加自定义档（id=custom），昵称和 W/kg 范围取自 profile，供前端下拉与编辑 */
    if (off < (int)sizeof(buf) - 96) {
        off += snprintf(buf + off, sizeof(buf) - off,
            ",{\"id\":\"%s\",\"name\":\"%s\",\"wkg_low\":%d.%d,\"wkg_high\":%d.%d,\"custom\":true}",
            PROFILE_RIDE_TYPE_OPTIONS[PACER_INDEX_CUSTOM],
            custom_name_esc,
            e->profile.custom_wkg_low / 10, e->profile.custom_wkg_low % 10,
            e->profile.custom_wkg_high / 10, e->profile.custom_wkg_high % 10);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "]}");

    send_json(req, buf);
    return ESP_OK;
}

// ============================ /api/config /api/close-wifi ============================

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
    apply_json_fields(&next, body, false);
    free(body);

    if (nvs_store_save(&next)) {
        bool name_changed = strcmp(s_engine->device_name, next.device_name) != 0;
        memcpy(s_engine, &next, sizeof(power_engine_t));
        power_engine_clear_dirty(s_engine);
        if (name_changed) ble_bikepower_refresh_device_name();
        app_state_refresh_led();
        ESP_LOGI(TAG, "配置已保存 sport=%d bmask=%d p=%dW c=%dRPM mode=%d name=%s p_step=%d p_float=%d c_float=%d hr_float=%d ride=%d run=%d/%d/%d/%d",
                 (int)s_engine->sport_profile,
                 s_engine->broadcast_mask,
                 s_engine->base_power, s_engine->cadence_base,
                 (int)s_engine->mode, s_engine->device_name,
                 s_engine->power_step,
                 s_engine->power_float, s_engine->cadence_float, s_engine->hr_float,
                 s_engine->profile.ride_type,
                 s_engine->running.speed_x100_kmh, s_engine->running.cadence_spm,
                 s_engine->running.cadence_float_spm, s_engine->running.incline_x10);
        send_json(req, "{\"ok\":true}");
    } else {
        ESP_LOGE(TAG, "配置保存失败，s_engine 保持原值");
        arm_deadline();
        send_json(req, "{\"ok\":false,\"error\":\"NVS 保存失败\"}");
    }
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
    bool sport_changed = apply_json_fields(&next, body, true);
    free(body);

    if (nvs_store_save(&next)) {
        bool name_changed = strcmp(s_engine->device_name, next.device_name) != 0;
        memcpy(s_engine, &next, sizeof(power_engine_t));
        power_engine_clear_dirty(s_engine);
        if (sport_changed) {
            s_ble_restart_requested = true;
        }
        if (name_changed) ble_bikepower_refresh_device_name();
        app_state_refresh_led();
        ESP_LOGI(TAG, "配置已保存并准备关闭 WiFi sport=%d bmask=%d p=%dW c=%dRPM mode=%d name=%s p_step=%d p_float=%d c_float=%d hr_float=%d ride=%d run=%d/%d/%d/%d",
                 (int)s_engine->sport_profile,
                 s_engine->broadcast_mask,
                 s_engine->base_power, s_engine->cadence_base,
                 (int)s_engine->mode, s_engine->device_name,
                 s_engine->power_step,
                 s_engine->power_float, s_engine->cadence_float, s_engine->hr_float,
                 s_engine->profile.ride_type,
                 s_engine->running.speed_x100_kmh, s_engine->running.cadence_spm,
                 s_engine->running.cadence_float_spm, s_engine->running.incline_x10);
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
    ESP_LOGI(TAG, "配网窗口已续期 剩余%d秒", remaining_seconds());
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

// ============================ /api/device-templates ============================

static esp_err_t h_api_device_templates(httpd_req_t *req)
{
    arm_deadline();
    device_template_list_t list;
    device_template_store_load(&list);
    char buf[2048];
    if (device_template_list_serialize_json(&list, buf, sizeof(buf)) == 0) {
        send_json(req, "{\"max\":5,\"count\":0,\"items\":[]}");
        return ESP_OK;
    }
    send_json(req, buf);
    return ESP_OK;
}

static void json_get_str_def(const char *json, const char *key, char *out, size_t out_sz)
{
    if (out_sz == 0) return;
    if (!json_get_str(json, key, out, out_sz)) out[0] = '\0';
}

static esp_err_t h_api_device_template_save(httpd_req_t *req)
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
    device_template_t tpl;
    device_template_defaults(&tpl);
    tpl.used = true;
    json_get_str_def(body, "alias", tpl.alias, sizeof(tpl.alias));
    json_get_str_def(body, "adv_name", tpl.adv_name, sizeof(tpl.adv_name));
    json_get_str_def(body, "device_name", tpl.device_name, sizeof(tpl.device_name));
    json_get_str_def(body, "manufacturer", tpl.manufacturer, sizeof(tpl.manufacturer));
    json_get_str_def(body, "model", tpl.model, sizeof(tpl.model));
    json_get_str_def(body, "firmware", tpl.firmware, sizeof(tpl.firmware));
    json_get_str_def(body, "hardware", tpl.hardware, sizeof(tpl.hardware));
    json_get_str_def(body, "software", tpl.software, sizeof(tpl.software));
    json_get_str_def(body, "addr", tpl.addr, sizeof(tpl.addr));
    tpl.addr_type = (uint8_t)json_get_int_def(body, "addr_type", 0);
    tpl.last_rssi = (int8_t)json_get_int_def(body, "last_rssi", 0);
    tpl.service_flags = (uint16_t)json_get_int_def(body, "service_flags", 0);
    free(body);
    if (tpl.alias[0] == '\0' && tpl.device_name[0] == '\0' && tpl.adv_name[0] == '\0') {
        send_json(req, "{\"ok\":false,\"error\":\"模板名称不能为空\"}");
        return ESP_OK;
    }
    if (tpl.device_name[0] == '\0' && tpl.adv_name[0] != '\0') {
        strncpy(tpl.device_name, tpl.adv_name, sizeof(tpl.device_name) - 1);
        tpl.device_name[sizeof(tpl.device_name) - 1] = '\0';
    }
    if (tpl.alias[0] == '\0') {
        strncpy(tpl.alias, tpl.device_name[0] ? tpl.device_name : tpl.adv_name, sizeof(tpl.alias) - 1);
        tpl.alias[sizeof(tpl.alias) - 1] = '\0';
    }
    int slot = -1;
    if (!device_template_store_add(&tpl, &slot)) {
        send_json(req, "{\"ok\":false,\"error\":\"模板已满，请先删除旧模板\"}");
        return ESP_OK;
    }
    char resp[48];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"id\":%d}", slot);
    send_json(req, resp);
    return ESP_OK;
}

static esp_err_t h_api_device_template_delete(httpd_req_t *req)
{
    if (s_closing) {
        send_json(req, "{\"ok\":false,\"error\":\"配网收尾中\"}");
        return ESP_OK;
    }
    arm_deadline();
    char *body = NULL;
    int len = recv_body(req, &body);
    if (len < 0) {
        send_json(req, "{\"ok\":false,\"error\":\"读取请求体失败\"}");
        return ESP_OK;
    }
    int id = json_get_int_def(body, "id", -1);
    free(body);
    if (id < 0 || id >= DEVICE_TEMPLATE_MAX || !device_template_store_delete((uint8_t)id)) {
        send_json(req, "{\"ok\":false,\"error\":\"模板不存在\"}");
        return ESP_OK;
    }
    send_json(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t h_api_device_template_apply(httpd_req_t *req)
{
    if (s_closing) {
        send_json(req, "{\"ok\":false,\"error\":\"配网收尾中\"}");
        return ESP_OK;
    }
    arm_deadline();
    char *body = NULL;
    int len = recv_body(req, &body);
    if (len < 0) {
        send_json(req, "{\"ok\":false,\"error\":\"读取请求体失败\"}");
        return ESP_OK;
    }
    int id = json_get_int_def(body, "id", -1);
    free(body);
    device_template_list_t list;
    device_template_store_load(&list);
    if (id < 0 || id >= DEVICE_TEMPLATE_MAX || !list.items[id].used) {
        send_json(req, "{\"ok\":false,\"error\":\"模板不存在\"}");
        return ESP_OK;
    }
    const char *name = list.items[id].device_name[0] ? list.items[id].device_name : list.items[id].adv_name;
    if (!name || !name[0]) {
        send_json(req, "{\"ok\":false,\"error\":\"模板缺少设备名\"}");
        return ESP_OK;
    }
    power_engine_t next;
    memcpy(&next, s_engine, sizeof(power_engine_t));
    power_engine_set_device_name(&next, name);
    if (nvs_store_save(&next)) {
        bool name_changed = strcmp(s_engine->device_name, next.device_name) != 0;
        memcpy(s_engine, &next, sizeof(power_engine_t));
        power_engine_clear_dirty(s_engine);
        if (name_changed) ble_bikepower_refresh_device_name();
        send_json(req, "{\"ok\":true}");
    } else {
        send_json(req, "{\"ok\":false,\"error\":\"NVS 保存失败\"}");
    }
    return ESP_OK;
}

static esp_err_t h_api_ble_scan_start(httpd_req_t *req)
{
    arm_deadline();
    esp_err_t rc = ble_scanner_start(BLE_SCAN_DEFAULT_MS);
    if (rc == ESP_OK) send_json(req, "{\"ok\":true,\"scanning\":true}");
    else send_json(req, "{\"ok\":false,\"error\":\"蓝牙扫描启动失败\"}");
    return ESP_OK;
}

static esp_err_t h_api_ble_scan_stop(httpd_req_t *req)
{
    arm_deadline();
    ble_scanner_stop();
    send_json(req, "{\"ok\":true,\"scanning\":false}");
    return ESP_OK;
}

static esp_err_t h_api_ble_scan_results(httpd_req_t *req)
{
    arm_deadline();
    char buf[4096];
    if (ble_scanner_serialize_results(buf, sizeof(buf)) == 0) {
        send_json(req, "{\"scanning\":false,\"devices\":[]}");
        return ESP_OK;
    }
    send_json(req, buf);
    return ESP_OK;
}

static esp_err_t h_api_ble_scan_read(httpd_req_t *req)
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
    char addr[DEVICE_TEMPLATE_ADDR_MAX];
    json_get_str_def(body, "addr", addr, sizeof(addr));
    uint8_t addr_type = (uint8_t)json_get_int_def(body, "addr_type", 0);
    free(body);
    device_template_t tpl;
    esp_err_t rc = ble_scanner_read_template(addr, addr_type, BLE_SCAN_READ_TIMEOUT_MS, &tpl);
    if (rc != ESP_OK) {
        send_json(req, "{\"ok\":false,\"error\":\"连接或读取设备信息失败\"}");
        return ESP_OK;
    }
    char item[1024];
    if (device_template_serialize_json(&tpl, item, sizeof(item)) == 0) {
        send_json(req, "{\"ok\":false,\"error\":\"设备信息序列化失败\"}");
        return ESP_OK;
    }
    char resp[1100];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"template\":%s}", item);
    send_json(req, resp);
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

static esp_err_t h_captive_not_found(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    ESP_LOGI(TAG, "Captive 404 兜底重定向 uri=%s", req && req->uri ? req->uri : "?");
    return captive_portal_detect_handler(req);
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
        { .uri = CAPTIVE_PORTAL_API_PATH, .method = HTTP_GET, .handler = h_captive_portal_api },
        /* 状态与计时 */
        { .uri = "/api/state",       .method = HTTP_GET,  .handler = h_api_state },
        { .uri = "/api/time",        .method = HTTP_GET,  .handler = h_api_time },
        { .uri = "/api/renew",       .method = HTTP_POST, .handler = h_api_renew },
        { .uri = "/api/mem_stats",   .method = HTTP_GET,  .handler = h_api_mem_stats },
        /* 配置 */
        { .uri = "/api/config",      .method = HTTP_POST, .handler = h_api_config },
        { .uri = "/api/close-wifi",  .method = HTTP_POST, .handler = h_api_close_wifi },
        /* 设备信息模板 */
        { .uri = "/api/device-templates",        .method = HTTP_GET,  .handler = h_api_device_templates },
        { .uri = "/api/device-templates/save",   .method = HTTP_POST, .handler = h_api_device_template_save },
        { .uri = "/api/device-templates/delete", .method = HTTP_POST, .handler = h_api_device_template_delete },
        { .uri = "/api/device-templates/apply",  .method = HTTP_POST, .handler = h_api_device_template_apply },
        { .uri = "/api/ble-scan/start",          .method = HTTP_POST, .handler = h_api_ble_scan_start },
        { .uri = "/api/ble-scan/stop",           .method = HTTP_POST, .handler = h_api_ble_scan_stop },
        { .uri = "/api/ble-scan/results",        .method = HTTP_GET,  .handler = h_api_ble_scan_results },
        { .uri = "/api/ble-scan/read",           .method = HTTP_POST, .handler = h_api_ble_scan_read },
        /* Captive Portal 探测端点：各 OS 连通性检测，统一 303 重定向到配网首页触发自动弹窗 */
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
    ret = httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, h_captive_not_found);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Captive Portal 404 兜底注册失败 ret=%d", ret);
        httpd_stop(s_httpd);
        s_httpd = NULL;
        return ret;
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
