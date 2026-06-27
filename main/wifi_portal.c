/**
 * WiFi 配网门户实现：HTTP 门户（esp_http_server）
 *
 * 注意：WiFi SoftAP 启动已收回 radio_ctrl.c 统一管理，本文件只负责 HTTP 路由和页面。
 * 调用方（radio_ctrl）需在调用 wifi_portal_start 前确保 AP 已就绪。
 *
 * 网页 UI 照抄 MicroPython 旧版（web_home/web_common/web_more_pages）：
 *   GET  /            首页（iOS 三卡 + 模式分段 + 训练区间染色 + 续期 + 倒计时 + saved=1 toast）
 *   POST /config      保存配置（实时生效到 BLE）+ 持久化 NVS，302 回首页（不关 WiFi，可继续修改）
 *   POST /close-wifi  保存配置 + 5s 后关闭 WiFi 回到 BLE（用户主动结束配网）
 *   GET  /success     成功页（兼容旧版路由，h_close_wifi 触发后展示）
 *   GET  /more        更多配置页（个人信息 + 设备名碎片入口）
 *   POST /draft       草稿保存（仅应用内存，不持久化不重启，302 回首页）
 *   GET  /ui/module   动态碎片（more/profile/device/mode/erg）
 *   GET  /closing     WiFi 关闭提示页
 *   GET  /time        剩余秒数（纯文本，前端每秒轮询）
 *   GET  /renew       续期（重置超时），返回剩余秒数
 *
 * 超时：180s 无续期自动关闭 WiFi 回到 BLE（不重启）；配置通过 power_engine setter 实时生效。
 *
 * mode 映射：旧版网页用字符串 steady/road/erg，C 版用整数 0/1/2。
 */
#include "wifi_portal.h"
#include "nvs_store.h"
#include "ble_bikepower.h"
#include "bikepower_config.h"
#include "profile.h"
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
// 避免 64 位变量在 httpd 任务续期与 esp_timer 回调读取之间发生撕裂导致误判提前重启。
// 秒级精度对配网窗口（分钟级）与每秒倒计时已足够。
static volatile uint32_t s_deadline_sec = 0;
// 配网结束标志：true 表示已进入收尾流程（保存成功或超时），不再续期、不再接受新请求
static volatile bool s_closing = false;
// done_cb 幂等标志：true 表示 portal_done 已调用过 done_cb，防止定时器重复触发投递多个事件。
// 与 s_closing 分离：s_closing 在 h_close_wifi 设 5s 收尾窗口时就置 true（阻止改配置），
// 但此时 done_cb 尚未调用；s_done_called 只在 portal_done 真正调用 done_cb 时置 true。
static volatile bool s_done_called = false;
// 配网结束回调：保存/超时后由 portal 调用，通知 app_main 投递 APP_EVT_WIFI_DONE 切换状态
static void (*s_done_cb)(void) = NULL;

// 当前单调时间（秒）。
static uint32_t now_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

// ============================ 超时管理 ============================

/**
 * 重置 WiFi 配网超时截止时间。
 * 把截止时间延长到当前时刻之后的固定配网窗口。
 * 无参数。
 * 返回值：无。
 * 特殊行为：若已进入计划重启状态则不再续期，避免保存后被再次延长。
 */
static void arm_deadline(void)
{
    if (s_closing) return;  // 计划重启中不再续期
    s_deadline_sec = now_sec() + (WIFI_SHUTDOWN_MS / 1000);
}

/**
 * 计算距离自动关闭还剩多少秒。
 * 基于当前截止时间与当前单调时间计算剩余秒数。
 * 无参数。
 * 返回值：剩余秒数，最小为 0。
 * 特殊行为：不会返回负数。
 */
static int remaining_seconds(void)
{
    uint32_t now = now_sec();
    if (now >= s_deadline_sec) return 0;
    return (int)(s_deadline_sec - now);
}

/**
 * 配网超时检查定时器回调。
 * 当到达截止时间时调用 done 回调关闭 WiFi 回到 BLE，不重启设备。
 * arg：未使用的定时器参数。
 * 返回值：无。
 * 特殊行为：超时和保存成功均通过本回调统一触发——保存成功时 h_config 会把 deadline 缩短为 5s，
 *           本回调在 5s 后捕获并调用 portal_done；portal_done 内部用 s_closing 做幂等保护。
 */
static void portal_done(void);  // 前向声明：shutdown_check_cb 复用此入口
static void shutdown_check_cb(void *arg)
{
    (void)arg;
    // s_done_called 已置位说明 portal_done 已触发过 done_cb：定时器理应被 wifi_portal_stop 停止。
    // 若因事件投递失败导致主循环未消费 APP_EVT_WIFI_DONE、wifi_portal_stop 未被调用，
    // 定时器仍在跑——此处守卫避免每秒重复调用 portal_done 投递多个事件。
    // 注意：不能用 s_closing 守卫，因为 h_close_wifi 设 5s 收尾窗口时也会置 s_closing=true，
    // 那样会阻止定时器在 5s 后触发 portal_done 关闭 WiFi。
    if (s_done_called) return;
    if (now_sec() >= s_deadline_sec) {
        ESP_LOGI(TAG, "WiFi 配网窗口到期/收尾窗口到期，关闭 WiFi 回到 BLE");
        portal_done();
    }
}

/**
 * 触发配网结束流程（保存成功或超时统一入口）。
 * 设置收尾标志并调用 done 回调，由 app_main 投递 APP_EVT_WIFI_DONE 切换状态。
 * 无参数。
 * 返回值：无。
 * 特殊行为：幂等，重复调用不会重复触发回调。
 */
static void portal_done(void)
{
    if (s_done_called) return;  // 幂等：done_cb 只调用一次
    s_done_called = true;
    s_closing = true;  // 收尾流程彻底启动，阻止后续表单操作
    ESP_LOGI(TAG, "portal_done 触发，调用 done_cb 投递 APP_EVT_WIFI_DONE");
    if (s_done_cb) s_done_cb();
    else ESP_LOGE(TAG, "s_done_cb 为 NULL，APP_EVT_WIFI_DONE 无法投递");
}

// ============================ 工具函数 ============================

// 字符串 ID → mode 整数，未知返回 -1
/**
 * 把网页中的模式字符串转换为领域层模式枚举值。
 * 解析 `steady`、`road`、`erg` 三种模式 ID。
 * id：网页表单里的模式字符串。
 * 返回值：对应的 ride_mode 整数值；未知字符串返回 -1。
 * 特殊行为：仅用于网页到领域层的输入转换。
 */
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

// mode 中文名（成功页用）
/**
 * 获取模式的人类可读中文名称。
 * 用于成功页等需要展示模式文案的场景。
 * mode：领域层模式值。
 * 返回值：静态中文字符串指针。
 * 特殊行为：未知值默认回退为“固定功率”。
 */
static const char *mode_name(int mode)
{
    if (mode == RIDE_MODE_ROAD) return "机器人配速";
    if (mode == RIDE_MODE_ERG) return "ERG";
    return "固定功率";
}

// HTML 转义到 out（截断保护），用于设备名等用户输入
/**
 * 对 HTML 文本做最小必要转义。
 * 将用户输入中的特殊字符转义后写入输出缓冲区，防止页面结构被破坏。
 * in：原始输入字符串。
 * out：输出缓冲区。
 * out_sz：输出缓冲区大小。
 * 返回值：无。
 * 特殊行为：输出空间不足时会截断，但始终保证以 `\0` 结尾。
 */
static void html_escape(const char *in, char *out, size_t out_sz)
{
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 7 < out_sz; i++) {
        char c = in[i];
        const char *rep = NULL;
        switch (c) {
            case '&': rep = "&amp;"; break;
            case '<': rep = "&lt;"; break;
            case '>': rep = "&gt;"; break;
            case '"': rep = "&quot;"; break;
            case '\'': rep = "&#39;"; break;
            default: out[o++] = c; continue;
        }
        size_t rl = strlen(rep);
        memcpy(out + o, rep, rl);
        o += rl;
    }
    out[o] = '\0';
}

// URL 解码（含 %XX 和 +→空格），就地写 out
/**
 * 对 URL 编码字符串做解码。
 * 支持 `%XX` 十六进制转义和 `+` 转空格。
 * s：原始 URL 编码字符串。
 * out：输出缓冲区。
 * out_sz：输出缓冲区大小。
 * 返回值：无。
 * 特殊行为：遇到非法 `%XX` 序列时保留原字符。
 */
