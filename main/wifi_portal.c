/**
 * WiFi 配网门户实现：SoftAP + esp_http_server
 *
 * 网页 UI 照抄 MicroPython 旧版（web_home/web_common/web_more_pages）：
 *   GET  /            首页（iOS 三卡 + 模式分段 + 训练区间染色 + 续期 + 倒计时）
 *   POST /config      解析表单，应用配置 + 持久化 NVS，返回成功页，5s 后重启
 *   GET  /success     成功页（兼容旧版路由）
 *   GET  /more        更多配置页（个人信息 + 设备名碎片入口）
 *   POST /draft       草稿保存（仅应用内存，不持久化不重启，302 回首页）
 *   GET  /ui/module   动态碎片（more/profile/device/mode/erg）
 *   GET  /closing     WiFi 关闭提示页
 *   GET  /time        剩余秒数（纯文本，前端每秒轮询）
 *   GET  /renew       续期（重置超时），返回剩余秒数
 *
 * 超时：180s 无续期自动 esp_restart() 恢复 BLE（方案 A）。
 *
 * mode 映射：旧版网页用字符串 steady/road/erg，C 版用整数 0/1/2。
 */
#include "wifi_portal.h"
#include "nvs_store.h"
#include "bikepower_config.h"
#include "profile.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "PORTAL";

static power_engine_t *s_engine = NULL;
static httpd_handle_t s_httpd = NULL;
static esp_timer_handle_t s_shutdown_timer = NULL;
static volatile int64_t s_deadline_us = 0;
static volatile bool s_rebooting = false;

// ============================ 超时管理 ============================

static void arm_deadline(void)
{
    if (s_rebooting) return;  // 计划重启中不再续期
    s_deadline_us = esp_timer_get_time() + (int64_t)WIFI_SHUTDOWN_MS * 1000;
}

static int remaining_seconds(void)
{
    int64_t left = s_deadline_us - esp_timer_get_time();
    if (left < 0) left = 0;
    return (int)(left / 1000000);
}

static void shutdown_check_cb(void *arg)
{
    (void)arg;
    if (esp_timer_get_time() >= s_deadline_us) {
        ESP_LOGI(TAG, "%s，重启恢复 BLE", s_rebooting ? "配置已保存" : "WiFi 配网超时");
        esp_restart();
    }
}

// ============================ 工具函数 ============================

// 字符串 ID → mode 整数，未知返回 -1
static int mode_from_id(const char *id)
{
    if (strcmp(id, "road") == 0) return RIDE_MODE_ROAD;
    if (strcmp(id, "erg") == 0) return RIDE_MODE_ERG;
    if (strcmp(id, "steady") == 0) return RIDE_MODE_STEADY;
    return -1;
}

// mode 中文名（成功页用）
static const char *mode_name(int mode)
{
    if (mode == RIDE_MODE_ROAD) return "机器人配速";
    if (mode == RIDE_MODE_ERG) return "ERG";
    return "固定功率";
}

// 配速机器人标签：如 "Coco · 2.4-2.8 W/kg"
static void road_pacer_label(int ride_type, char *out, size_t out_sz)
{
    if (ride_type < 0 || ride_type >= PACER_COUNT) ride_type = 0;
    const road_pacer_t *p = &ROAD_PACERS[ride_type];
    snprintf(out, out_sz, "%s · %d.%d-%d.%d W/kg",
             p->name, p->wkg_lo / 10, p->wkg_lo % 10, p->wkg_hi / 10, p->wkg_hi % 10);
}

// HTML 转义到 out（截断保护），用于设备名等用户输入
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
    char raw[128];
    if (len >= sizeof(raw)) len = sizeof(raw) - 1;
    memcpy(raw, p, len);
    raw[len] = '\0';
    url_decode(raw, out, out_sz);
    return true;
}

// 从 form body 提取整数，未找到返回 def
static int form_get_int(const char *body, const char *key, int def)
{
    char buf[32];
    if (!form_get_str(body, key, buf, sizeof(buf))) return def;
    if (buf[0] == '\0') return def;
    return atoi(buf);
}

// 从查询字符串提取 name 参数（GET /ui/module?name=xxx）
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

