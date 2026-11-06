/**
 * 配置 schema 实现 — 见 config_store.h
 */
#include "config_store.h"
#include "json_miniparse.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

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
    power_bridge_config_defaults(&c->bridge);
}

void config_store_from_engine(const power_engine_t *e, config_data_t *c)
{
    if (!e || !c) return;
    memset(c, 0, sizeof(*c));
    c->schema_ver = CONFIG_VERSION;
    c->power = e->base_power;
    c->cadence = e->cadence_base;
    c->mode = e->mode == RIDE_MODE_ERG ? RIDE_MODE_STEADY : (int)e->mode;
    c->heartrate = e->has_user_hr ? e->user_heartrate : DEFAULT_HEARTRATE;
    c->has_user_hr = e->has_user_hr ? 1 : 0;
    c->power_float = e->power_float;
    c->cadence_float = e->cadence_float;
    c->hr_float = e->hr_float;
    c->power_step = e->power_step;
    c->weight = e->profile.weight;
    c->ftp = e->profile.ftp;
    c->thr_hr = e->profile.thr_hr;
    c->ride_type = e->profile.ride_type;
    c->custom_wkg_low = e->profile.custom_wkg_low;
    c->custom_wkg_high = e->profile.custom_wkg_high;
    strncpy(c->custom_pacer_name, e->profile.custom_pacer_name, CUSTOM_PACER_NAME_MAX - 1);
    c->custom_pacer_name[CUSTOM_PACER_NAME_MAX - 1] = '\0';
    c->led = e->led;
    c->sport_profile = (int)e->sport_profile;
    c->broadcast_mask = e->broadcast_mask;
    c->run_speed_x100 = e->running.speed_x100_kmh;
    c->run_pace_float_sec = e->running.pace_float_sec;
    c->run_cadence = e->running.cadence_spm;
    c->run_cadence_float = e->running.cadence_float_spm;
    c->run_mode = e->running.mode;
    c->run_pacer_type = e->running.pacer_type;
    c->run_pace_low_sec = e->running.pace_low_sec;
    c->run_pace_high_sec = e->running.pace_high_sec;
    strncpy(c->run_custom_name, e->running.custom_name, RUN_CUSTOM_NAME_MAX - 1);
    c->run_custom_name[RUN_CUSTOM_NAME_MAX - 1] = '\0';
    strncpy(c->device_name, e->device_name, DEVICE_NAME_MAX - 1);
    c->device_name[DEVICE_NAME_MAX - 1] = '\0';
    c->bridge = e->bridge;
}

bool config_store_apply_to_engine(const config_data_t *c, power_engine_t *e)
{
    if (!c || !e) return false;

    config_data_t checked = *c;
    if (config_store_validate(&checked)) return false;

    power_engine_t next = *e;
    bool restore_erg = next.has_erg_target && checked.mode == RIDE_MODE_STEADY
        && checked.bridge.source == BRIDGE_SOURCE_SIMULATED;
    int erg_target = next.erg_active_target;

    if (next.mode != RIDE_MODE_STEADY) {
        power_engine_set_mode(&next, RIDE_MODE_STEADY);
    }
    power_engine_set_power(&next, checked.power);
    power_engine_set_cadence(&next, checked.cadence);
    power_engine_set_power_float(&next, checked.power_float);
    power_engine_set_cadence_float(&next, checked.cadence_float);
    power_engine_set_hr_float(&next, checked.hr_float);
    power_engine_set_power_step(&next, checked.power_step);
    if (checked.has_user_hr) {
        power_engine_set_heartrate(&next, checked.heartrate);
    } else {
        power_engine_clear_user_hr(&next);
    }

    profile_set_weight(&next.profile, checked.weight);
    profile_set_ftp(&next.profile, checked.ftp);
    profile_set_thr_hr(&next.profile, checked.thr_hr);
    profile_set_custom_wkg(&next.profile, checked.custom_wkg_low, checked.custom_wkg_high);
    profile_set_custom_name(&next.profile, checked.custom_pacer_name);
    profile_set_ride_type(&next.profile, checked.ride_type);
    next.led = checked.led;

    power_engine_set_sport_profile(&next, (sport_profile_t)checked.sport_profile);
    power_engine_set_broadcast_mask(&next, checked.broadcast_mask);
    power_engine_set_run_speed(&next, checked.run_speed_x100);
    power_engine_set_run_pace_float(&next, checked.run_pace_float_sec);
    power_engine_set_run_cadence_float(&next, checked.run_cadence_float);
    if (checked.run_pacer_type == RUN_PACER_CUSTOM) {
        power_engine_set_run_custom_pacer(&next, checked.run_custom_name,
                                          checked.run_pace_low_sec,
                                          checked.run_pace_high_sec,
                                          checked.run_cadence);
    } else {
        power_engine_set_run_pacer(&next, (run_pacer_type_t)checked.run_pacer_type);
        power_engine_set_run_pace_range(&next, checked.run_pace_low_sec,
                                        checked.run_pace_high_sec);
    }
    power_engine_set_run_mode(&next, (run_mode_t)checked.run_mode);
    power_engine_set_run_cadence(&next, checked.run_cadence);
    power_engine_set_device_name(&next, checked.device_name);
    power_engine_set_mode(&next, (ride_mode_t)checked.mode);
    next.bridge = checked.bridge;
    if (checked.bridge.source == BRIDGE_SOURCE_REAL) power_engine_clear_erg_target(&next);
    if (restore_erg) power_engine_set_erg_target(&next, erg_target);
    next.dirty = true;
    *e = next;
    return true;
}