static void url_decode(const char *s, char *out, size_t out_sz)
{
    size_t o = 0;
    for (size_t i = 0; s[i] && o + 1 < out_sz; i++) {
        char c = s[i];
        if (c == '+') {
            out[o++] = ' ';
        } else if (c == '%' && s[i + 1] && s[i + 2]) {
            char hex[3] = { s[i + 1], s[i + 2], '\0' };
            char *end;
            long v = strtol(hex, &end, 16);
            if (end == hex + 2) {
                out[o++] = (char)v;
                i += 2;
            } else {
                out[o++] = c;
            }
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

// 从 form-urlencoded body 提取 key 的字符串值（已 URL 解码），未找到返回 false
/**
 * 从 `application/x-www-form-urlencoded` 请求体中提取字符串字段。
 * 按键名查找并返回已 URL 解码的字段值。
 * body：完整表单请求体。
 * key：待提取字段名。
 * out：输出缓冲区。
 * out_sz：输出缓冲区大小。
 * 返回值：`true` 表示找到并成功写入，`false` 表示未找到。
 * 特殊行为：会校验匹配位置必须是字段起始，避免误命中其他字段的子串。
 */
static bool form_get_str(const char *body, const char *key, char *out, size_t out_sz)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "%s=", key);
    const char *p = strstr(body, pat);
    if (!p) return false;
    // 确保匹配到的是字段开头（body 开头或前一字符是 &）
    while (p && p != body && *(p - 1) != '&') {
        p = strstr(p + 1, pat);
    }
    if (!p) return false;
    p += strlen(pat);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    // 按字段实际长度动态分配中转缓冲，避免固定栈缓冲把长字段（如多字节 UTF-8 设备名）
    // 在 url_decode 之前就静默截断。解码后写入 out 仍受调用方 out_sz 约束。
    char *raw = malloc(len + 1);
    if (!raw) return false;
    memcpy(raw, p, len);
    raw[len] = '\0';
    url_decode(raw, out, out_sz);
    free(raw);
    return true;
}

// 从 form body 提取整数，未找到或非法输入返回 def
/**
 * 从表单请求体中提取整数值。
 * 基于 form_get_str 提取字段，再用 strtol 严格解析。
 * body：完整表单请求体。
 * key：待提取字段名。
 * def：字段缺失、为空或解析失败时返回的默认值（保持旧值）。
 * 返回值：解析后的整数值或默认值。
 * 特殊行为：必须完整消费字符串且为合法十进制整数，否则视为非法返回 def。
 */
static int form_get_int(const char *body, const char *key, int def)
{
    char buf[32];
    if (!form_get_str(body, key, buf, sizeof(buf))) return def;
    if (buf[0] == '\0') return def;
    char *endp = NULL;
    long v = strtol(buf, &endp, 10);
    if (endp == buf || *endp != '\0') return def;  // 未消费或残留字符 → 非法
    if (v < INT_MIN || v > INT_MAX) return def;
    return (int)v;
}

// 从查询字符串提取 name 参数（GET /ui/module?name=xxx）
/**
 * 从 URL 查询字符串中提取一个字段。
 * 读取当前请求 URL 的 query 部分并返回指定键的解码结果。
 * req：HTTP 请求对象。
 * key：待提取字段名。
 * out：输出缓冲区。
 * out_sz：输出缓冲区大小。
 * 返回值：`true` 表示找到字段，`false` 表示未找到或解析失败。
 * 特殊行为：内部临时分配查询字符串缓冲区，使用后立即释放。
 */
static bool query_get_str(httpd_req_t *req, const char *key, char *out, size_t out_sz)
{
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen <= 1) return false;
    char *q = malloc(qlen);
    if (!q) return false;
    bool ok = false;
    if (httpd_req_get_url_query_str(req, q, qlen) == ESP_OK) {
        char raw[64];
        if (httpd_query_key_value(q, key, raw, sizeof(raw)) == ESP_OK) {
            url_decode(raw, out, out_sz);
            ok = true;
        }
    }
    free(q);
    return ok;
}

// 收取请求 body 到动态缓冲（调用方 free），返回长度，失败返回 -1，超 HTTP_MAX_BODY_LEN 返回 -2
/**
 * 读取完整 HTTP 请求体到动态缓冲区。
 * 按 content_len 读取 body，并在末尾补 `\0` 便于后续按字符串解析。
 * 请求体超过 HTTP_MAX_BODY_LEN 字节时返回 -2，由调用方返回 413，避免截断后应用半截配置。
 * req：HTTP 请求对象。
 * out：输出的动态缓冲区地址，由调用方负责 free（返回 -2 时不分配）。
 * 返回值：成功时返回实际读取长度；返回 -1 表示读取失败；返回 -2 表示超过 HTTP_MAX_BODY_LEN 字节上限。
 * 特殊行为：超限时不读取任何数据，直接拒绝。
 */