// 收取请求 body 到动态缓冲（调用方 free），返回长度，失败返回 -1
static int recv_body(httpd_req_t *req, char **out)
{
    int total = req->content_len;
    if (total <= 0 || total > 2048) total = total > 2048 ? 2048 : 0;
    char *buf = malloc((size_t)total + 1);
    if (!buf) return -1;
    int recv = 0;
    while (recv < total) {
        int r = httpd_req_recv(req, buf + recv, total - recv);
        if (r <= 0) { free(buf); return -1; }
        recv += r;
    }
    buf[recv] = '\0';
    *out = buf;
    return recv;
}

// chunked 发送（大 HTML 分块，避免一次性大缓冲）
#define SEND(req, s)  httpd_resp_sendstr_chunk((req), (s))
static void send_end(httpd_req_t *req)
{
    httpd_resp_sendstr_chunk(req, NULL);
}

// ============================ 首页 ============================

static esp_err_t h_home(httpd_req_t *req)
{
    arm_deadline();
    power_engine_t *e = s_engine;
    int power = e->base_power;
    int cadence = e->cadence_base;
    int hr = e->last_heartrate;
    int mode = (int)e->mode;
    bool on = (mode == RIDE_MODE_STEADY);
    const char *dis = on ? "" : " disabled";

    char dn[64];
    html_escape(e->device_name, dn, sizeof(dn));
    char pacer[48];
    road_pacer_label(e->profile.ride_type, pacer, sizeof(pacer));

    httpd_resp_set_type(req, "text/html; charset=utf-8");

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
        ".gear{min-height:36px;padding:0 12px;color:#007aff;background:#fff;border:1px solid #007aff29;border-radius:18px;font-size:13px;font-weight:800;text-decoration:none;display:flex;align-items:center}"
        ".status{font-size:13px}"
        ".status .ble{color:#007aff;font-weight:800}"
        ".badge{background:#fff;border:1px solid #0001;color:#111;border-radius:18px;padding:6px 11px;font-weight:800;font-size:12px}"
        ".badge.ok{background:#e9f8ef;color:#15803d;border-color:#b7ebc8}"
        ".badge.warn{background:#fff3e0;color:#b45309;border-color:#ffd59a}"
        ".note.warn{background:#fff3e0;color:#92400e;border-color:#ffd59a}"
        "#vs{position:relative}.card{background:#fff;border:1px solid #0001;border-radius:22px;padding:15px 16px;margin:0 0 10px}"
        ".card .val{align-items:baseline;gap:6px}"
        ".card .val input{width:100%;border:0;background:transparent;color:#111;font-size:36px;font-weight:800;padding:0}"
        ".card .unit{color:#86868b;font-size:14px}"
        ".card .lbl{display:flex;justify-content:space-between;align-items:center;color:#666;font-size:13px;margin-top:3px}"
        ".zone{display:flex;align-items:center;gap:4px;font-size:11px;font-weight:800;color:#666}"
        ".zone .dot{width:7px;height:7px;border-radius:50%;display:inline-block}"
        ".seg-row{display:grid;grid-template-columns:1fr 1fr 1fr;gap:4px;background:#e9e9ee;border:0;border-radius:18px;padding:4px;margin:4px 0 10px}"
        ".seg{gap:5px;padding:11px 0;border-radius:15px;color:#666;font-size:13px;font-weight:800}"
        ".seg input{display:none}.seg.on{background:#fff;color:#111}"
        ".note{font-size:12px;line-height:1.45;color:#3f3f46;background:#fff;border:1px solid #0001;border-radius:18px;padding:10px 12px;margin:0 0 10px}"
        "#mi{display:none;position:absolute;inset:0;background:#fff;border:1px solid #0001;border-radius:22px;padding:18px;font-size:13px;line-height:1.55;color:#111}"
        "#mi b{display:block;color:#111;font-size:18px;margin-bottom:8px}.mi-sub{font-size:13px;font-weight:800;margin-bottom:8px;color:#007aff}.mi-line{color:#666;margin-top:5px}#vs.i .card{visibility:hidden}#vs.i #mi{display:block}"
        ".btn{gap:8px;width:100%;padding:15px;border:0;border-radius:18px;background:#007aff;color:#fff;font-size:17px;font-weight:800}"
        "</style></head><body><div class=\"box\">"
        "<div class=\"bar\"><div class=\"brand\">BikePower</div>"
        "<a class=\"gear\" href=\"/more\">&#9881; 更多配置</a></div>"
        "<div class=\"status\"><span class=\"ble\">&#9679; 配网中</span>"
        "<span class=\"badge\" id=\"cd\" onclick=\"doRenew()\">剩余 180 秒</span></div>"
        "<form method=\"POST\" action=\"/config\">");

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
        "<div class=\"lbl\">心率<span class=\"zone\" id=\"hz\"><span class=\"dot\"></span><span></span></span></div></div>"
        "<div id=\"mi\"></div></div>"
        "<div class=\"seg-row\">",
        hr, dis);
    SEND(req, buf);

    // 三段模式按钮
    const struct { int m; const char *id; const char *label; } segs[] = {
        { RIDE_MODE_STEADY, "steady", "固定功率" },
        { RIDE_MODE_ROAD, "road", "机器人配速" },
        { RIDE_MODE_ERG, "erg", "ERG 模式" },
    };
    for (int i = 0; i < 3; i++) {
        snprintf(buf, sizeof(buf),
            "<label class=\"%s\"><input type=\"radio\" name=\"mode\" value=\"%s\"%s onchange=\"syncMode()\">%s</label>",
            mode == segs[i].m ? "seg on" : "seg", segs[i].id,
            mode == segs[i].m ? " checked" : "", segs[i].label);
        SEND(req, buf);
    }

    SEND(req,
        "</div>"
        "<div class=\"note\" id=\"mn\">固定功率：手动设置三项数据，保存后骑行 App 直接接收。</div>"
        "<button class=\"btn\" type=\"submit\">&#8635; 保存并重启</button></form>"
        "</div><script>");

    snprintf(buf, sizeof(buf),
        "var F=%d,W=%d,T=%d,C=[\"#94a3b8\",\"#2563eb\",\"#16a34a\",\"#f97316\",\"#ef4444\",\"#b91c1c\",\"#7f1d1d\"],P=[\"恢复\",\"有氧\",\"节奏\",\"阈值\",\"VO2\",\"无氧\",\"神经\"],H=[\"恢复\",\"有氧\",\"节奏\",\"阈值\",\"VO2\"],E={};",
        e->profile.ftp, e->profile.weight, e->profile.thr_hr);
    SEND(req, buf);

    SEND(req,
        "function g(i){return E[i]||(E[i]=document.getElementById(i))}var z;function updZone(){clearTimeout(z);z=setTimeout(uz,250)}"
        "function uz(){var w=parseInt(g(\"p\").value)||0,h=parseInt(g(\"h\").value)||0,c=parseInt(g(\"c\").value)||0,r=w/F,x=h/T,p=w&&F?(r<.55?0:r<.75?1:r<.9?2:r<1.05?3:r<1.2?4:r<1.5?5:6):0,hr=h&&T?(x<.68?0:x<.83?1:x<.94?2:x<1.05?3:4):0,a=g(\"pz\"),b=g(\"hz\"),d=g(\"cz\");"
        "a.children[0].style.background=C[p];a.children[1].textContent=w&&W?(w/W).toFixed(1)+\"W/kg Z\"+(p+1)+\" \"+P[p]:\"-\";"
        "b.children[0].style.background=C[hr];b.children[1].textContent=h?\"Z\"+(hr+1)+\" \"+H[hr]:\"-\";"
        "d.children[0].style.background=c>=80&&c<=100?\"#16a34a\":c>=60&&c<=120?\"#f97316\":\"#ef4444\";d.children[1].textContent=c?(c>=80&&c<=100?\"稳定\":c>100?\"偏高\":\"偏低\"):\"-\"}"
        "var R=0;function doRenew(){if(R)return;R=1;var b=g(\"cd\");b.className=\"badge ok\";b.textContent=\"已延长\";fetch(\"/renew\").then(tick);setTimeout(function(){b.className=\"badge\";R=0},1200)}"
        "function curMode(){var r=document.getElementsByName(\"mode\");for(var i=0;i<r.length;i++){if(r[i].checked)return r[i].value}return \"steady\"}"
        "function syncMode(){var m=curMode();var on=m===\"steady\";"
        "var s=document.getElementsByClassName(\"seg\");for(var i=0;i<s.length;i++){var c=s[i].getElementsByTagName(\"input\")[0];s[i].className=c.checked?\"seg on\":\"seg\"}"
        "g(\"vs\").className=on?\"\":\"i\";"
        "[\"p\",\"c\",\"h\"].forEach(function(id){g(id).disabled=!on});"
        "g(\"mn\").textContent=on?\"固定功率：手动设置三项数据，保存后骑行 App 直接接收。\":m===\"erg\"?\"ERG：App 下发目标功率，设备跟随训练课表。\":\"机器人配速：按体重和 W/kg 换算功率，模拟跟骑。\";");

    snprintf(buf, sizeof(buf),
        "g(\"mi\").innerHTML=m===\"erg\"?\"<b>ERG 模式</b><div class=\\\"mi-line\\\">App 下发目标功率，设备跟随课表；心率和踏频小幅波动。</div>\":\"<b>机器人配速</b><div class=\\\"mi-sub\\\">%s</div><div class=\\\"mi-line\\\">按体重换算目标功率，心率接近对应强度。到更多配置调整个人信息和机器人。</div>\"}syncMode();updZone();",
        pacer);
    SEND(req, buf);

    SEND(req,
        "var _lw=0;function tick(){if(document.hidden)return;fetch(\"/time\").then(function(r){return r.text()}).then(function(t){var s=parseInt(t);"
        "var w=s>0&&s<=30?1:0;if(w!==_lw){_lw=w;g(\"cd\").className=w?\"badge warn\":\"badge\";g(\"mn\").className=w?\"note warn\":\"note\"}"
        "g(\"cd\").textContent=s>0?\"剩余 \"+s+\" 秒 · 点击延长\":\"WiFi 正在关闭\";"
        "if(w)g(\"mn\").textContent=\"剩余 \"+s+\" 秒，请保存或点击延长。\";if(s<=0)location.href=\"/closing\"})}"
        "tick();setInterval(tick,5000);"
        "</script></body></html>");

    send_end(req);
    return ESP_OK;
}

