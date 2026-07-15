/**
 * 配置 schema 实现 — 见 config_store.h
 */
#include "config_store.h"
#include "json_miniparse.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/*
 * 把配置对象填充为出厂默认值。
 * 参数：c 为待写入默认值的配置对象。
 * 返回值：无。
 * 特殊行为：会统一设置 schema 版本、领域层默认参数和默认设备名，并保证名称以空字符结尾。
 */
void config_store_defaults(config_data_t *c)
{
    c->schema_ver = CONFIG_VERSION;
    c->power = DEFAULT_POWER;
    c->cadence = DEFAULT_CADENCE;
    c->mode = DEFAULT_RIDE_MODE;
    c->heartrate = DEFAULT_HEARTRATE;
    c->has_user_hr = 0;
    c->power_float = POWER_FLOAT_DEFAULT;
    c->cadence_float = CADENCE_FLOAT_DEFAULT;
    c->hr_float = HR_FLOAT_DEFAULT;
    c->power_step = POWER_STEP_DEFAULT;
    c->weight = PROFILE_WEIGHT_DEFAULT;
    c->ftp = PROFILE_FTP_DEFAULT;
    c->thr_hr = PROFILE_THR_HR_DEFAULT;
    c->ride_type = PACER_INDEX_DEFAULT;
    c->custom_wkg_low = CUSTOM_WKG_LOW_DEFAULT;
    c->custom_wkg_high = CUSTOM_WKG_HIGH_DEFAULT;
    strncpy(c->custom_pacer_name, CUSTOM_PACER_NAME_DEFAULT, CUSTOM_PACER_NAME_MAX - 1);
    c->custom_pacer_name[CUSTOM_PACER_NAME_MAX - 1] = '\0';
    led_config_defaults(&c->led);
    c->sport_profile = DEFAULT_SPORT_PROFILE;
    c->broadcast_mask = BROADCAST_FIELD_ALL;
    c->run_speed_x100 = RUN_SPEED_X100_DEFAULT;
    c->run_pace_float_sec = RUN_PACE_FLOAT_SEC_DEFAULT;
    c->run_cadence = RUN_CADENCE_DEFAULT;
    c->run_cadence_float = RUN_CADENCE_FLOAT_DEFAULT;
    c->run_mode = RUN_MODE_DEFAULT;
    c->run_pacer_type = RUN_PACER_DEFAULT;
    c->run_pace_low_sec = RUN_PACERS[RUN_PACER_DEFAULT].pace_low_sec;
    c->run_pace_high_sec = RUN_PACERS[RUN_PACER_DEFAULT].pace_high_sec;
    strncpy(c->run_custom_name, RUN_CUSTOM_NAME_DEFAULT, RUN_CUSTOM_NAME_MAX - 1);
    c->run_custom_name[RUN_CUSTOM_NAME_MAX - 1] = '\0';
    strncpy(c->device_name, DEVICE_NAME_DEFAULT, DEVICE_NAME_MAX - 1);
    c->device_name[DEVICE_NAME_MAX - 1] = '\0';
}

/*
 * 校验并修正配置对象中的各字段范围。
 * 参数：c 为待校验的配置对象。
 * 返回值：若任一字段被修正则返回 true，否则返回 false。
 * 特殊行为：非法模式和 ride_type 会回退到默认值；设备名为空时会恢复默认名称，并始终补齐末尾终止符。
 */