static int recv_body(httpd_req_t *req, char **out)
{
    // content_len 是 size_t，先按无符号比较上限，避免赋给 int 时 >INT_MAX 回绕成负数/异常值。
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

// chunked 发送（大 HTML 分块，避免一次性大缓冲）
#define SEND(req, s)  httpd_resp_sendstr_chunk((req), (s))

// 设备名 HTML 转义后的最大字节数：每字符最坏转义为 6 字节（&quot;），按 DEVICE_NAME_MAX 预留。
#define DEVICE_NAME_HTML_MAX  (DEVICE_NAME_MAX * 6)

/**
 * 结束一次 chunked HTTP 响应。
 * 向 esp_http_server 发送空 chunk，标记响应完成。
 * req：HTTP 请求对象。
 * 返回值：无。
 * 特殊行为：配合 SEND 宏使用，适合大 HTML 分块输出。
 */
static void send_end(httpd_req_t *req)
{
    httpd_resp_sendstr_chunk(req, NULL);
}

// ============================ 首页 ============================

/**
 * 处理首页路由。
 * 输出主配置页面，展示当前数值、模式切换和倒计时状态。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK 表示处理成功。
 * 特殊行为：每次访问首页都会自动续期配网倒计时。
 */
static esp_err_t h_home(httpd_req_t *req)
{
    // 已进入关 WiFi 收尾流程：此时刷新首页会续期 deadline 破坏收尾，引导到成功页
    if (s_closing) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/success");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    arm_deadline();
    power_engine_t *e = s_engine;
    int power = e->base_power;
    int cadence = e->cadence_base;
    int hr = e->last_heartrate;
    int mode = (int)e->mode;
    bool on = (mode == RIDE_MODE_STEADY);
    const char *dis = on ? "" : " disabled";

    char dn[DEVICE_NAME_HTML_MAX];
    html_escape(e->device_name, dn, sizeof(dn));

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");

    SEND(req,
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>BikePower</title><style>"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{background:#f5f5f7;padding:12px;font-family:-apple-system,BlinkMacSystemFont,sans-serif;color:#111}"
        ".box{max-width:420px;margin:0 auto}"
        ".bar,.brand,.status,.card .val{display:flex;align-items:center}"
        ".seg,.btn{display:flex;align-items:center;justify-content:center}"
        ".bar,.status{justify-content:space-between;margin:0 0 10px}"
        ".brand{gap:8px;font-size:24px;font-weight:800}"
        ".gear{position:relative;z-index:10;min-height:36px;padding:0 12px;color:#007aff;background:#fff;border:1px solid #007aff29;border-radius:18px;font-size:13px;font-weight:800;text-decoration:none;display:flex;align-items:center}"
        ".status{font-size:13px}"
        ".status .ble{color:#007aff;font-weight:800}"
        ".badge{background:#fff;border:1px solid #0001;color:#111;border-radius:18px;padding:6px 11px;font-weight:800;font-size:12px}"
        ".badge.ok{background:#e9f8ef;color:#15803d;border-color:#b7ebc8}"
        ".badge.warn{background:#fff3e0;color:#b45309;border-color:#ffd59a}"
        ".note.warn{background:#fff3e0;color:#92400e;border-color:#ffd59a}"
        ".renew-btn{display:none;width:100%;padding:14px;border:0;border-radius:18px;background:#ff9500;color:#fff;font-size:16px;font-weight:800;margin:0 0 10px;animation:pulse 1s infinite}"
        "@keyframes pulse{0%,100%{opacity:1}50%{opacity:.7}}"
        ".mode-info{display:none;position:absolute;top:0;right:0;bottom:0;left:0;overflow-y:auto;background:#fff;border:1px solid #0001;border-radius:22px;padding:18px 16px;margin:0 0 10px}"
        ".mode-info h3{font-size:17px;font-weight:800;margin:0 0 8px}"
        ".mode-info p{font-size:13px;line-height:1.5;color:#666;margin:0 0 8px}"
        ".mode-info .pacer-sel{width:100%;padding:12px;border:1px solid rgba(0,0,0,.08);border-radius:16px;font-size:15px;font-weight:700;background:#f5f5f7;color:#1d1d1f;margin:4px 0}"
        ".mode-info .pacer-label{font-size:12px;font-weight:800;color:#007aff;margin:0 0 4px}"
        "#vs{position:relative}#vs.i .card{visibility:hidden}#vs.road #mi-road,#vs.erg #mi-erg{display:block}.card{background:#fff;border:1px solid #0001;border-radius:22px;padding:15px 16px;margin:0 0 10px}"
        ".card .val{display:flex;align-items:baseline;gap:6px}"
        ".card .val input{width:100%;border:0;background:transparent;color:#111;font-size:36px;font-weight:800;padding:0}"
        ".card .val input.out-of-range{color:#ef4444}"
        ".card .unit{color:#86868b;font-size:14px}"
        ".card .lbl{display:flex;justify-content:space-between;align-items:center;color:#666;font-size:13px;margin-top:3px}"
        ".zone{display:flex;align-items:center;gap:4px;font-size:11px;font-weight:800;color:#666}"
        ".zone .dot{width:12px;height:12px;border-radius:50%;display:inline-block}"
        ".seg-row{display:grid;grid-template-columns:1fr 1fr 1fr;gap:4px;background:#e9e9ee;border:0;border-radius:18px;padding:4px;margin:4px 0 10px}"
        ".seg{gap:5px;padding:11px 0;border-radius:15px;color:#666;font-size:13px;font-weight:800}"
        ".seg input{display:none}.seg.on{background:#fff;color:#111}"
        ".note{font-size:12px;line-height:1.45;color:#3f3f46;background:#fff;border:1px solid #0001;border-radius:18px;padding:10px 12px;margin:0 0 10px}"
        ".btn{gap:8px;width:100%;padding:15px;border:0;border-radius:18px;background:#007aff;color:#fff;font-size:17px;font-weight:800}"
        "@media(prefers-color-scheme:dark){body{background:#1c1c1e;color:#f5f5f7}.card,.note,.mode-info{background:#2c2c2e;border-color:#3a3a3c}.card .val input{color:#f5f5f7}.badge{background:#2c2c2e;border-color:#3a3a3c;color:#f5f5f7}.seg-row{background:#3a3a3c}.seg{color:#8e8e93}.seg.on{background:#2c2c2e;color:#f5f5f7}.mode-info .pacer-sel{background:#3a3a3c;color:#f5f5f7;border-color:#48484a}.gear{background:#2c2c2e;border-color:#48484a;color:#0a84ff}.renew-btn{background:#ff9f0a}}"
        "</style></head><body><div class=\"box\">"
        "<div class=\"bar\"><div class=\"brand\">BikePower <small style=\"font-size:10px;font-weight:600;color:#86868b\">v" FIRMWARE_VERSION "</small></div>"
        "<a class=\"gear\" href=\"/more\" onclick=\"return goMore()\">&#9881; 更多配置</a></div>"
        "<div class=\"status\"><span class=\"ble\">&#9679; 配网中</span>"
        "<span class=\"badge\" id=\"cd\" onclick=\"doRenew()\">剩余 180 秒</span></div>"
        "<button type=\"button\" class=\"renew-btn\" id=\"rb\" onclick=\"doRenew()\">&#9202; 点击延长配网时间</button>"
        "<form id=\"cfg\" method=\"POST\" action=\"/config\" onsubmit=\"return validateForm()\">");

    char buf[1024];
    snprintf(buf, sizeof(buf),
        "<input type=\"hidden\" name=\"device_name\" value=\"%s\">"
        "<div id=\"vs\"%s>"
        "<div class=\"card\">"
        "<div class=\"val\"><input id=\"p\" type=\"number\" name=\"power\" value=\"%d\" min=\"0\" max=\"2000\"%s oninput=\"updZone()\"><span class=\"unit\">W</span></div>"
        "<div class=\"lbl\">功率<span class=\"zone\" id=\"pz\"><span class=\"dot\"></span><span></span></span></div></div>"
        "<div class=\"card\">"
        "<div class=\"val\"><input id=\"c\" type=\"number\" name=\"cadence\" value=\"%d\" min=\"20\" max=\"120\"%s oninput=\"updZone()\"><span class=\"unit\">RPM</span></div>"
        "<div class=\"lbl\">踏频<span class=\"zone\" id=\"cz\"><span class=\"dot\"></span><span></span></span></div></div>",
        dn, on ? "" : " class=\"i\"", power, dis, cadence, dis);
    SEND(req, buf);

    snprintf(buf, sizeof(buf),
        "<div class=\"card\">"
        "<div class=\"val\"><input id=\"h\" type=\"number\" name=\"heartrate\" value=\"%d\" min=\"60\" max=\"200\"%s oninput=\"updZone()\"><span class=\"unit\">BPM</span></div>"
        "<div class=\"lbl\">心率<span class=\"zone\" id=\"hz\"><span class=\"dot\"></span><span></span></span></div></div>", hr, dis);
    SEND(req, buf);

    SEND(req,
        "<div class=\"mode-info\" id=\"mi-road\">"
        "<h3>机器人配速</h3>"
        "<p>按体重和 W/kg 换算目标功率，心率接近对应强度。</p>"
        "<div class=\"pacer-label\">选择配速机器人</div>"
        "<select class=\"pacer-sel\" name=\"profile_ride_type\" id=\"road-pacer\" onchange=\"updNote()\">");
    for (int i = 0; i < PACER_COUNT; i++) {
        snprintf(buf, sizeof(buf),
            "<option value=\"%s\"%s>%s · %d.%d-%d.%d W/kg</option>",
            PROFILE_RIDE_TYPE_OPTIONS[i],
            i == e->profile.ride_type ? " selected" : "",
            ROAD_PACERS[i].name,
            ROAD_PACERS[i].wkg_low / 10, ROAD_PACERS[i].wkg_low % 10,
            ROAD_PACERS[i].wkg_high / 10, ROAD_PACERS[i].wkg_high % 10);
        SEND(req, buf);
    }

    SEND(req,
        "</select>"
        "<p style=\"margin-top:12px;padding:10px 12px;background:#e8f4fd;border-radius:12px;font-size:12px;color:#007aff;line-height:1.5\">&#128161; 修改体重、FTP 等个人信息可获得更精准的功率计算，<a href=\"/more\" style=\"color:#007aff;font-weight:700\" onclick=\"return goMore()\">前往设置 →</a></p>"
        "</div>"
        "<div class=\"mode-info\" id=\"mi-erg\">"
        "<h3>ERG 模式</h3>"
        "<p>App 通过 BLE FTMS 下发目标功率，设备跟随训练课表。心率和踏频保持小幅波动。</p>"
        "<p style=\"color:#007aff;font-weight:600\">功率由骑行 App 动态下发，无需手动设置。</p>"
        "</div></div>"
        "<div class=\"seg-row\">");

    // 三段模式按钮
    const struct { int m; const char *id; const char *label; } segs[] = {
        { RIDE_MODE_STEADY, "steady", "固定功率" },
        { RIDE_MODE_ROAD, "road", "机器人配速" },
        { RIDE_MODE_ERG, "erg", "ERG 模式" },
    };
    for (int i = 0; i < RIDE_MODE_COUNT; i++) {
        // 这里把领域层整数模式重新映射成旧版网页使用的字符串值，保证前端兼容。
        snprintf(buf, sizeof(buf),
            "<label class=\"%s\" onclick=\"setTimeout(syncMode,0)\"><input type=\"radio\" name=\"mode\" value=\"%s\"%s>%s</label>",
            mode == segs[i].m ? "seg on" : "seg", segs[i].id,
            mode == segs[i].m ? " checked" : "", segs[i].label);
        SEND(req, buf);
    }

    SEND(req,
        "</div>"
        "<div class=\"note\" id=\"mn\">固定功率：手动设置三项数据，保存后骑行 App 直接接收。</div>"
        // 两个提交按钮：通过 formaction 区分目标路由。
        // 主按钮"保存设置" → /config（仅保存+实时生效，不关 WiFi，可继续修改）
        // 次按钮"保存并关闭 WiFi" → /close-wifi（保存+实时生效+5s 后关 WiFi 回到 BLE）
        "<button class=\"btn\" type=\"submit\" formaction=\"/config\">&#10003; 保存设置</button>"
        "<button class=\"btn\" type=\"submit\" formaction=\"/close-wifi\" style=\"background:#34c759;margin-top:8px\">&#8634; 保存并关闭 WiFi</button></form>"
        // 保存成功 toast：由首页 URL 携带 saved=1 触发，2s 后自动消失
        "<div id=\"toast\" style=\"display:none;position:fixed;left:50%;bottom:32px;transform:translateX(-50%);background:#1d1d1f;color:#fff;padding:12px 22px;border-radius:18px;font-size:14px;font-weight:700;box-shadow:0 8px 24px rgba(0,0,0,.18);z-index:50\">&#10003; 已保存并实时生效</div>"
        "</div><script>");

    snprintf(buf, sizeof(buf),
        "var F=%d,W=%d,T=%d,C=[\"#94a3b8\",\"#2563eb\",\"#16a34a\",\"#f97316\",\"#ef4444\",\"#b91c1c\",\"#7f1d1d\"],P=[\"恢复\",\"有氧\",\"节奏\",\"阈值\",\"VO2\",\"无氧\",\"神经\"],H=[\"恢复\",\"有氧\",\"节奏\",\"阈值\",\"VO2\"],E={};",
        e->profile.ftp, e->profile.weight, e->profile.thr_hr);
    SEND(req, buf);

    SEND(req,
        // saved=1 toast：h_config 保存成功 302 回首页时携带，2s 后自动消失。
        "if(location.search.indexOf(\"saved=1\")>=0){"
        "var t=g(\"toast\");t.style.display=\"block\";"
        "setTimeout(function(){t.style.display=\"none\";history.replaceState(null,\"\",\"\")},2000)}"
        "function g(i){return E[i]||(E[i]=document.getElementById(i))}var z;function updZone(){clearTimeout(z);z=setTimeout(uz,150)}"
        "function uz(){var w=parseInt(g(\"p\").value)||0,h=parseInt(g(\"h\").value)||0,c=parseInt(g(\"c\").value)||0,r=w/F,x=h/T,p=w&&F?(r<.55?0:r<.75?1:r<.9?2:r<1.05?3:r<1.2?4:r<1.5?5:6):0,hr=h&&T?(x<.68?0:x<.83?1:x<.94?2:x<1.05?3:4):0,a=g(\"pz\"),b=g(\"hz\"),d=g(\"cz\");"
        "g(\"p\").className=w<0||w>2000?\"out-of-range\":\"\";g(\"c\").className=c<20||c>120?\"out-of-range\":\"\";g(\"h\").className=h<60||h>200?\"out-of-range\":\"\";"
        "a.children[0].style.background=C[p];a.children[1].textContent=w&&W?(w/W).toFixed(1)+\"W/kg Z\"+(p+1)+\" \"+P[p]:\"-\";"
        "b.children[0].style.background=C[hr];b.children[1].textContent=h?\"Z\"+(hr+1)+\" \"+H[hr]:\"-\";"
        "d.children[0].style.background=c>=80&&c<=100?\"#16a34a\":c>=60&&c<=120?\"#f97316\":\"#ef4444\";d.children[1].textContent=c?(c>=80&&c<=100?\"稳定 80-100\":c>100?\"偏高\":\"偏低 \"+c+\" RPM\"):\"-\";"
        "updNote(w,h,c,p,hr)}"
        "function updNote(w,h,c,p,hr){var m=curMode();if(m===\"steady\"){var t=\"手动设置三项数据\";if(w&&F)t+=\"，当前 \"+(w/W).toFixed(1)+\" W/kg（\"+P[p]+\"区间）\";if(h&&T)t+=\"，心率 \"+H[hr]+\"区间\";g(\"mn\").textContent=\"固定功率：\"+t+\"。\"}else if(m===\"road\"){g(\"mn\").textContent=\"机器人配速：按体重和 W/kg 换算功率，模拟跟骑。\"}else if(m===\"erg\"){g(\"mn\").textContent=\"ERG：App 下发目标功率，设备跟随训练课表。\"}}"
        "var R=0;function doRenew(){if(R)return;R=1;var b=g(\"cd\");b.className=\"badge ok\";b.textContent=\"已延长\";fetch(\"/renew\").then(function(r){return r.text()}).then(function(t){var s=parseInt(t);if(s>0){_sv=s;_lt=Date.now();updCd()}});setTimeout(function(){b.className=\"badge\";R=0},1200)}"
        "function curMode(){var r=document.getElementsByName(\"mode\");for(var i=0;i<r.length;i++){if(r[i].checked)return r[i].value}return \"steady\"}"
        "function goMore(){var fd=new URLSearchParams(new FormData(document.getElementById(\"cfg\")));fetch(\"/draft\",{method:\"POST\",body:fd}).then(function(){location.href=\"/more\"}).catch(function(){location.href=\"/more\"});return false}"
        "function validateForm(){if(curMode()!==\"steady\")return true;var p=parseInt(g(\"p\").value),c=parseInt(g(\"c\").value),h=parseInt(g(\"h\").value);if(p<0||p>2000){alert(\"功率超出范围（0-2000W）\");return false}if(c<20||c>120){alert(\"踏频超出范围（20-120 RPM）\");return false}if(h<60||h>200){alert(\"心率超出范围（60-200 BPM）\");return false}return true}"
        "function syncMode(){var m=curMode();var on=m===\"steady\";"
        "var s=document.getElementsByClassName(\"seg\");for(var i=0;i<s.length;i++){var c=s[i].getElementsByTagName(\"input\")[0];s[i].className=c.checked?\"seg on\":\"seg\"}"
        "g(\"vs\").className=on?\"\":m===\"road\"?\"i road\":\"i erg\";"
        "[\"p\",\"c\",\"h\"].forEach(function(id){g(id).disabled=!on});"
        "updNote()}");

    snprintf(buf, sizeof(buf),
        "syncMode();updZone();");
    SEND(req, buf);

    SEND(req,
        "var _sv=180,_lt=Date.now(),_lw=0;"
        "function updCd(){var el=Math.floor((Date.now()-_lt)/1000),s=_sv-el;if(s<0)s=0;"
        "var w=s>0&&s<=30?1:0;if(w!==_lw){_lw=w;g(\"cd\").className=w?\"badge warn\":\"badge\";g(\"mn\").className=w?\"note warn\":\"note\";g(\"rb\").style.display=w?\"block\":\"none\"}"
        "g(\"cd\").textContent=s>0?\"剩余 \"+s+\" 秒 · 点击延长\":\"WiFi 正在关闭\";"
        "if(w)g(\"mn\").textContent=\"剩余 \"+s+\" 秒，请保存或点击延长。\";"
        "if(s<=0)location.href=\"/closing\"}"
        "function tick(){if(document.hidden)return;fetch(\"/time\").then(function(r){return r.text()}).then(function(t){_sv=parseInt(t);_lt=Date.now();updCd()})}"
        "updCd();setInterval(updCd,1000);setInterval(tick,5000);"
        "</script></body></html>");

    send_end(req);
    return ESP_OK;
}

// ============================ 成功页 ============================

/**
 * 输出配置保存成功页面。
 * 展示最终保存的关键配置，并给出即将关闭 WiFi 回到 BLE 的倒计时。
 * req：HTTP 请求对象。
 * 返回值：无。
 * 特殊行为：只负责输出页面，不修改收尾状态；配置已通过 power_engine setter 实时生效，BLE 通知中已是新值。
 */
static void send_success(httpd_req_t *req)
{
    power_engine_t *e = s_engine;
    char dn[DEVICE_NAME_HTML_MAX];
    html_escape(e->device_name, dn, sizeof(dn));

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    SEND(req,
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>设置已保存</title><style>"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{min-height:100vh;background:#f5f5f7;padding:24px 16px;display:flex;justify-content:center;align-items:center;font-family:-apple-system,BlinkMacSystemFont,\"Helvetica Neue\",Arial,sans-serif;color:#1d1d1f}"
        ".box{max-width:360px;width:100%;background:#fff;border:1px solid rgba(0,0,0,.06);border-radius:26px;padding:28px;text-align:center;box-shadow:0 12px 32px rgba(0,0,0,.08)}"
        ".icon{width:64px;height:64px;border-radius:50%;margin:0 auto 14px;background:#e9f8ef;color:#34c759;font-size:38px;font-weight:800;display:flex;align-items:center;justify-content:center}"
        "h1{font-size:22px;margin-bottom:8px;font-weight:800;letter-spacing:-.03em}"
        ".sub{font-size:13px;color:#6e6e73;line-height:1.5;margin-bottom:16px}"
        ".values{background:#f5f5f7;border-radius:18px;padding:14px;margin:14px 0;border:1px solid rgba(0,0,0,.04)}"
        ".values p{color:#1d1d1f;font-size:15px;font-weight:800}.values .muted{margin-top:7px;font-size:12px;color:#6e6e73;font-weight:700}"
        ".reboot-box{background:#eef6ff;border:1px solid #cfe5ff;border-radius:18px;padding:16px;margin:16px 0;color:#0f4c81}"
        ".reboot-box .title{font-size:16px;font-weight:800;margin-bottom:6px}"
        ".reboot-box .desc{font-size:13px;line-height:1.6}"
        ".countdown{background:#fff;color:#007aff;font-size:42px;font-weight:800;border-radius:50%;width:82px;height:82px;display:flex;justify-content:center;align-items:center;margin:16px auto;border:1px solid rgba(0,122,255,.2);box-shadow:0 8px 20px rgba(0,122,255,.14)}"
        ".note{font-size:12px;color:#86868b;margin-top:12px;font-weight:600;line-height:1.5}"
        "</style></head><body><div class=\"box\">"
        "<div class=\"icon\">&#10003;</div>"
        "<h1>已保存</h1>"
        "<p class=\"sub\">配置已实时生效，蓝牙连接未中断。</p>"
        "<div class=\"values\">");

    char buf[768];
    snprintf(buf, sizeof(buf),
        "<p>%dW / %dRPM / %dBPM</p>"
        "<p class=\"muted\">%s模式 · 蓝牙名称：%s</p></div>"
        "<div class=\"reboot-box\"><div class=\"title\">下一步</div>"
        "<div class=\"desc\">倒计时结束后 WiFi 自动关闭，%s 全程在线，回到骑行 App 即可继续骑行。</div></div>",
        e->base_power, e->cadence_base, e->last_heartrate,
        mode_name((int)e->mode), dn, dn);
    SEND(req, buf);

    SEND(req,
        "<div class=\"countdown\" id=\"t\">5</div>"
        "<p class=\"note\">请保持设备供电，WiFi 会自动关闭。</p>"
        "</div>"
        "<script>var t=5;function c(){if(t>0){document.getElementById(\"t\").innerHTML=t;t--;setTimeout(c,1000)}else{document.body.innerHTML='<div style=\"text-align:center;padding:40px;color:#6e6e73;font-weight:600;font-family:-apple-system,BlinkMacSystemFont,Helvetica Neue,Arial,sans-serif\"><p style=\"font-size:22px;font-weight:800;color:#1d1d1f\">WiFi 已关闭</p><p style=\"margin-top:12px;font-size:15px\">蓝牙全程未断开，回到骑行 App 继续骑行即可</p></div>'}}c()</script></body></html>");
    send_end(req);
}

/**
 * 发送配置保存失败错误页（HTTP 500）。
 * 页面提示用户保存失败，提供返回重试按钮，不触发重启。
 * req：HTTP 请求对象。
 */
static void send_save_error(httpd_req_t *req)
{
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    SEND(req,
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>保存失败</title><style>"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{min-height:100vh;background:#f5f5f7;padding:24px 16px;display:flex;justify-content:center;align-items:center;font-family:-apple-system,BlinkMacSystemFont,\"Helvetica Neue\",Arial,sans-serif;color:#1d1d1f}"
        ".box{max-width:360px;width:100%;background:#fff;border:1px solid rgba(0,0,0,.06);border-radius:26px;padding:28px;text-align:center;box-shadow:0 12px 32px rgba(0,0,0,.08)}"
        ".icon{width:64px;height:64px;border-radius:50%;margin:0 auto 14px;background:#fff2f2;color:#ff3b30;font-size:38px;font-weight:800;display:flex;align-items:center;justify-content:center}"
        "h1{font-size:22px;margin-bottom:8px;font-weight:800;letter-spacing:-.03em}"
        ".sub{font-size:13px;color:#6e6e73;line-height:1.5;margin-bottom:16px}"
        ".btn{display:inline-block;padding:12px 24px;background:#007aff;color:#fff;border-radius:12px;text-decoration:none;font-size:15px;font-weight:600}"
        ".note{font-size:12px;color:#86868b;margin-top:12px;font-weight:600;line-height:1.5}"
        "</style></head><body><div class=\"box\">"
        "<div class=\"icon\">!</div>"
        "<h1>保存失败</h1>"
        "<p class=\"sub\">配置未能写入设备存储，请检查后重试。</p>"
        "<a href=\"/\" class=\"btn\">返回重试</a>"
        "<p class=\"note\">设备不会自动重启，仍可继续操作。</p>"
        "</div></body></html>");
    send_end(req);
}

/**
 * 处理成功页路由。
 * 在已进入重启流程时输出成功页，否则将直接访问重定向回首页。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK 表示处理成功。
 * 特殊行为：充当 PRG 守卫，避免用户未提交配置却直接打开成功页。
 */
static esp_err_t h_success(httpd_req_t *req)
{
    if (!s_closing) {  // 未提交配置时直访，302 回首页（与旧版 PRG 守卫一致）
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    send_success(req);
    return ESP_OK;
}

// ============================ 配置提交 ============================

// 应用表单参数到引擎（与按钮路径一致，mode 字符串转整数）
/**
 * 把网页提交的表单字段统一应用到功率引擎。
 * 解析 mode/设备名/手动三项/profile 五项并写入引擎，供 /config 与 /draft 共用。
 * body：表单请求体。
 * 返回值：无。
 * 特殊行为：手动功率/踏频/心率三项依赖 power_engine setter 内部的"仅 STEADY 生效"守卫，
 *           非 STEADY 模式下 setter 自行拒绝，无需调用方再判断。
 */
static void apply_form_fields(power_engine_t *e, const char *body)
{
    char val[64];

    if (form_get_str(body, "mode", val, sizeof(val))) {
        int m = mode_from_id(val);
        if (m >= 0) power_engine_set_mode(e, (ride_mode_t)m);
    }
    if (form_get_str(body, "device_name", val, sizeof(val))) {
        power_engine_set_device_name(e, val);
    }
    // 手动三项：setter 内部仅 STEADY 生效，非 STEADY 自动拒绝
    power_engine_set_power(e, form_get_int(body, "power", e->base_power));
    power_engine_set_cadence(e, form_get_int(body, "cadence", e->cadence_base));
    power_engine_set_heartrate(e, form_get_int(body, "heartrate", e->last_heartrate));

    profile_set_weight(&e->profile, form_get_int(body, "profile_weight", e->profile.weight));
    profile_set_ftp(&e->profile, form_get_int(body, "profile_ftp", e->profile.ftp));
    profile_set_thr_hr(&e->profile, form_get_int(body, "profile_thr_hr", e->profile.thr_hr));
    if (form_get_str(body, "profile_ride_type", val, sizeof(val)) && val[0])
        profile_set_ride_type_str(&e->profile, val);
}

/**
 * 处理配置提交路由（仅保存，不关闭 WiFi）。
 * 读取表单、应用配置（实时生效到 BLE 通知）、保存到 NVS，302 回首页并带 saved=1 提示。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK 表示成功处理，ESP_FAIL 表示读取请求体失败。
 * 特殊行为：本路由不触发 WiFi 关闭，用户可反复保存继续修改；如需关闭 WiFi 走 /close-wifi。
 */
static esp_err_t h_config(httpd_req_t *req)
{
    if (s_closing) {  // 已进入收尾流程，拒绝再保存
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/success");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // 开始处理配置提交前先续期超时窗口，避免处理过程中（读请求体/写NVS/发响应）被超时触发收尾
    arm_deadline();

    char *body = NULL;
    int len = recv_body(req, &body);
    if (len == -2) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "request body too large");
        return ESP_FAIL;
    }
    if (len < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "read error");
        return ESP_FAIL;
    }

    // 写入临时副本：NVS 失败时直接丢弃，s_engine 完全不变，BLE 主循环不会读到中间值造成跳变
    power_engine_t next;
    memcpy(&next, s_engine, sizeof(power_engine_t));
    apply_form_fields(&next, body);
    free(body);

    if (nvs_store_save(&next)) {
        // NVS 成功后再 swap 回主引擎：BLE 主循环下次取快照即推新值
        // 设备名变化时同步刷新 GAP 服务+广播，让扫描端立即看到新名
        bool name_changed = strcmp(s_engine->device_name, next.device_name) != 0;
        memcpy(s_engine, &next, sizeof(power_engine_t));
        power_engine_clear_dirty(s_engine);
        if (name_changed) {
            ble_bikepower_refresh_device_name();
        }
        ESP_LOGI(TAG, "配置已保存（仅保存，不关WiFi）p=%dW c=%dRPM mode=%d name=%s",
                 s_engine->base_power, s_engine->cadence_base,
                 (int)s_engine->mode, s_engine->device_name);
        // PRG 模式：302 回首页，URL 带 saved=1 触发"已保存"toast。
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/?saved=1");
        httpd_resp_send(req, NULL, 0);
    } else {
        ESP_LOGE(TAG, "配置保存失败，s_engine 保持原值");
        // 保存失败后续期完整超时窗口，允许用户重试
        arm_deadline();
        send_save_error(req);
        return ESP_OK;  // 500 响应已通过 SEND 完整发送，避免 httpd 重复发错误
    }

    return ESP_OK;
}