// ============================ 成功页 ============================

static void send_success(httpd_req_t *req)
{
    power_engine_t *e = s_engine;
    char dn[64];
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
        "<p class=\"sub\">设备将重启并恢复蓝牙连接。</p>"
        "<div class=\"values\">");

    char buf[512];
    snprintf(buf, sizeof(buf),
        "<p>%dW / %dRPM / %dBPM</p>"
        "<p class=\"muted\">%s模式 · 蓝牙名称：%s</p></div>"
        "<div class=\"reboot-box\"><div class=\"title\">下一步</div>"
        "<div class=\"desc\">倒计时结束后回到骑行 App，重新连接 %s。</div></div>",
        e->base_power, e->cadence_base, e->last_heartrate,
        mode_name((int)e->mode), dn, dn);
    SEND(req, buf);

    SEND(req,
        "<div class=\"countdown\" id=\"t\">5</div>"
        "<p class=\"note\">请保持设备供电，WiFi 会自动关闭。</p>"
        "</div>"
        "<script>var t=5;function c(){if(t>0){document.getElementById(\"t\").innerHTML=t;t--;setTimeout(c,1000)}else{document.body.innerHTML='<div style=\"text-align:center;padding:40px;color:#6e6e73;font-weight:600;font-family:-apple-system,BlinkMacSystemFont,Helvetica Neue,Arial,sans-serif\"><p style=\"font-size:22px;font-weight:800;color:#1d1d1f\">设备正在重启</p><p style=\"margin-top:12px;font-size:15px\">蓝牙恢复后，请回到骑行 App 重新连接</p></div>'}}c()</script></body></html>");
    send_end(req);
}