bool config_store_validate(config_data_t *c)
{
    config_data_t in = *c;

    c->power = clamp_int(c->power, POWER_MIN, POWER_MAX);
    c->cadence = clamp_int(c->cadence, CADENCE_MIN, CADENCE_MAX);
    if (c->mode < 0 || c->mode >= RIDE_MODE_COUNT) c->mode = DEFAULT_RIDE_MODE;
    c->heartrate = clamp_int(c->heartrate, HR_MIN, HR_MAX);
    c->has_user_hr = c->has_user_hr ? 1 : 0;
    c->power_float = clamp_int(c->power_float, POWER_FLOAT_MIN, POWER_FLOAT_MAX);
    c->cadence_float = clamp_int(c->cadence_float, CADENCE_FLOAT_MIN, CADENCE_FLOAT_MAX);
    c->hr_float = clamp_int(c->hr_float, HR_FLOAT_MIN, HR_FLOAT_MAX);
    c->power_step = clamp_int(c->power_step, POWER_STEP_MIN, POWER_STEP_MAX);
    c->weight = clamp_int(c->weight, PROFILE_WEIGHT_MIN, PROFILE_WEIGHT_MAX);
    c->ftp = clamp_int(c->ftp, PROFILE_FTP_MIN, PROFILE_FTP_MAX);
    c->thr_hr = clamp_int(c->thr_hr, PROFILE_THR_HR_MIN, PROFILE_THR_HR_MAX);
    if (!PACER_INDEX_IS_VALID(c->ride_type)) c->ride_type = PACER_INDEX_DEFAULT;
    c->custom_wkg_low = clamp_int(c->custom_wkg_low, CUSTOM_WKG_MIN, CUSTOM_WKG_MAX);
    c->custom_wkg_high = clamp_int(c->custom_wkg_high, CUSTOM_WKG_MIN, CUSTOM_WKG_MAX);
    if (c->custom_wkg_low > c->custom_wkg_high) c->custom_wkg_high = c->custom_wkg_low;
    c->custom_pacer_name[CUSTOM_PACER_NAME_MAX - 1] = '\0';
    if (c->custom_pacer_name[0] == '\0') {
        strncpy(c->custom_pacer_name, CUSTOM_PACER_NAME_DEFAULT, CUSTOM_PACER_NAME_MAX - 1);
    }
    led_config_validate(&c->led);
    if (c->sport_profile < 0 || c->sport_profile >= SPORT_PROFILE_COUNT) c->sport_profile = DEFAULT_SPORT_PROFILE;
    c->broadcast_mask &= BROADCAST_FIELD_ALL;
    if (c->broadcast_mask == 0) c->broadcast_mask = BROADCAST_FIELD_ALL;
    c->run_speed_x100 = clamp_int(c->run_speed_x100, RUN_SPEED_X100_MIN, RUN_SPEED_X100_MAX);
    c->run_pace_float_sec = clamp_int(c->run_pace_float_sec, RUN_PACE_FLOAT_SEC_MIN, RUN_PACE_FLOAT_SEC_MAX);
    c->run_cadence = clamp_int(c->run_cadence, RUN_CADENCE_MIN, RUN_CADENCE_MAX);
    c->run_cadence_float = clamp_int(c->run_cadence_float, RUN_CADENCE_FLOAT_MIN, RUN_CADENCE_FLOAT_MAX);
    if (c->run_mode < 0 || c->run_mode >= RUN_MODE_COUNT) c->run_mode = RUN_MODE_DEFAULT;
    if (c->run_pacer_type < 0 || c->run_pacer_type >= RUN_PACER_COUNT) c->run_pacer_type = RUN_PACER_DEFAULT;
    c->run_pace_low_sec = clamp_int(c->run_pace_low_sec, RUN_PACE_SEC_MIN, RUN_PACE_SEC_MAX);
    c->run_pace_high_sec = clamp_int(c->run_pace_high_sec, RUN_PACE_SEC_MIN, RUN_PACE_SEC_MAX);
    if (c->run_pace_low_sec > c->run_pace_high_sec) c->run_pace_high_sec = c->run_pace_low_sec;
    c->run_custom_name[RUN_CUSTOM_NAME_MAX - 1] = '\0';
    if (c->run_custom_name[0] == '\0') {
        strncpy(c->run_custom_name, RUN_CUSTOM_NAME_DEFAULT, RUN_CUSTOM_NAME_MAX - 1);
    }

    c->device_name[DEVICE_NAME_MAX - 1] = '\0';
    if (c->device_name[0] == '\0') {
        strncpy(c->device_name, DEVICE_NAME_DEFAULT, DEVICE_NAME_MAX - 1);
    }

    return in.power != c->power
        || in.cadence != c->cadence
        || in.mode != c->mode
        || in.heartrate != c->heartrate
        || in.has_user_hr != c->has_user_hr
        || in.power_float != c->power_float
        || in.cadence_float != c->cadence_float
        || in.hr_float != c->hr_float
        || in.power_step != c->power_step
        || in.weight != c->weight
        || in.ftp != c->ftp
        || in.thr_hr != c->thr_hr
        || in.ride_type != c->ride_type
        || in.custom_wkg_low != c->custom_wkg_low
        || in.custom_wkg_high != c->custom_wkg_high
        || strncmp(in.custom_pacer_name, c->custom_pacer_name, CUSTOM_PACER_NAME_MAX) != 0
        || memcmp(&in.led, &c->led, sizeof(c->led)) != 0
        || in.sport_profile != c->sport_profile
        || in.broadcast_mask != c->broadcast_mask
        || in.run_speed_x100 != c->run_speed_x100
        || in.run_pace_float_sec != c->run_pace_float_sec
        || in.run_cadence != c->run_cadence
        || in.run_cadence_float != c->run_cadence_float
        || in.run_mode != c->run_mode
        || in.run_pacer_type != c->run_pacer_type
        || in.run_pace_low_sec != c->run_pace_low_sec
        || in.run_pace_high_sec != c->run_pace_high_sec
        || strncmp(in.run_custom_name, c->run_custom_name, RUN_CUSTOM_NAME_MAX) != 0
        || strncmp(in.device_name, c->device_name, DEVICE_NAME_MAX) != 0;
}