/**
 * 处理"保存并关闭 WiFi"路由。
 * 与 h_config 同样保存配置+实时生效，额外触发 portal_done 关闭 WiFi 回到 BLE。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK 表示成功处理，ESP_FAIL 表示读取请求体失败。
 * 特殊行为：成功后设置 5s 收尾窗口，由 shutdown_check_cb 触发 portal_done 关 WiFi。
 */
static esp_err_t h_close_wifi(httpd_req_t *req)
{
    if (s_closing) {  // 防重复提交
        send_success(req);
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
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "read error");
        return ESP_FAIL;
    }

    power_engine_t next;
    memcpy(&next, s_engine, sizeof(power_engine_t));
    apply_form_fields(&next, body);
    free(body);

    if (nvs_store_save(&next)) {
        bool name_changed = strcmp(s_engine->device_name, next.device_name) != 0;
        memcpy(s_engine, &next, sizeof(power_engine_t));
        power_engine_clear_dirty(s_engine);
        if (name_changed) {
            ble_bikepower_refresh_device_name();
        }
        ESP_LOGI(TAG, "配置已保存并准备关闭 WiFi p=%dW c=%dRPM mode=%d name=%s",
                 s_engine->base_power, s_engine->cadence_base,
                 (int)s_engine->mode, s_engine->device_name);
        // 配置已实时生效到 BLE 通知，设置 5s 收尾窗口让成功页发送后关闭 WiFi。
        s_closing = true;
        s_deadline_sec = now_sec() + WIFI_PORTAL_REBOOT_DELAY_SEC;
        send_success(req);
    } else {
        ESP_LOGE(TAG, "配置保存失败，s_engine 保持原值");
        arm_deadline();
        send_save_error(req);
        return ESP_OK;
    }

    return ESP_OK;
}