static esp_err_t h_success(httpd_req_t *req)
{
    send_success(req);
    return ESP_OK;
}

// ============================ 配置提交 ============================

// 应用表单参数到引擎（与按钮路径一致，mode 字符串转整数）
static void apply_config(const char *body)
{
    power_engine_t *e = s_engine;
    char val[64];

    int cur_mode = (int)e->mode;
    int mode_target = cur_mode;
    if (form_get_str(body, "mode", val, sizeof(val))) {
        int m = mode_from_id(val);
        if (m >= 0) {
            power_engine_set_mode(e, (ride_mode_t)m);
            mode_target = m;
        }
    }

    if (form_get_str(body, "device_name", val, sizeof(val))) {
        power_engine_set_device_name(e, val);
    }

    // 仅固定功率模式应用手动数值
    if (mode_target == RIDE_MODE_STEADY) {
        int p = form_get_int(body, "power", e->base_power);
        power_engine_set_power(e, p);
        int c = form_get_int(body, "cadence", e->cadence_base);
        power_engine_set_cadence(e, c);
        int h = form_get_int(body, "heartrate", e->last_heartrate);
        power_engine_set_heartrate(e, h);
    }

    if (form_get_str(body, "profile_weight", val, sizeof(val)) && val[0])
        profile_set_weight(&e->profile, atoi(val));
    if (form_get_str(body, "profile_ftp", val, sizeof(val)) && val[0])
        profile_set_ftp(&e->profile, atoi(val));
    if (form_get_str(body, "profile_thr_hr", val, sizeof(val)) && val[0])
        profile_set_thr_hr(&e->profile, atoi(val));
    if (form_get_str(body, "profile_ride_type", val, sizeof(val)) && val[0])
        profile_set_ride_type_str(&e->profile, val);
}