/*
 * 校验并修正配置对象中的各字段范围。
 * 参数：c 为待校验的配置对象。
 * 返回值：若任一字段被修正则返回 true，否则返回 false。
 * 特殊行为：非法模式和 ride_type 会回退到默认值；设备名为空时会恢复默认名称，并始终补齐末尾终止符。
 */
bool config_store_validate(config_data_t *c)
{
    if (!c) return false;
    config_data_t in = *c;
    bool bridge_fixed = power_bridge_config_validate(&c->bridge);

    c->power = clamp_int(c->power, POWER_MIN, POWER_MAX);
    c->cadence = clamp_int(c->cadence, CADENCE_MIN, CADENCE_MAX);
    if (c->mode < 0 || c->mode >= RIDE_MODE_COUNT) {
        c->mode = DEFAULT_RIDE_MODE;
    } else if (c->mode == RIDE_MODE_ERG) {
        c->mode = RIDE_MODE_STEADY;
    }
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

    return bridge_fixed || in.power != c->power
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
 * 特殊行为：已知版本逐级迁移并保留用户配置；未知版本回退为整套默认配置。
 * v11 到 v12 仅替换历史默认的 ±5W，其他功率浮动值视为用户自定义并保留。
 */
bool config_store_migrate(config_data_t *c)
{
    if (!c) return false;
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
        c->schema_ver = 11;
    }
    if (c->schema_ver == 11) {
        c->schema_ver = 12;
        if (c->power_float == POWER_FLOAT_LEGACY_DEFAULT) {
            c->power_float = POWER_FLOAT_DEFAULT;
        }
    }
    if (c->schema_ver == 12) {
        c->schema_ver = 13;
        power_bridge_config_defaults(&c->bridge);
    }
    if (c->schema_ver == 13) {
        c->schema_ver = 14;
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
static bool json_hex4(const char **cursor, uint32_t *value)
{
    const char *p = *cursor;
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned char ch = (unsigned char)*p;
        unsigned digit;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
        else return false;
        result = (result << 4) | digit;
        p++;
    }
    *cursor = p;
    *value = result;
    return true;
}

static bool json_unescape(const char *src, char *dst, size_t dst_sz)
{
    if (!src || !dst || dst_sz == 0) return false;
    size_t n = 0;
    const char *p = src;
    while (*p && *p != '"') {
        unsigned char bytes[4];
        size_t length = 1;
        unsigned char ch = (unsigned char)*p++;
        if (ch < 0x20) goto invalid;
        if (ch == '\\') {
            ch = (unsigned char)*p;
            if (!ch) goto invalid;
            p++;
            switch (ch) {
            case '"': case '\\': case '/': bytes[0] = ch; break;
            case 'b': bytes[0] = '\b'; break;
            case 'f': bytes[0] = '\f'; break;
            case 'n': bytes[0] = '\n'; break;
            case 'r': bytes[0] = '\r'; break;
            case 't': bytes[0] = '\t'; break;
            case 'u': {
                uint32_t value;
                if (!json_hex4(&p, &value)) goto invalid;
                if (value >= 0xd800 && value <= 0xdbff) {
                    if (*p != '\\') goto invalid;
                    p++;
                    if (*p != 'u') goto invalid;
                    p++;
                    uint32_t low;
                    if (!json_hex4(&p, &low) || low < 0xdc00 || low > 0xdfff) goto invalid;
                    value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
                } else if (value >= 0xdc00 && value <= 0xdfff) {
                    goto invalid;
                }
                if (!value) goto invalid;
                if (value < 0x80) {
                    bytes[0] = (unsigned char)value;
                } else if (value < 0x800) {
                    length = 2;
                    bytes[0] = (unsigned char)(0xc0 | (value >> 6));
                    bytes[1] = (unsigned char)(0x80 | (value & 0x3f));
                } else if (value < 0x10000) {
                    length = 3;
                    bytes[0] = (unsigned char)(0xe0 | (value >> 12));
                    bytes[1] = (unsigned char)(0x80 | ((value >> 6) & 0x3f));
                    bytes[2] = (unsigned char)(0x80 | (value & 0x3f));
                } else {
                    length = 4;
                    bytes[0] = (unsigned char)(0xf0 | (value >> 18));
                    bytes[1] = (unsigned char)(0x80 | ((value >> 12) & 0x3f));
                    bytes[2] = (unsigned char)(0x80 | ((value >> 6) & 0x3f));
                    bytes[3] = (unsigned char)(0x80 | (value & 0x3f));
                }
                break;
            }
            default: goto invalid;
            }
        } else {
            bytes[0] = ch;
        }
        if (length >= dst_sz - n) goto invalid;
        memcpy(dst + n, bytes, length);
        n += length;
    }
    if (*p != '"') goto invalid;
    dst[n] = '\0';
    return true;
invalid:
    dst[0] = '\0';
    return false;
}

static bool append_format(char *buf, size_t size, size_t *offset,
                          const char *format, ...)
{
    if (!buf || !offset || *offset >= size) return false;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buf + *offset, size - *offset, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= size - *offset) return false;
    *offset += (size_t)written;
    return true;
}