// ============================ 草稿保存 ============================

/**
 * 处理草稿保存路由。
 * 仅把更多配置页里的字段暂存到内存，随后重定向回首页，不写 NVS 也不重启。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK 表示处理成功。
 * 特殊行为：该路由用于多页 UI 回写隐藏字段，不代表真正保存到设备。
 */
static esp_err_t h_draft(httpd_req_t *req)
{
    // 配置已提交并计划重启时，拒绝再修改内存配置：否则会覆盖刚写入 NVS 的快照，
    // 导致重启后 BLE 生效值与成功页展示的"已保存"值不一致。直接引导到成功页。
    if (s_closing) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/success");
        httpd_resp_send(req, NULL, 0);
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
        // 草稿语义：仅写内存不落盘。apply_form_fields 会置 dirty，
        // 立即清除避免主循环 10s 心跳自动 nvs_store_save 把草稿误存进 NVS。
        // 用户点"保存设置"或"保存并关闭 WiFi"才会真正持久化。
        apply_form_fields(s_engine, body);
        power_engine_clear_dirty(s_engine);
        power_engine_t *e = s_engine;
        ESP_LOGI(TAG, "草稿保存（仅内存，未落盘）mode=%d p=%d c=%d hr=%d name=%s w=%d ftp=%d thr=%d ride=%d",
                 (int)e->mode, e->base_power, e->cadence_base, e->last_heartrate,
                 e->device_name, e->profile.weight, e->profile.ftp,
                 e->profile.thr_hr, e->profile.ride_type);
        free(body);
    }
    // 302 回首页
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ============================ 更多配置页 ============================