static esp_err_t h_config(httpd_req_t *req)
{
    if (s_rebooting) {  // 防重复提交
        send_success(req);
        return ESP_OK;
    }

    char *body = NULL;
    int len = recv_body(req, &body);
    if (len < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "read error");
        return ESP_FAIL;
    }

    apply_config(body);
    free(body);

    if (nvs_store_save(s_engine)) {
        power_engine_clear_dirty(s_engine);
        ESP_LOGI(TAG, "配置已保存 p=%dW c=%dRPM mode=%d name=%s",
                 s_engine->base_power, s_engine->cadence_base,
                 (int)s_engine->mode, s_engine->device_name);
    } else {
        ESP_LOGE(TAG, "配置保存失败");
    }

    send_success(req);

    s_rebooting = true;
    s_deadline_us = esp_timer_get_time() + 5000000;
    return ESP_OK;
}

// ============================ 草稿保存 ============================

static esp_err_t h_draft(httpd_req_t *req)
{
    arm_deadline();
    char *body = NULL;
    int len = recv_body(req, &body);
    if (len >= 0) {
        power_engine_t *e = s_engine;
        char val[64];
        if (form_get_str(body, "device_name", val, sizeof(val)))
            power_engine_set_device_name(e, val);
        if (form_get_str(body, "profile_weight", val, sizeof(val)) && val[0])
            profile_set_weight(&e->profile, atoi(val));
        if (form_get_str(body, "profile_ftp", val, sizeof(val)) && val[0])
            profile_set_ftp(&e->profile, atoi(val));
        if (form_get_str(body, "profile_thr_hr", val, sizeof(val)) && val[0])
            profile_set_thr_hr(&e->profile, atoi(val));
        if (form_get_str(body, "profile_ride_type", val, sizeof(val)) && val[0])
            profile_set_ride_type_str(&e->profile, val);
        ESP_LOGI(TAG, "草稿保存 name=%s w=%d ftp=%d thr=%d ride=%d",
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

static esp_err_t h_more(httpd_req_t *req)
{
    arm_deadline();
    power_engine_t *e = s_engine;
    char dn[64];
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

    char buf[768];
    snprintf(buf, sizeof(buf),
        "<input type=\"hidden\" id=\"form-device-name\" name=\"device_name\" value=\"%s\">"
        "<input type=\"hidden\" id=\"form-profile-weight\" name=\"profile_weight\" value=\"%d\">"
        "<input type=\"hidden\" id=\"form-profile-ftp\" name=\"profile_ftp\" value=\"%d\">"
        "<input type=\"hidden\" id=\"form-profile-thr-hr\" name=\"profile_thr_hr\" value=\"%d\">"
        "<input type=\"hidden\" id=\"form-profile-ride-type\" name=\"profile_ride_type\" value=\"%s\">"
        "<div id=\"panel\"></div>"
        "<button class=\"btn\" type=\"submit\">应用并回首页</button></form>",
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
        "<button type=\"button\" class=\"menu-btn\" onclick=\"openMod('profile')\"><span class=\"icon\">&#128100;</span><span class=\"text\">个人信息<span class=\"desc\">体重、FTP、阈值心率和配速机器人</span></span><span class=\"arrow\">&rsaquo;</span></button>"
        "<button type=\"button\" class=\"menu-btn\" onclick=\"openMod('device')\"><span class=\"icon\">&#128241;</span><span class=\"text\">设备名称<span class=\"desc\">修改骑行 App 中看到的蓝牙名称</span></span><span class=\"arrow\">&rsaquo;</span></button>"
        "</div>"
        "<script>panelCleanup=function(){};</script>");
}

static void send_device_panel(httpd_req_t *req)
{
    char dn[64];
    html_escape(s_engine->device_name, dn, sizeof(dn));
    SEND(req, PANEL_STYLE
        "<div class=\"panel-card\">"
        "<h2>设备名称</h2>"
        "<p>这是骑行 App 搜索蓝牙设备时看到的名称。应用回首页后，再点击保存并重启生效。</p>"
        "<div class=\"field\"><input inputmode=\"text\" type=\"text\" id=\"more-device-name\" value=\"");
    SEND(req, dn);
    SEND(req, "\" maxlength=\"22\" oninput=\"syncDeviceName()\"></div></div>"
        "<script>"
        "window.syncDeviceName=function(){var v=document.getElementById(\"more-device-name\");if(v)document.getElementById(\"form-device-name\").value=v.value};"
        "(function(){var hf=document.getElementById(\"form-device-name\");if(hf&&hf.value)document.getElementById(\"more-device-name\").value=hf.value})();"
        "panelCleanup=function(){};"
        "</script>");
}

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
        "<div class=\"field\" style=\"margin-top:14px\"><label>配速机器人</label>"
        "<select id=\"profile-ride-type\" style=\"width:100%;padding:12px;border:1px solid rgba(0,0,0,.08);border-radius:16px;font-size:15px;font-weight:700;color:#1d1d1f;background:#f5f5f7\" oninput=\"syncProfileRideType()\">"
        "<option value=\"coco\">Coco · 2.4-2.8 W/kg</option>"
        "<option value=\"yumi\">Yumi · 2.7-3.1 W/kg</option>"
        "<option value=\"jacques\">Jacques · 3.0-3.5 W/kg</option>"
        "<option value=\"genie\">Genie · 3.5-3.9 W/kg</option>"
        "<option value=\"constance\">Constance · 4.0-4.4 W/kg</option>"
        "</select></div>"
        "</div>"
        "<div class=\"module-note\">调整后请点\"应用并回首页\"，再在首页点\"保存并重启\"写入设备。</div>"
        "<script>"
        "window.syncProfileWeight=function(){document.getElementById(\"form-profile-weight\").value=document.getElementById(\"profile-weight\").value};"
        "window.syncProfileFtp=function(){document.getElementById(\"form-profile-ftp\").value=document.getElementById(\"profile-ftp\").value};"
        "window.syncProfileThrHr=function(){document.getElementById(\"form-profile-thr-hr\").value=document.getElementById(\"profile-thr-hr\").value};"
        "window.syncProfileRideType=function(){document.getElementById(\"form-profile-ride-type\").value=document.getElementById(\"profile-ride-type\").value};"
        "(function(){var fw=document.getElementById(\"form-profile-weight\");if(fw&&fw.value){document.getElementById(\"profile-weight\").value=fw.value}var ff=document.getElementById(\"form-profile-ftp\");if(ff&&ff.value){document.getElementById(\"profile-ftp\").value=ff.value}var ft=document.getElementById(\"form-profile-thr-hr\");if(ft&&ft.value){document.getElementById(\"profile-thr-hr\").value=ft.value}var fr=document.getElementById(\"form-profile-ride-type\");if(fr&&fr.value){document.getElementById(\"profile-ride-type\").value=fr.value}})();"
        "panelCleanup=function(){};"
        "</script>");
}

static void send_mode_panel(httpd_req_t *req)
{
    SEND(req, PANEL_STYLE
        "<div class=\"panel-card\">"
        "<h2>配速机器人</h2>"
        "<p>设备会根据机器人的 W/kg 区间和你的体重，自动换算目标功率。</p>"
        "<p style=\"margin-top:8px\">心率会逐步接近对应强度，适合模拟跟随稳定骑行团。</p>"
        "</div>"
        "<div class=\"module-note\">此模式下首页的手动数值不生效。要改变输出，请调整个人信息和配速机器人。</div>"
        "<script>panelCleanup=function(){};</script>");
}

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
        "<div class=\"module-note\">此模式下首页的手动功率不生效；非 ERG 模式下 App 无法遥控功率。</div>");
}

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