static bool format_bridge_devices(const bridge_config_t *bridge,
                                  char *out, size_t out_size)
{
    size_t offset = 0;
    if (!append_format(out, out_size, &offset, "[")) return false;
    for (int i = 0; i < bridge->device_count; i++) {
        const bridge_device_t *device = &bridge->devices[i];
        char name[BRIDGE_NAME_MAX * 6];
        if (json_escape(device->name, name, sizeof(name)) == (size_t)-1) return false;
        char address[13];
        for (size_t j = 0; j < sizeof(device->addr); j++) {
            snprintf(address + j * 2, 3, "%02x", device->addr[j]);
        }
        if (!append_format(out, out_size, &offset,
                           "%s{\"addr_type\":%d,\"address\":\"%s\",\"name\":\"%s\"}",
                           i ? "," : "", device->addr_type, address, name)) {
            return false;
        }
    }
    return append_format(out, out_size, &offset, "]");
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
    char bridge_devices[BRIDGE_DEVICE_MAX * (BRIDGE_NAME_MAX * 6 + 80) + 3];
    if (!format_bridge_devices(&c->bridge, bridge_devices, sizeof(bridge_devices))) return 0;

    const bridge_device_t *primary = power_bridge_device(&c->bridge, 0);
    bridge_device_t empty_primary = {0};
    if (!primary) primary = &empty_primary;
    char bridge_esc[BRIDGE_NAME_MAX * 6];
    if (json_escape(primary->name, bridge_esc, sizeof(bridge_esc)) == (size_t)-1) return 0;
    char address[13];
    for (size_t i = 0; i < sizeof(primary->addr); i++) {
        snprintf(address + i * 2, 3, "%02x", primary->addr[i]);
    }

    /* 先写入栈缓冲，确认总长后再拷贝出参，避免向 out 写截断内容 */
    char buf[CONFIG_JSON_MAX_LEN];
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
        "\"device_name\":\"%s\","
        "\"cycling_source\":%d,\"bridge_calibration\":%d,"
        "\"bridge_watts\":%d,\"bridge_percent_x10\":%d,"
        "\"bridge_devices\":%s,"
        "\"bridge_bound\":%d,\"bridge_addr_type\":%d,"
        "\"bridge_address\":\"%s\",\"bridge_name\":\"%s\"}",
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
        esc, c->bridge.source, c->bridge.calibration_mode,
        c->bridge.watts, c->bridge.percent_x10, bridge_devices,
        primary->bound, primary->addr_type, address, bridge_esc);

    if (n < 0 || (size_t)n >= sizeof(buf)) return 0;  /* 编码错误或内部缓冲不足 */
    if ((size_t)n >= out_sz) return 0;                /* 出参缓冲不足，不溢出 */
    memcpy(out, buf, (size_t)n + 1);
    return (size_t)n;
}

static const char *json_string_value(const char *json, const char *key)
{
    char pattern[48];
    int written = snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    if (written < 0 || (size_t)written >= sizeof(pattern)) return NULL;
    const char *value = strstr(json, pattern);
    return value ? value + strlen(pattern) : NULL;
}