/**
 * 处理更多配置页路由。
 * 输出一个壳页面，内部通过 `/ui/module` 动态加载不同设置面板。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK 表示处理成功。
 * 特殊行为：访问时会自动续期配网倒计时。
 */
static esp_err_t h_more(httpd_req_t *req)
{
    arm_deadline();
    power_engine_t *e = s_engine;
    char dn[DEVICE_NAME_HTML_MAX];
    html_escape(e->device_name, dn, sizeof(dn));
    const char *ride = PROFILE_RIDE_TYPE_OPTIONS[
        (e->profile.ride_type >= 0 && e->profile.ride_type < PACER_COUNT) ? e->profile.ride_type : 0];

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    SEND(req,
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>更多配置 - BikePower</title><style>"
        "*{margin:0;padding:0;box-sizing:border-box}"
        "body{background:#f5f5f7;padding:12px;font-family:-apple-system,BlinkMacSystemFont,\"Helvetica Neue\",Arial,sans-serif;color:#1d1d1f}"
        ".box{max-width:420px;margin:0 auto}"
        ".hero{background:#fff;border:1px solid rgba(0,0,0,.06);border-radius:24px;padding:14px 14px 16px;margin-bottom:12px;box-shadow:0 10px 28px rgba(0,0,0,.06)}"
        "h1{font-size:24px;margin:0 0 4px;letter-spacing:-.04em}.sub{font-size:12px;color:#6e6e73;line-height:1.5;margin-bottom:12px}"
        "a.back{display:flex;align-items:center;justify-content:center;width:100%;min-height:44px;margin:0 0 12px;color:#fff;background:#007aff;border-radius:18px;text-decoration:none;font-size:15px;font-weight:800;box-shadow:0 10px 20px rgba(0,122,255,.22)}"
        ".panel-card{border:1px solid rgba(0,0,0,.06);border-radius:20px;padding:14px;margin-bottom:10px;background:#fff;box-shadow:0 6px 18px rgba(0,0,0,.045)}"
        ".panel-card h2{font-size:15px;margin-bottom:6px;letter-spacing:-.02em}.panel-card p{font-size:12px;line-height:1.55;color:#6e6e73}"
        ".module-btn{display:block;width:100%;margin-top:8px;padding:12px;border:none;border-radius:16px;background:#007aff;color:#fff;font-weight:800}"
        ".module-btn.secondary{background:#fff;color:#007aff;border:1px solid rgba(0,122,255,.18)}"
        ".module-note{margin-top:8px;font-size:12px;line-height:1.5;color:#0f4c81;background:#eef6ff;border:1px solid #cfe5ff;border-radius:16px;padding:10px}"
        ".range-row{margin-top:8px}.range-row label{display:flex;justify-content:space-between;font-size:12px;font-weight:700;color:#6e6e73;margin-bottom:6px}"
        ".range-row .range-value{font-size:11px;color:#007aff;font-weight:800}"
        ".spin{width:20px;height:20px;margin:0 auto 8px;border:3px solid #cfe5ff;border-top-color:#007aff;border-radius:50%;animation:r .8s linear infinite}@keyframes r{to{transform:rotate(360deg)}}.busy{opacity:.55;pointer-events:none}"
        ".btn{display:block;width:100%;margin:10px 0 0;padding:14px;border:0;border-radius:18px;background:#1d1d1f;color:#fff;font-size:15px;font-weight:800;text-align:center;text-decoration:none}"
        ".btn.alt{background:#fff;color:#007aff;border:1px solid rgba(0,122,255,.18)}"
        "input,select{width:100%;padding:12px;border:1px solid rgba(0,0,0,.08);border-radius:16px;font-size:15px;background:#f5f5f7;color:#1d1d1f;font-weight:700}"
        ".field{margin:9px 0}.field label{display:block;font-size:12px;font-weight:800;color:#6e6e73;margin:0 0 5px}"
        "</style></head><body><div class=\"box\">"
        "<div class=\"hero\"><a class=\"back\" href=\"/\">&larr; 返回首页</a><h1>更多配置</h1><p class=\"sub\">先调整个人信息或设备名称，再应用回首页统一保存。</p></div>"
        "<form method=\"POST\" action=\"/draft\" id=\"core-form\">");

    char buf[1024];
    snprintf(buf, sizeof(buf),
        "<input type=\"hidden\" id=\"form-mode\" name=\"mode\" value=\"%s\">"
        "<input type=\"hidden\" id=\"form-power\" name=\"power\" value=\"%d\">"
        "<input type=\"hidden\" id=\"form-cadence\" name=\"cadence\" value=\"%d\">"
        "<input type=\"hidden\" id=\"form-heartrate\" name=\"heartrate\" value=\"%d\">"
        "<input type=\"hidden\" id=\"form-device-name\" name=\"device_name\" value=\"%s\">"
        "<input type=\"hidden\" id=\"form-profile-weight\" name=\"profile_weight\" value=\"%d\">"
        "<input type=\"hidden\" id=\"form-profile-ftp\" name=\"profile_ftp\" value=\"%d\">"
        "<input type=\"hidden\" id=\"form-profile-thr-hr\" name=\"profile_thr_hr\" value=\"%d\">"
        "<input type=\"hidden\" id=\"form-profile-ride-type\" name=\"profile_ride_type\" value=\"%s\">"
        "<div id=\"panel\"></div>"
        "<button class=\"btn\" type=\"submit\">应用并回首页</button></form>",
        mode_to_id(e->mode), e->base_power, e->cadence_base, e->last_heartrate,
        dn, e->profile.weight, e->profile.ftp, e->profile.thr_hr, ride);
    SEND(req, buf);

    SEND(req,
        "</div><script>"
        "var panelCleanup=null,modBusy=false;function runScripts(r){var s=r.getElementsByTagName(\"script\");for(var i=0;i<s.length;i++){try{eval(s[i].textContent)}catch(e){}}}"
        "function busyCard(t){return '<div class=\"panel-card\" style=\"text-align:center;padding:18px\"><div class=\"spin\"></div><p>'+t+'</p></div>'}"
        "function setBusy(b){modBusy=b;var bs=document.getElementsByTagName(\"button\");for(var i=0;i<bs.length;i++){bs[i].disabled=b;if(b)bs[i].classList.add(\"busy\");else bs[i].classList.remove(\"busy\")}}"
        "function openMod(n){if(modBusy)return;if(panelCleanup){try{panelCleanup()}catch(e){}panelCleanup=null}var p=document.getElementById(\"panel\");setBusy(true);p.innerHTML=busyCard(\"正在加载...\");fetch(\"/ui/module?name=\"+encodeURIComponent(n)).then(function(r){return r.text()}).then(function(h){p.innerHTML=h;setBusy(false);runScripts(p)}).catch(function(){setBusy(false);p.innerHTML='<div class=\"module-note\">加载失败，请重试</div>'})}"
        "openMod(\"more\");"
        "</script></body></html>");
    send_end(req);
    return ESP_OK;
}

// ============================ 动态碎片 /ui/module ============================