static esp_err_t h_closing(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>WiFi正在关闭</title></head>"
        "<body style=\"background:#f5f5f7;min-height:100vh;margin:0;padding:24px 16px;display:flex;justify-content:center;align-items:center;font-family:-apple-system,BlinkMacSystemFont,Helvetica Neue,Arial,sans-serif;color:#1d1d1f\">"
        "<div style=\"max-width:360px;width:100%;background:#fff;border:1px solid rgba(0,0,0,.06);border-radius:22px;padding:28px;text-align:center;box-shadow:0 12px 32px rgba(0,0,0,.08)\">"
        "<h1 style=\"color:#007aff;font-size:22px;margin:0 0 14px;font-weight:800\">WiFi 正在关闭</h1>"
        "<p style=\"color:#6e6e73;font-size:14px;line-height:1.6;margin:0\">设备将重启并恢复蓝牙。请回到骑行 App 重新连接。</p>"
        "</div></body></html>");
    return ESP_OK;
}

// ============================ 计时器路由 ============================

static esp_err_t h_time(httpd_req_t *req)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", remaining_seconds());
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t h_renew(httpd_req_t *req)
{
    arm_deadline();
    return h_time(req);
}

// ============================ 启动 ============================

static void start_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap = { 0 };
    strncpy((char *)ap.ap.ssid, WIFI_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(WIFI_SSID);
    strncpy((char *)ap.ap.password, WIFI_PASSWORD, sizeof(ap.ap.password));
    ap.ap.channel = WIFI_AP_CHANNEL;
    ap.ap.max_connection = 3;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP 启动 ssid=%s ip=192.168.4.1", WIFI_SSID);
}