/*
 * 将旧版本配置迁移到当前 schema。
 * 参数：c 为待迁移的配置对象。
 * 返回值：若执行了迁移返回 true，否则返回 false。
 * 特殊行为：当前没有细粒度历史迁移逻辑，schema 不匹配时会直接回退为整套默认配置。
 */
bool config_store_migrate(config_data_t *c)
{
    if (c->schema_ver == CONFIG_VERSION) {
        return false;
    }
    if (c->schema_ver == 6) {
        c->schema_ver = 7;
        c->run_pace_float_sec = RUN_PACE_FLOAT_SEC_DEFAULT;
    }
    if (c->schema_ver == 7) {
        c->schema_ver = 8;
        c->run_cadence_float = RUN_CADENCE_FLOAT_DEFAULT;
    }
    if (c->schema_ver == 8) {
        c->schema_ver = 9;
        c->broadcast_mask = BROADCAST_FIELD_ALL;
    }
    if (c->schema_ver == 9) {
        c->schema_ver = 10;
        c->run_mode = RUN_MODE_DEFAULT;
        c->run_pacer_type = RUN_PACER_DEFAULT;
        c->run_pace_low_sec = RUN_PACERS[RUN_PACER_DEFAULT].pace_low_sec;
        c->run_pace_high_sec = RUN_PACERS[RUN_PACER_DEFAULT].pace_high_sec;
        strncpy(c->run_custom_name, RUN_CUSTOM_NAME_DEFAULT, RUN_CUSTOM_NAME_MAX - 1);
        c->run_custom_name[RUN_CUSTOM_NAME_MAX - 1] = '\0';
    }
    if (c->schema_ver == 10) {
        c->schema_ver = CONFIG_VERSION;
        config_store_validate(c);
        return true;
    }
    config_store_defaults(c);
    return true;
}

/* 把字符串按 JSON 规则转义写入 dst（含两端不含引号）。
 * 返回写入字符数；若空间不足（含结尾 '\0' 预留由调用方保证）则返回 (size_t)-1。 */
static size_t json_escape(const char *src, char *dst, size_t dst_sz)
{
    size_t n = 0;
    for (const char *p = src; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        char tmp[8];
        const char *seg;
        size_t seglen;
        switch (ch) {
        case '"':  seg = "\\\""; seglen = 2; break;
        case '\\': seg = "\\\\"; seglen = 2; break;
        case '\b': seg = "\\b";  seglen = 2; break;
        case '\f': seg = "\\f";  seglen = 2; break;
        case '\n': seg = "\\n";  seglen = 2; break;
        case '\r': seg = "\\r";  seglen = 2; break;
        case '\t': seg = "\\t";  seglen = 2; break;
        default:
            if (ch < 0x20) {
                snprintf(tmp, sizeof(tmp), "\\u%04x", ch);
                seg = tmp; seglen = 6;
            } else {
                tmp[0] = (char)ch; tmp[1] = '\0';
                seg = tmp; seglen = 1;
            }
            break;
        }
        if (n + seglen >= dst_sz) return (size_t)-1;
        memcpy(dst + n, seg, seglen);
        n += seglen;
    }
    if (n >= dst_sz) return (size_t)-1;
    dst[n] = '\0';
    return n;
}

/* 把 JSON 字符串值反转义到 dst。src 指向值首字符（引号之后），
 * 解析到未转义的 '"' 结束。返回 true 表示成功。 */
static bool json_unescape(const char *src, char *dst, size_t dst_sz)
{
    size_t n = 0;
    for (const char *p = src; *p && *p != '"'; p++) {
        char out;
        if (*p == '\\') {
            p++;
            switch (*p) {
            case '"':  out = '"';  break;
            case '\\': out = '\\'; break;
            case '/':  out = '/';  break;
            case 'b':  out = '\b'; break;
            case 'f':  out = '\f'; break;
            case 'n':  out = '\n'; break;
            case 'r':  out = '\r'; break;
            case 't':  out = '\t'; break;
            case 'u': {
                /* \uXXXX：仅支持 00-FF 范围（设备名为 ASCII），高字节忽略 */
                char hex[5] = {0};
                for (int i = 0; i < 4 && p[1]; i++) hex[i] = *(++p);
                out = (char)(strtol(hex, NULL, 16) & 0xFF);
                break;
            }
            case '\0': return false;  /* 反斜杠后截断 */
            default:   out = *p;      break;
            }
        } else {
            out = *p;
        }
        if (n + 1 >= dst_sz) break;  /* 截断保护，保留结尾 '\0' */
        dst[n++] = out;
    }
    dst[n] = '\0';
    return true;
}