#define PANEL_STYLE \
    "<style>" \
    ".panel-card{border:1px solid rgba(0,0,0,.06);border-radius:20px;padding:14px;margin-bottom:10px;background:#fff;box-shadow:0 6px 18px rgba(0,0,0,.045)}" \
    ".panel-card h2{font-size:15px;margin-bottom:6px;letter-spacing:-.02em}.panel-card p{font-size:12px;line-height:1.55;color:#6e6e73}" \
    ".module-btn{display:block;width:100%;margin-top:8px;padding:12px;border:none;border-radius:16px;background:#007aff;color:#fff;font-weight:800}" \
    ".module-btn.secondary{background:#fff;color:#007aff;border:1px solid rgba(0,122,255,.18)}" \
    ".module-note{margin-top:8px;font-size:12px;line-height:1.5;color:#0f4c81;background:#eef6ff;border:1px solid #cfe5ff;border-radius:16px;padding:10px}" \
    ".range-row{margin-top:8px}.range-row label{display:flex;justify-content:space-between;font-size:12px;font-weight:700;color:#6e6e73;margin-bottom:6px}" \
    ".range-row .range-value{font-size:11px;color:#007aff;font-weight:800}" \
    ".spin{width:20px;height:20px;margin:0 auto 8px;border:3px solid #cfe5ff;border-top-color:#007aff;border-radius:50%;animation:r .8s linear infinite}@keyframes r{to{transform:rotate(360deg)}}.busy{opacity:.55;pointer-events:none}" \
    "</style>"

/**
 * 输出“更多配置”首页面板。
 * 提供进入个人信息和设备名称两个子面板的入口按钮。
 * req：HTTP 请求对象。
 * 返回值：无。
 * 特殊行为：该函数只输出 HTML 碎片，依赖外层页面的脚本环境。
 */
static void send_more_panel(httpd_req_t *req)
{
    SEND(req, PANEL_STYLE
        "<style>"
        ".menu-grid{display:grid;grid-template-columns:1fr;gap:10px;margin:8px 0}"
        ".menu-btn{display:flex;align-items:center;justify-content:space-between;text-align:left;padding:15px;border:0;border-radius:20px;background:#fff;color:#1d1d1f;font-size:15px;font-weight:800;text-decoration:none;min-height:72px;cursor:pointer;border:1px solid rgba(0,0,0,.06);box-shadow:0 6px 18px rgba(0,0,0,.045)}"
        ".menu-btn:active{opacity:0.85}"
        ".menu-btn .icon{width:34px;height:34px;border-radius:12px;background:#eef6ff;color:#007aff;display:flex;align-items:center;justify-content:center;font-size:19px;margin-right:10px}"
        ".menu-btn .text{flex:1}.menu-btn .desc{display:block;margin-top:3px;color:#6e6e73;font-size:12px;font-weight:600;line-height:1.35}.menu-btn .arrow{color:#c7c7cc;font-size:22px}"
        "</style>"
        "<div class=\"menu-grid\">"
        "<button type=\"button\" class=\"menu-btn\" onclick=\"openMod('profile')\"><span class=\"icon\">&#128100;</span><span class=\"text\">个人信息<span class=\"desc\">体重、FTP、阈值心率</span></span><span class=\"arrow\">&rsaquo;</span></button>"
        "<button type=\"button\" class=\"menu-btn\" onclick=\"openMod('device')\"><span class=\"icon\">&#128241;</span><span class=\"text\">设备名称<span class=\"desc\">修改骑行 App 中看到的蓝牙名称</span></span><span class=\"arrow\">&rsaquo;</span></button>"
        "</div>"
        "<script>panelCleanup=function(){};</script>");
}

/**
 * 输出设备名称编辑面板。
 * 显示当前设备名称输入框，并与隐藏表单字段保持同步。
 * req：HTTP 请求对象。
 * 返回值：无。
 * 特殊行为：只是更新草稿字段，真正保存要回首页再提交 `/config`。
 */
static void send_device_panel(httpd_req_t *req)
{
    char dn[DEVICE_NAME_HTML_MAX];
    html_escape(s_engine->device_name, dn, sizeof(dn));
    SEND(req, PANEL_STYLE
        "<div class=\"panel-card\">"
        "<h2>设备名称</h2>"
        "<p>这是骑行 App 搜索蓝牙设备时看到的名称。应用回首页后，再点击保存并应用生效。</p>"
        "<div class=\"field\"><input inputmode=\"text\" type=\"text\" id=\"more-device-name\" value=\"");
    SEND(req, dn);
    SEND(req, "\" maxlength=\"22\" oninput=\"syncDeviceName()\"></div></div>"
        "<script>"
        "window.syncDeviceName=function(){var v=document.getElementById(\"more-device-name\");if(v)document.getElementById(\"form-device-name\").value=v.value};"
        "(function(){var hf=document.getElementById(\"form-device-name\");if(hf&&hf.value)document.getElementById(\"more-device-name\").value=hf.value})();"
        "panelCleanup=function(){};"
        "</script>");
}

/**
 * 输出个人信息编辑面板。
 * 展示体重、FTP、阈值心率和配速机器人等设置项。
 * req：HTTP 请求对象。
 * 返回值：无。
 * 特殊行为：输入变化只同步到隐藏字段，不立即写入引擎持久化。
 */
static void send_profile_panel(httpd_req_t *req)
{
    SEND(req, PANEL_STYLE
        "<div class=\"panel-card\">"
        "<h2>个人信息</h2>"
        "<p>这些数据用于机器人配速和心率区间。数值越准确，模拟结果越贴近真实骑行。</p>"
        "</div>"
        "<div class=\"panel-card\">"
        "<div class=\"field\" style=\"margin-top:10px\"><label>体重 (kg)</label>"
        "<input inputmode=\"numeric\" type=\"number\" id=\"profile-weight\" min=\"40\" max=\"150\" value=\"\" placeholder=\"如 70\" oninput=\"syncProfileWeight()\"></div>"
        "<div class=\"field\" style=\"margin-top:14px\"><label>功率 FTP (W)</label>"
        "<input inputmode=\"numeric\" type=\"number\" id=\"profile-ftp\" min=\"50\" max=\"500\" value=\"\" placeholder=\"如 200\" oninput=\"syncProfileFtp()\"></div>"
        "<div class=\"field\" style=\"margin-top:14px\"><label>阈值心率 (BPM)</label>"
        "<input inputmode=\"numeric\" type=\"number\" id=\"profile-thr-hr\" min=\"100\" max=\"220\" value=\"\" placeholder=\"如 170\" oninput=\"syncProfileThrHr()\"></div>"
        "</div>"
        "<div class=\"module-note\">配速机器人请在首页「机器人配速」模式下选择。调整后请点\"应用并回首页\"，再在首页点\"保存并应用\"写入设备。</div>"
        "<script>"
        "window.syncProfileWeight=function(){document.getElementById(\"form-profile-weight\").value=document.getElementById(\"profile-weight\").value};"
        "window.syncProfileFtp=function(){document.getElementById(\"form-profile-ftp\").value=document.getElementById(\"profile-ftp\").value};"
        "window.syncProfileThrHr=function(){document.getElementById(\"form-profile-thr-hr\").value=document.getElementById(\"profile-thr-hr\").value};"
        "(function(){var fw=document.getElementById(\"form-profile-weight\");if(fw&&fw.value){document.getElementById(\"profile-weight\").value=fw.value}var ff=document.getElementById(\"form-profile-ftp\");if(ff&&ff.value){document.getElementById(\"profile-ftp\").value=ff.value}var ft=document.getElementById(\"form-profile-thr-hr\");if(ft&&ft.value){document.getElementById(\"profile-thr-hr\").value=ft.value}})();"
        "panelCleanup=function(){};"
        "</script>");
}

/**
 * 输出机器人配速模式说明面板。
 * 解释路配模式的功率换算逻辑和适用场景。
 * req：HTTP 请求对象。
 * 返回值：无。
 * 特殊行为：当前版本该面板仅作为说明页，不含可编辑字段。
 */
static void send_mode_panel(httpd_req_t *req)
{
    SEND(req, PANEL_STYLE
        "<div class=\"panel-card\">"
        "<h2>配速机器人</h2>"
        "<p>设备会根据机器人的 W/kg 区间和你的体重，自动换算目标功率。</p>"
        "<p style=\"margin-top:8px\">心率会逐步接近对应强度，适合模拟跟随稳定骑行团。</p>"
        "</div>"
        "<div class=\"module-note\">请在首页切换到「机器人配速」模式后直接选择配速机器人。修改体重、FTP 等个人信息可获得更精准的功率计算。</div>"
        "<script>panelCleanup=function(){};</script>");
}

/**
 * 输出 ERG 模式说明面板。
 * 说明 FTMS 控制点如何驱动目标功率变化及其工作流程。
 * req：HTTP 请求对象。
 * 返回值：无。
 * 特殊行为：当前版本该面板仅展示说明，不提供直接配置项。
 */
static void send_erg_panel(httpd_req_t *req)
{
    SEND(req, PANEL_STYLE
        "<div class=\"panel-card\">"
        "<h2>ERG 模式</h2>"
        "<p>由 Zwift、TrainerRoad 等骑行 App 实时下发目标功率。</p>"
        "<p style=\"margin-top:8px\">设备会跟随训练课表变化，心率和踏频保持小幅波动。</p>"
        "</div>"
        "<div class=\"panel-card\">"
        "<h2>工作原理</h2>"
        "<p>1. App 通过 BLE FTMS 下发目标功率</p>"
        "<p>2. 设备接收后自动切换并跟随目标</p>"
        "<p>3. 心率和踏频继续模拟真实骑行变化</p>"
        "</div>"
        "<div class=\"module-note\">此模式下首页的手动功率不生效；非 ERG 模式下 App 无法遥控功率。</div>"
        "<script>panelCleanup=function(){};</script>");
}