static void start_httpd(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = WIFI_PORT;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 12;
    cfg.stack_size = 8192;

    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd 启动失败");
        return;
    }
    httpd_uri_t routes[] = {
        { .uri = "/",          .method = HTTP_GET,  .handler = h_home },
        { .uri = "/config",    .method = HTTP_POST, .handler = h_config },
        { .uri = "/success",   .method = HTTP_GET,  .handler = h_success },
        { .uri = "/more",      .method = HTTP_GET,  .handler = h_more },
        { .uri = "/draft",     .method = HTTP_POST, .handler = h_draft },
        { .uri = "/ui/module", .method = HTTP_GET,  .handler = h_ui_module },
        { .uri = "/closing",   .method = HTTP_GET,  .handler = h_closing },
        { .uri = "/time",      .method = HTTP_GET,  .handler = h_time },
        { .uri = "/renew",     .method = HTTP_GET,  .handler = h_renew },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_httpd, &routes[i]);
    }
    ESP_LOGI(TAG, "HTTP 配网服务就绪 端口=%d", WIFI_PORT);
}

void wifi_portal_start(power_engine_t *engine)
{
    s_engine = engine;
    s_rebooting = false;

    start_softap();
    start_httpd();
    arm_deadline();

    const esp_timer_create_args_t targs = { .callback = shutdown_check_cb, .name = "portal_to" };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_shutdown_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_shutdown_timer, 1000000));  // 每秒检查超时

    ESP_LOGI(TAG, "WiFi 配网模式就绪，%ds 后自动超时", WIFI_SHUTDOWN_MS / 1000);
}