size_t config_store_serialize(const config_data_t *c, char *out, size_t out_sz)
{
    if (c == NULL || out == NULL || out_sz == 0) return 0;

    /* 字符串转义后最大膨胀 6 倍（\u00XX），分别按缓冲区大小预留 */
    char custom_esc[CUSTOM_PACER_NAME_MAX * 6];
    if (json_escape(c->custom_pacer_name, custom_esc, sizeof(custom_esc)) == (size_t)-1) return 0;
    char run_custom_esc[RUN_CUSTOM_NAME_MAX * 6];
    if (json_escape(c->run_custom_name, run_custom_esc, sizeof(run_custom_esc)) == (size_t)-1) return 0;
    char esc[DEVICE_NAME_MAX * 6];
    if (json_escape(c->device_name, esc, sizeof(esc)) == (size_t)-1) return 0;

    /* 先写入栈缓冲，确认总长后再拷贝出参，避免向 out 写截断内容 */
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
        "{\"schema_ver\":%d,\"power\":%d,\"cadence\":%d,\"mode\":%d,"
        "\"heartrate\":%d,\"has_user_hr\":%d,"
        "\"power_float\":%d,\"cadence_float\":%d,\"hr_float\":%d,"
        "\"power_step\":%d,"
        "\"weight\":%d,\"ftp\":%d,"
        "\"thr_hr\":%d,\"ride_type\":%d,"
        "\"custom_wkg_low\":%d,\"custom_wkg_high\":%d,"
        "\"custom_pacer_name\":\"%s\","
        "\"led_silent\":%d,"
        "\"led_wait_mode\":%d,\"led_wait_color\":%d,"
        "\"led_conn_mode\":%d,\"led_conn_color\":%d,"
        "\"led_wifi_mode\":%d,\"led_wifi_color\":%d,"
        "\"led_confirm_mode\":%d,\"led_confirm_color\":%d,"
        "\"led_error_mode\":%d,\"led_error_color\":%d,"
        "\"sport_profile\":%d,\"broadcast_mask\":%d,\"run_speed_x100\":%d,"
        "\"run_pace_float_sec\":%d,"
        "\"run_cadence\":%d,\"run_cadence_float\":%d,"
        "\"run_mode\":%d,\"run_pacer_type\":%d,"
        "\"run_pace_low_sec\":%d,\"run_pace_high_sec\":%d,"
        "\"run_custom_name\":\"%s\","
        "\"device_name\":\"%s\"}",
        c->schema_ver, c->power, c->cadence, c->mode,
        c->heartrate, c->has_user_hr,
        c->power_float, c->cadence_float, c->hr_float,
        c->power_step,
        c->weight, c->ftp,
        c->thr_hr, c->ride_type,
        c->custom_wkg_low, c->custom_wkg_high,
        custom_esc,
        c->led.silent,
        c->led.scenes[LED_SCENE_WAIT_CONN].mode, c->led.scenes[LED_SCENE_WAIT_CONN].color,
        c->led.scenes[LED_SCENE_CONNECTED].mode, c->led.scenes[LED_SCENE_CONNECTED].color,
        c->led.scenes[LED_SCENE_WIFI_CONFIG].mode, c->led.scenes[LED_SCENE_WIFI_CONFIG].color,
        c->led.scenes[LED_SCENE_WIFI_CONFIRM].mode, c->led.scenes[LED_SCENE_WIFI_CONFIRM].color,
        c->led.scenes[LED_SCENE_ERROR].mode, c->led.scenes[LED_SCENE_ERROR].color,
        c->sport_profile, c->broadcast_mask, c->run_speed_x100,
        c->run_pace_float_sec,
        c->run_cadence, c->run_cadence_float,
        c->run_mode, c->run_pacer_type,
        c->run_pace_low_sec, c->run_pace_high_sec,
        run_custom_esc,
        esc);

    if (n < 0 || (size_t)n >= sizeof(buf)) return 0;  /* 编码错误或内部缓冲不足 */
    if ((size_t)n >= out_sz) return 0;                /* 出参缓冲不足，不溢出 */
    memcpy(out, buf, (size_t)n + 1);
    return (size_t)n;
}

