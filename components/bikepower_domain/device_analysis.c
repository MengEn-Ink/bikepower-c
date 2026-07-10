/**
 * 外部 BLE 运动设备分析纯逻辑实现。
 */
#include "device_analysis.h"

#include <stdio.h>
#include <string.h>

static uint16_t le16u(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int16_t le16s(const uint8_t *p)
{
    return (int16_t)le16u(p);
}

static void bytes_to_hex(const uint8_t *data, size_t len, char *out, size_t out_sz)
{
    static const char hex[] = "0123456789abcdef";
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!data || len == 0) return;
    size_t max_bytes = (out_sz - 1) / 2;
    if (len > max_bytes) len = max_bytes;
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex[(data[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[data[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

static void append_part(char *dst, size_t dst_sz, const char *part)
{
    if (!dst || dst_sz == 0 || !part || !part[0]) return;
    size_t used = strlen(dst);
    if (used >= dst_sz - 1) return;
    if (used > 0) {
        int n = snprintf(dst + used, dst_sz - used, " / %s", part);
        (void)n;
    } else {
        int n = snprintf(dst, dst_sz, "%s", part);
        (void)n;
    }
}

static size_t json_escape(const char *src, char *dst, size_t dst_sz)
{
    if (!src || !dst || dst_sz == 0) return (size_t)-1;
    size_t n = 0;
    for (const char *p = src; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        const char *seg = NULL;
        char tmp[8];
        size_t seg_len = 0;
        switch (ch) {
        case '"': seg = "\\\""; seg_len = 2; break;
        case '\\': seg = "\\\\"; seg_len = 2; break;
        case '\b': seg = "\\b"; seg_len = 2; break;
        case '\f': seg = "\\f"; seg_len = 2; break;
        case '\n': seg = "\\n"; seg_len = 2; break;
        case '\r': seg = "\\r"; seg_len = 2; break;
        case '\t': seg = "\\t"; seg_len = 2; break;
        default:
            if (ch < 0x20) {
                snprintf(tmp, sizeof(tmp), "\\u%04x", ch);
                seg = tmp;
                seg_len = 6;
            } else {
                tmp[0] = (char)ch;
                tmp[1] = '\0';
                seg = tmp;
                seg_len = 1;
            }
            break;
        }
        if (n + seg_len >= dst_sz) return (size_t)-1;
        memcpy(dst + n, seg, seg_len);
        n += seg_len;
    }
    if (n >= dst_sz) return (size_t)-1;
    dst[n] = '\0';
    return n;
}

void device_analysis_defaults(device_analysis_t *a)
{
    if (!a) return;
    memset(a, 0, sizeof(*a));
    snprintf(a->status, sizeof(a->status), "未采集");
    snprintf(a->summary, sizeof(a->summary), "尚未收到运动数据");
}

bool device_analysis_parse_cps(device_analysis_t *a, const uint8_t *data, size_t len)
{
    if (!a || !data || len < 2) return false;
    bytes_to_hex(data, len, a->last_cps_hex, sizeof(a->last_cps_hex));
    a->service_flags |= DEVICE_TEMPLATE_SVC_CPS;
    uint8_t flags = data[0];
    size_t off = 1;
    if ((flags & 0x01) == 0) {
        if (len < off + 2) return false;
        a->power_watts = le16s(data + off);
        off += 2;
    }
    if (flags & 0x20) {
        off += 4;
        if (len >= off + 2) off += 2;
    }
    if (flags & 0x04) {
        off += 4;
        if (len >= off + 2) off += 2;
    }
    a->sample_count++;
    return true;
}

bool device_analysis_parse_hr(device_analysis_t *a, const uint8_t *data, size_t len)
{
    if (!a || !data || len < 2) return false;
    bytes_to_hex(data, len, a->last_hrs_hex, sizeof(a->last_hrs_hex));
    a->service_flags |= DEVICE_TEMPLATE_SVC_HRS;
    if ((data[0] & 0x01) != 0) {
        if (len < 3) return false;
        a->heart_rate_bpm = le16u(data + 1);
    } else {
        a->heart_rate_bpm = data[1];
    }
    a->sample_count++;
    return true;
}

bool device_analysis_parse_csc(device_analysis_t *a, const uint8_t *data, size_t len)
{
    if (!a || !data || len < 1) return false;
    bytes_to_hex(data, len, a->last_csc_hex, sizeof(a->last_csc_hex));
    uint8_t flags = data[0];
    size_t off = 1;
    a->service_flags |= DEVICE_TEMPLATE_SVC_CSC;
    if (flags & 0x01) {
        if (len < off + 6) return false;
        off += 6;
    }
    if (flags & 0x02) {
        if (len < off + 4) return false;
        a->cadence_rpm = 1;
    }
    a->sample_count++;
    return true;
}

bool device_analysis_parse_ftms_feature(device_analysis_t *a, const uint8_t *data, size_t len)
{
    if (!a || !data || len < 8) return false;
    bytes_to_hex(data, len, a->ftms_feature_hex, sizeof(a->ftms_feature_hex));
    a->service_flags |= DEVICE_TEMPLATE_SVC_FTMS;
    uint32_t target = (uint32_t)data[4]
                    | ((uint32_t)data[5] << 8)
                    | ((uint32_t)data[6] << 16)
                    | ((uint32_t)data[7] << 24);
    a->ftms_control_supported = target != 0 ? 1 : 0;
    return true;
}

bool device_analysis_parse_ftms_bike(device_analysis_t *a, const uint8_t *data, size_t len)
{
    if (!a || !data || len < 2) return false;
    bytes_to_hex(data, len, a->last_ftms_hex, sizeof(a->last_ftms_hex));
    uint16_t flags = le16u(data);
    size_t off = 2;
    a->service_flags |= DEVICE_TEMPLATE_SVC_FTMS;

    if ((flags & 0x0001) == 0) {
        if (len < off + 2) return false;
        a->speed_x10_kmh = le16u(data + off) / 10;
        off += 2;
    }
    if (flags & 0x0002) {
        if (len < off + 2) return false;
        off += 2;
    }
    if (flags & 0x0004) {
        if (len < off + 2) return false;
        a->cadence_rpm = le16u(data + off) / 2;
        off += 2;
    }
    if (flags & 0x0008) {
        if (len < off + 2) return false;
        off += 2;
    }
    if (flags & 0x0010) {
        if (len < off + 3) return false;
        off += 3;
    }
    if (flags & 0x0020) {
        if (len < off + 2) return false;
        off += 2;
    }
    if (flags & 0x0040) {
        if (len < off + 2) return false;
        a->power_watts = le16s(data + off);
        off += 2;
    }
    if (flags & 0x0080) {
        if (len < off + 2) return false;
        off += 2;
    }
    if (flags & 0x0100) {
        if (len < off + 1) return false;
        a->heart_rate_bpm = data[off];
        off += 1;
    }
    (void)off;
    a->sample_count++;
    return true;
}

void device_analysis_finalize(device_analysis_t *a)
{
    if (!a) return;
    a->summary[0] = '\0';
    if (a->sample_count == 0) {
        snprintf(a->status, sizeof(a->status), "未收到数据");
        snprintf(a->summary, sizeof(a->summary), "设备可连接，但未收到运动数据");
        return;
    }
    snprintf(a->status, sizeof(a->status), "数据正常");
    if (a->power_watts != 0) {
        char part[24];
        snprintf(part, sizeof(part), "功率 %dW", (int)a->power_watts);
        append_part(a->summary, sizeof(a->summary), part);
    }
    if (a->cadence_rpm != 0) {
        char part[24];
        snprintf(part, sizeof(part), "踏频 %urpm", (unsigned)a->cadence_rpm);
        append_part(a->summary, sizeof(a->summary), part);
    }
    if (a->heart_rate_bpm != 0) {
        char part[24];
        snprintf(part, sizeof(part), "心率 %u", (unsigned)a->heart_rate_bpm);
        append_part(a->summary, sizeof(a->summary), part);
    }
    if (a->speed_x10_kmh != 0) {
        char part[24];
        snprintf(part, sizeof(part), "速度 %u.%ukm/h",
                 (unsigned)(a->speed_x10_kmh / 10),
                 (unsigned)(a->speed_x10_kmh % 10));
        append_part(a->summary, sizeof(a->summary), part);
    }
    if (a->summary[0] == '\0') {
        snprintf(a->summary, sizeof(a->summary), "已收到运动数据");
    }
}

size_t device_analysis_serialize_json(const device_analysis_t *a, char *out, size_t out_sz)
{
    if (!a || !out || out_sz == 0) return 0;
    char status[DEVICE_ANALYSIS_STATUS_MAX * 6];
    char summary[DEVICE_ANALYSIS_SUMMARY_MAX * 6];
    if (json_escape(a->status, status, sizeof(status)) == (size_t)-1) return 0;
    if (json_escape(a->summary, summary, sizeof(summary)) == (size_t)-1) return 0;
    int n = snprintf(out, out_sz,
        "{\"sample_count\":%u,\"service_flags\":%u,\"power_watts\":%d,"
        "\"cadence_rpm\":%u,\"heart_rate_bpm\":%u,\"speed_x10_kmh\":%u,"
        "\"ftms_control_supported\":%s,"
        "\"last_cps_hex\":\"%s\",\"last_hrs_hex\":\"%s\",\"last_csc_hex\":\"%s\","
        "\"last_ftms_hex\":\"%s\",\"ftms_feature_hex\":\"%s\","
        "\"status\":\"%s\",\"summary\":\"%s\"}",
        (unsigned)a->sample_count,
        (unsigned)a->service_flags,
        (int)a->power_watts,
        (unsigned)a->cadence_rpm,
        (unsigned)a->heart_rate_bpm,
        (unsigned)a->speed_x10_kmh,
        a->ftms_control_supported ? "true" : "false",
        a->last_cps_hex,
        a->last_hrs_hex,
        a->last_csc_hex,
        a->last_ftms_hex,
        a->ftms_feature_hex,
        status,
        summary);
    if (n < 0 || (size_t)n >= out_sz) return 0;
    return (size_t)n;
}