static bool parse_bridge_address(const char *value, uint8_t address[6])
{
    if (!value || strlen(value) < 13 || value[12] != '"') return false;
    for (size_t i = 0; i < 6; i++) {
        char hex[3] = {value[i * 2], value[i * 2 + 1], 0};
        char *end = NULL;
        long byte = strtol(hex, &end, 16);
        if (end != hex + 2 || byte < 0 || byte > UINT8_MAX) return false;
        address[i] = (uint8_t)byte;
    }
    return true;
}

static bool bridge_device_valid(const bridge_device_t *device)
{
    bridge_config_t check;
    power_bridge_config_defaults(&check);
    check.source = BRIDGE_SOURCE_REAL;
    check.device_count = 1;
    check.devices[0] = *device;
    return !power_bridge_config_validate(&check) && check.device_count == 1;
}

static bool parse_bridge_device(const char *json, bridge_device_t *device)
{
    if (!json || !device || !strstr(json, "\"addr_type\":")
        || !strstr(json, "\"address\":\"") || !strstr(json, "\"name\":\"")) {
        return false;
    }
    bridge_device_t parsed = {
        .bound = 1,
        .addr_type = json_get_int(json, "addr_type"),
    };
    if (!parse_bridge_address(json_string_value(json, "address"), parsed.addr)
        || !json_unescape(json_string_value(json, "name"),
                          parsed.name, sizeof(parsed.name))
        || !bridge_device_valid(&parsed)) {
        return false;
    }
    *device = parsed;
    return true;
}

static bool parse_bridge_devices(const char *json, bridge_config_t *bridge)
{
    int count = json_get_object_array_count(json, "bridge_devices");
    if (count < 0 || count > BRIDGE_DEVICE_MAX) return false;
    bridge->device_count = 0;
    memset(bridge->devices, 0, sizeof(bridge->devices));
    for (int i = 0; i < count; i++) {
        char item[BRIDGE_NAME_MAX * 6 + 96];
        bridge_device_t device;
        if (!json_get_object_array_item(json, "bridge_devices", i,
                                        item, sizeof(item))
            || !parse_bridge_device(item, &device)
            || power_bridge_find_device(bridge, &device) >= 0) {
            return false;
        }
        bridge->devices[bridge->device_count++] = device;
    }
    return true;
}

static bool parse_legacy_bridge(const char *json, bridge_config_t *bridge)
{
    if (!strstr(json, "\"bridge_bound\":")) return true;
    int bound = json_get_int(json, "bridge_bound");
    if (bound == 0) return true;
    if (bound != 1 || !strstr(json, "\"bridge_addr_type\":")
        || !strstr(json, "\"bridge_address\":\"")) {
        return false;
    }
    bridge_device_t device = {
        .bound = 1,
        .addr_type = json_get_int(json, "bridge_addr_type"),
    };
    if (!parse_bridge_address(json_string_value(json, "bridge_address"), device.addr)) {
        return false;
    }
    const char *name = json_string_value(json, "bridge_name");
    if (name && !json_unescape(name, device.name, sizeof(device.name))) return false;
    if (!bridge_device_valid(&device)) return false;
    bridge->device_count = 1;
    bridge->devices[0] = device;
    return true;
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
        {"cycling_source", &tmp.bridge.source},
        {"bridge_calibration", &tmp.bridge.calibration_mode},
        {"bridge_watts", &tmp.bridge.watts},
        {"bridge_percent_x10", &tmp.bridge.percent_x10},
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
        if (!json_unescape(cp, tmp.custom_pacer_name, CUSTOM_PACER_NAME_MAX)) return false;
    }
    const char *rp = strstr(json, "\"run_custom_name\":\"");
    if (rp) {
        rp += strlen("\"run_custom_name\":\"");
        if (!json_unescape(rp, tmp.run_custom_name, RUN_CUSTOM_NAME_MAX)) return false;
    }

    /* device_name：定位值起点后做反转义（json_get_str 不处理转义） */
    const char *dp = strstr(json, "\"device_name\":\"");
    if (dp) {
        dp += strlen("\"device_name\":\"");
        if (!json_unescape(dp, tmp.device_name, DEVICE_NAME_MAX)) return false;
    }

    if (tmp.schema_ver == CONFIG_VERSION) {
        if (!parse_bridge_devices(json, &tmp.bridge)) return false;
    } else if (tmp.schema_ver <= 13) {
        if (!parse_legacy_bridge(json, &tmp.bridge)) return false;
    }

    config_store_migrate(&tmp);
    config_store_validate(&tmp);
    *c = tmp;
    return true;
}