bool config_store_deserialize(const char *json, config_data_t *c)
{
    if (json == NULL || c == NULL) return false;
    /* 必备结构校验：缺 schema_ver 键视为非法 JSON，避免静默落默认配置 */
    if (strstr(json, "\"schema_ver\":") == NULL) return false;

    /* 解析到临时对象，成功才写出，失败不污染调用方的 c */
    config_data_t tmp;
    config_store_defaults(&tmp);

    /* 仅覆盖 JSON 中出现的整数键，缺键字段保持默认 */
    struct { const char *key; int *field; } int_keys[] = {
        {"schema_ver", &tmp.schema_ver}, {"power", &tmp.power},
        {"cadence", &tmp.cadence}, {"mode", &tmp.mode},
        {"heartrate", &tmp.heartrate}, {"has_user_hr", &tmp.has_user_hr},
        {"power_float", &tmp.power_float}, {"cadence_float", &tmp.cadence_float},
        {"hr_float", &tmp.hr_float},
        {"power_step", &tmp.power_step},
        {"weight", &tmp.weight}, {"ftp", &tmp.ftp},
        {"thr_hr", &tmp.thr_hr}, {"ride_type", &tmp.ride_type},
        {"custom_wkg_low", &tmp.custom_wkg_low}, {"custom_wkg_high", &tmp.custom_wkg_high},
        {"sport_profile", &tmp.sport_profile},
        {"broadcast_mask", &tmp.broadcast_mask},
        {"run_speed_x100", &tmp.run_speed_x100},
        {"run_pace_float_sec", &tmp.run_pace_float_sec},
        {"run_cadence", &tmp.run_cadence},
        {"run_cadence_float", &tmp.run_cadence_float},
        {"run_mode", &tmp.run_mode},
        {"run_pacer_type", &tmp.run_pacer_type},
        {"run_pace_low_sec", &tmp.run_pace_low_sec},
        {"run_pace_high_sec", &tmp.run_pace_high_sec},
    };
    for (size_t i = 0; i < sizeof(int_keys) / sizeof(int_keys[0]); i++) {
        char pat[40];
        snprintf(pat, sizeof(pat), "\"%s\":", int_keys[i].key);
        if (strstr(json, pat)) {
            *int_keys[i].field = json_get_int(json, int_keys[i].key);
        }
    }

    struct { const char *key; uint8_t *field; } led_keys[] = {
        {"led_silent", &tmp.led.silent},
        {"led_wait_mode", &tmp.led.scenes[LED_SCENE_WAIT_CONN].mode},
        {"led_wait_color", &tmp.led.scenes[LED_SCENE_WAIT_CONN].color},
        {"led_conn_mode", &tmp.led.scenes[LED_SCENE_CONNECTED].mode},
        {"led_conn_color", &tmp.led.scenes[LED_SCENE_CONNECTED].color},
        {"led_wifi_mode", &tmp.led.scenes[LED_SCENE_WIFI_CONFIG].mode},
        {"led_wifi_color", &tmp.led.scenes[LED_SCENE_WIFI_CONFIG].color},
        {"led_confirm_mode", &tmp.led.scenes[LED_SCENE_WIFI_CONFIRM].mode},
        {"led_confirm_color", &tmp.led.scenes[LED_SCENE_WIFI_CONFIRM].color},
        {"led_error_mode", &tmp.led.scenes[LED_SCENE_ERROR].mode},
        {"led_error_color", &tmp.led.scenes[LED_SCENE_ERROR].color},
    };
    for (size_t i = 0; i < sizeof(led_keys) / sizeof(led_keys[0]); i++) {
        char pat[48];
        snprintf(pat, sizeof(pat), "\"%s\":", led_keys[i].key);
        if (strstr(json, pat)) {
            *led_keys[i].field = (uint8_t)json_get_int(json, led_keys[i].key);
        }
    }

    const char *cp = strstr(json, "\"custom_pacer_name\":\"");
    if (cp) {
        cp += strlen("\"custom_pacer_name\":\"");
        json_unescape(cp, tmp.custom_pacer_name, CUSTOM_PACER_NAME_MAX);
    }

    const char *rp = strstr(json, "\"run_custom_name\":\"");
    if (rp) {
        rp += strlen("\"run_custom_name\":\"");
        json_unescape(rp, tmp.run_custom_name, RUN_CUSTOM_NAME_MAX);
    }

    /* device_name：定位值起点后做反转义（json_get_str 不处理转义） */
    const char *dp = strstr(json, "\"device_name\":\"");
    if (dp) {
        dp += strlen("\"device_name\":\"");
        json_unescape(dp, tmp.device_name, DEVICE_NAME_MAX);
    }

    config_store_validate(&tmp);
    *c = tmp;
    return true;
}