/**
 * 处理动态模块路由。
 * 根据查询参数 `name` 输出对应 HTML 碎片，用于更多配置页内的局部刷新。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK 表示处理成功。
 * 特殊行为：未知模块名时返回提示文本而不是 404。
 */
static esp_err_t h_ui_module(httpd_req_t *req)
{
    arm_deadline();
    char name[24] = "";
    query_get_str(req, "name", name, sizeof(name));

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    if (strcmp(name, "more") == 0)        send_more_panel(req);
    else if (strcmp(name, "device") == 0) send_device_panel(req);
    else if (strcmp(name, "profile") == 0) send_profile_panel(req);
    else if (strcmp(name, "mode") == 0)   send_mode_panel(req);
    else if (strcmp(name, "erg") == 0)    send_erg_panel(req);
    else SEND(req, "<div class=\"module-note\">请选择模块。</div>");
    send_end(req);
    return ESP_OK;
}

// ============================ 关闭提示页 ============================

/**
 * 处理 WiFi 关闭提示页路由。
 * 在配网窗口结束后向浏览器展示一个静态提示页面。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK 表示处理成功。
 * 特殊行为：页面本身不触发任何动作，WiFi 关闭由后台定时器 + done 回调完成。
 */
static esp_err_t h_closing(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>WiFi正在关闭</title></head>"
        "<body style=\"background:#f5f5f7;min-height:100vh;margin:0;padding:24px 16px;display:flex;justify-content:center;align-items:center;font-family:-apple-system,BlinkMacSystemFont,Helvetica Neue,Arial,sans-serif;color:#1d1d1f\">"
        "<div style=\"max-width:360px;width:100%;background:#fff;border:1px solid rgba(0,0,0,.06);border-radius:26px;padding:28px;text-align:center;box-shadow:0 12px 32px rgba(0,0,0,.08)\">"
        "<div style=\"width:64px;height:64px;border-radius:50%;margin:0 auto 14px;background:#eef6ff;color:#007aff;font-size:38px;font-weight:800;display:flex;align-items:center;justify-content:center\">&#9202;</div>"
        "<h1 style=\"font-size:22px;margin:0 0 8px;font-weight:800;letter-spacing:-.03em\">WiFi 正在关闭</h1>"
        "<p style=\"color:#6e6e73;font-size:13px;line-height:1.5;margin:0\">蓝牙全程未断开，回到骑行 App 继续骑行即可。</p>"
        "</div></body></html>");
    return ESP_OK;
}

// ============================ 计时器路由 ============================

/**
 * 处理剩余时间查询路由。
 * 以纯文本形式返回当前 WiFi 配网剩余秒数。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK 表示处理成功。
 * 特殊行为：用于前端轮询倒计时显示，不会自动续期。
 */
static esp_err_t h_time(httpd_req_t *req)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", remaining_seconds());
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/**
 * 处理续期路由。
 * 重置截止时间并复用 h_time 返回最新剩余秒数。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK 表示处理成功。
 * 特殊行为：这是前端“点击延长”按钮的后端入口。
 */
static esp_err_t h_renew(httpd_req_t *req)
{
    arm_deadline();
    return h_time(req);
}

// favicon：返回 204 空响应，避免浏览器配网时报 404
/**
 * 处理 favicon 路由。
 * 返回空响应，避免浏览器自动请求图标时产生无意义 404。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK 表示处理成功。
 * 特殊行为：HTTP 状态码固定为 204 No Content。
 */
static esp_err_t h_favicon(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// 内存统计 JSON（free + min_free），用于调试/压测
/**
 * 处理内存统计路由。
 * 返回当前空闲堆和历史最小空闲堆的 JSON 数据。
 * req：HTTP 请求对象。
 * 返回值：ESP_OK 表示处理成功。
 * 特殊行为：仅用于调试和压测，不参与正式配网流程。
 */
static esp_err_t h_mem_stats(httpd_req_t *req)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"free\":%u,\"min_free\":%u}",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// ============================ 启动 ============================

/**
 * 启动 HTTP 配网服务。
 * 创建 esp_http_server 实例并注册所有页面与辅助接口路由。
 * 无参数。
 * 返回值：ESP_OK 表示 HTTP 服务已启动并注册所有路由，否则为错误码。
 * 特殊行为：若启动失败只记录日志并返回错误码，调用方决定后续动作。
 */
static esp_err_t start_httpd(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = WIFI_PORT;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = HTTP_MAX_URI_HANDLERS;
    cfg.stack_size = TASK_STACK_HTTPD;

    esp_err_t ret = httpd_start(&s_httpd, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd 启动失败 ret=%d", ret);
        return ret;
    }
    httpd_uri_t routes[] = {
        { .uri = "/",          .method = HTTP_GET,  .handler = h_home },
        { .uri = "/config",    .method = HTTP_POST, .handler = h_config },
        { .uri = "/close-wifi", .method = HTTP_POST, .handler = h_close_wifi },
        { .uri = "/success",   .method = HTTP_GET,  .handler = h_success },
        { .uri = "/more",      .method = HTTP_GET,  .handler = h_more },
        { .uri = "/draft",     .method = HTTP_POST, .handler = h_draft },
        { .uri = "/ui/module", .method = HTTP_GET,  .handler = h_ui_module },
        { .uri = "/closing",   .method = HTTP_GET,  .handler = h_closing },
        { .uri = "/time",      .method = HTTP_GET,  .handler = h_time },
        { .uri = "/renew",     .method = HTTP_GET,  .handler = h_renew },
        { .uri = "/mem_stats", .method = HTTP_GET,  .handler = h_mem_stats },
        { .uri = "/favicon.ico", .method = HTTP_GET, .handler = h_favicon },
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
    ESP_LOGI(TAG, "HTTP 配网服务就绪 端口=%d", WIFI_PORT);
    return ESP_OK;
}

/**
 * 启动 WiFi 配网门户（HTTP 部分）。
 * 绑定功率引擎、启动 HTTP 服务，并开启超时检查定时器。
 * 调用前必须已由 radio_ctrl 启动 SoftAP（共存模式下 BLE 仍在线）。
 * engine：供网页读取和写入配置的功率引擎实例。
 * 返回值：ESP_OK 表示门户已就绪，否则为错误码。
 * 特殊行为：HTTP 启动失败时不会 arm 定时器，调用方应关闭 SoftAP 回到 BLE。
 */
esp_err_t wifi_portal_start(power_engine_t *engine)
{
    s_engine = engine;
    s_closing = false;
    s_done_called = false;

    esp_err_t ret = start_httpd();
    if (ret != ESP_OK) {
        // httpd 启动失败：portal 没跑起来，done_cb 不会被定时器触发。
        // 主动调用 portal_done 投递 APP_EVT_WIFI_DONE，让状态机走 radio_ctrl_exit_wifi
        // 清理已启动的 SoftAP 并回到 ST_BLE_ACTIVE，避免永久卡在配网态。
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
        portal_done();  // 触发 done_cb 让状态机清理 SoftAP 回 BLE
        return ret;
    }
    ret = esp_timer_start_periodic(s_shutdown_timer, WIFI_PORTAL_CHECK_INTERVAL_US);  // 每秒检查超时
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic 失败 ret=%d", ret);
        esp_timer_delete(s_shutdown_timer);
        s_shutdown_timer = NULL;
        httpd_stop(s_httpd);
        s_httpd = NULL;
        portal_done();  // 触发 done_cb 让状态机清理 SoftAP 回 BLE
        return ret;
    }

    ESP_LOGI(TAG, "WiFi 配网模式就绪（BLE 共存），%ds 后自动超时", WIFI_SHUTDOWN_MS / 1000);
    return ESP_OK;
}

/**
 * 停止 WiFi 配网门户。
 * 停止超时检查定时器并关闭 HTTP 服务，释放 esp_http_server 资源。
 * 无参数。
 * 返回值：无。
 * 特殊行为：通常由 done 回调间接触发（在 app_main 任务上下文里调用，避免 httpd 任务自杀）；
 *           SoftAP 由 radio_ctrl_exit_wifi 单独关闭；可重复调用（幂等）。
 */
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
    ESP_LOGI(TAG, "HTTP 配网门户已停止");
}

/**
 * 注册配网结束回调。
 * 保存成功或超时后会调用 cb，由调用方在 app_main 任务上下文里关闭 WiFi 并切换状态。
 * cb：配网结束回调，可为 NULL。
 * 返回值：无。
 * 特殊行为：回调在 esp_timer 任务上下文执行，应仅做轻量操作（如投递事件队列），避免阻塞。
 */
void wifi_portal_set_done_callback(void (*cb)(void))
{
    s_done_cb = cb;
}
