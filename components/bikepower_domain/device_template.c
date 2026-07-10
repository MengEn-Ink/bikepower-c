/**
 * BLE 设备信息模板纯逻辑实现。
 */
#include "device_template.h"
#include <stdio.h>
#include <string.h>

static void str_terminate(char *s, size_t sz)
{
    if (sz == 0) return;
    s[sz - 1] = '\0';
}

static bool str_changed(const char *before, const char *after, size_t sz)
{
    return strncmp(before, after, sz) != 0;
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

static bool text_present(const char *s)
{
    return s && s[0] != '\0';
}

static void copy_if_present(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0 || !text_present(src)) return;
    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
}

static bool same_template_identity(const device_template_t *a, const device_template_t *b)
{
    if (!a || !b) return false;
    if (text_present(a->addr) && text_present(b->addr)
        && a->addr_type == b->addr_type
        && strcmp(a->addr, b->addr) == 0) {
        return true;
    }
    if (text_present(a->device_name) && text_present(b->device_name)
        && strcmp(a->device_name, b->device_name) == 0) {
        return true;
    }
    return false;
}

static void merge_template(device_template_t *dst, const device_template_t *src, uint8_t id)
{
    if (!dst || !src) return;
    uint16_t service_flags = dst->service_flags | src->service_flags;
    copy_if_present(dst->alias, sizeof(dst->alias), src->alias);
    copy_if_present(dst->adv_name, sizeof(dst->adv_name), src->adv_name);
    copy_if_present(dst->device_name, sizeof(dst->device_name), src->device_name);
    copy_if_present(dst->manufacturer, sizeof(dst->manufacturer), src->manufacturer);
    copy_if_present(dst->model, sizeof(dst->model), src->model);
    copy_if_present(dst->firmware, sizeof(dst->firmware), src->firmware);
    copy_if_present(dst->hardware, sizeof(dst->hardware), src->hardware);
    copy_if_present(dst->software, sizeof(dst->software), src->software);
    copy_if_present(dst->addr, sizeof(dst->addr), src->addr);
    copy_if_present(dst->adv_raw_hex, sizeof(dst->adv_raw_hex), src->adv_raw_hex);
    copy_if_present(dst->scan_rsp_raw_hex, sizeof(dst->scan_rsp_raw_hex), src->scan_rsp_raw_hex);
    copy_if_present(dst->manufacturer_data_hex, sizeof(dst->manufacturer_data_hex), src->manufacturer_data_hex);
    copy_if_present(dst->service_data_hex, sizeof(dst->service_data_hex), src->service_data_hex);
    dst->addr_type = src->addr_type <= 1 ? src->addr_type : dst->addr_type;
    if (src->last_rssi <= 0) dst->last_rssi = src->last_rssi;
    if (src->tx_power != 0) dst->tx_power = src->tx_power;
    if (src->appearance != 0) dst->appearance = src->appearance;
    dst->service_flags = service_flags;
    dst->used = true;
    dst->id = id;
    device_template_validate(dst);
}

void device_template_defaults(device_template_t *tpl)
{
    if (!tpl) return;
    memset(tpl, 0, sizeof(*tpl));
}

void device_template_list_defaults(device_template_list_t *list)
{
    if (!list) return;
    memset(list, 0, sizeof(*list));
    for (int i = 0; i < DEVICE_TEMPLATE_MAX; i++) {
        list->items[i].id = (uint8_t)i;
    }
}

bool device_template_validate(device_template_t *tpl)
{
    if (!tpl) return false;
    device_template_t before = *tpl;
    str_terminate(tpl->alias, sizeof(tpl->alias));
    str_terminate(tpl->adv_name, sizeof(tpl->adv_name));
    str_terminate(tpl->device_name, sizeof(tpl->device_name));
    str_terminate(tpl->manufacturer, sizeof(tpl->manufacturer));
    str_terminate(tpl->model, sizeof(tpl->model));
    str_terminate(tpl->firmware, sizeof(tpl->firmware));
    str_terminate(tpl->hardware, sizeof(tpl->hardware));
    str_terminate(tpl->software, sizeof(tpl->software));
    str_terminate(tpl->addr, sizeof(tpl->addr));
    str_terminate(tpl->adv_raw_hex, sizeof(tpl->adv_raw_hex));
    str_terminate(tpl->scan_rsp_raw_hex, sizeof(tpl->scan_rsp_raw_hex));
    str_terminate(tpl->manufacturer_data_hex, sizeof(tpl->manufacturer_data_hex));
    str_terminate(tpl->service_data_hex, sizeof(tpl->service_data_hex));
    if (tpl->id >= DEVICE_TEMPLATE_MAX) tpl->id = 0;
    if (tpl->addr_type > 1) tpl->addr_type = 0;
    if (tpl->last_rssi > 0) tpl->last_rssi = 0;
    return before.used != tpl->used
        || before.id != tpl->id
        || str_changed(before.alias, tpl->alias, sizeof(tpl->alias))
        || str_changed(before.adv_name, tpl->adv_name, sizeof(tpl->adv_name))
        || str_changed(before.device_name, tpl->device_name, sizeof(tpl->device_name))
        || str_changed(before.manufacturer, tpl->manufacturer, sizeof(tpl->manufacturer))
        || str_changed(before.model, tpl->model, sizeof(tpl->model))
        || str_changed(before.firmware, tpl->firmware, sizeof(tpl->firmware))
        || str_changed(before.hardware, tpl->hardware, sizeof(tpl->hardware))
        || str_changed(before.software, tpl->software, sizeof(tpl->software))
        || str_changed(before.addr, tpl->addr, sizeof(tpl->addr))
        || str_changed(before.adv_raw_hex, tpl->adv_raw_hex, sizeof(tpl->adv_raw_hex))
        || str_changed(before.scan_rsp_raw_hex, tpl->scan_rsp_raw_hex, sizeof(tpl->scan_rsp_raw_hex))
        || str_changed(before.manufacturer_data_hex, tpl->manufacturer_data_hex, sizeof(tpl->manufacturer_data_hex))
        || str_changed(before.service_data_hex, tpl->service_data_hex, sizeof(tpl->service_data_hex))
        || before.addr_type != tpl->addr_type
        || before.last_rssi != tpl->last_rssi
        || before.tx_power != tpl->tx_power
        || before.appearance != tpl->appearance
        || before.service_flags != tpl->service_flags;
}

bool device_template_set_addr(device_template_t *tpl, const uint8_t addr[6], uint8_t addr_type)
{
    if (!tpl || !addr) return false;
    snprintf(tpl->addr, sizeof(tpl->addr), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    tpl->addr_type = addr_type > 1 ? 0 : addr_type;
    return true;
}

bool device_template_note_service(device_template_t *tpl, uint16_t uuid16)
{
    if (!tpl) return false;
    uint16_t flag = 0;
    switch (uuid16) {
    case 0x180A: flag = DEVICE_TEMPLATE_SVC_DIS; break;
    case 0x180F: flag = DEVICE_TEMPLATE_SVC_BAS; break;
    case 0x1818: flag = DEVICE_TEMPLATE_SVC_CPS; break;
    case 0x180D: flag = DEVICE_TEMPLATE_SVC_HRS; break;
    case 0x1816: flag = DEVICE_TEMPLATE_SVC_CSC; break;
    case 0x1826: flag = DEVICE_TEMPLATE_SVC_FTMS; break;
    default: return false;
    }
    tpl->service_flags |= flag;
    return true;
}

bool device_template_has_ride_service(uint16_t service_flags)
{
    return (service_flags & (DEVICE_TEMPLATE_SVC_CPS
                           | DEVICE_TEMPLATE_SVC_HRS
                           | DEVICE_TEMPLATE_SVC_CSC
                           | DEVICE_TEMPLATE_SVC_FTMS)) != 0;
}

void device_template_list_recount(device_template_list_t *list)
{
    if (!list) return;
    uint8_t count = 0;
    for (int i = 0; i < DEVICE_TEMPLATE_MAX; i++) {
        list->items[i].id = (uint8_t)i;
        device_template_validate(&list->items[i]);
        if (list->items[i].used) count++;
    }
    list->count = count;
}

int device_template_list_add(device_template_list_t *list, const device_template_t *tpl)
{
    if (!list || !tpl) return -1;
    device_template_list_recount(list);
    for (int i = 0; i < DEVICE_TEMPLATE_MAX; i++) {
        if (list->items[i].used && same_template_identity(&list->items[i], tpl)) {
            merge_template(&list->items[i], tpl, (uint8_t)i);
            device_template_list_recount(list);
            return i;
        }
    }
    for (int i = 0; i < DEVICE_TEMPLATE_MAX; i++) {
        if (!list->items[i].used) {
            list->items[i] = *tpl;
            list->items[i].used = true;
            list->items[i].id = (uint8_t)i;
            device_template_validate(&list->items[i]);
            device_template_list_recount(list);
            return i;
        }
    }
    return -1;
}

bool device_template_list_delete(device_template_list_t *list, uint8_t id)
{
    if (!list || id >= DEVICE_TEMPLATE_MAX || !list->items[id].used) return false;
    device_template_defaults(&list->items[id]);
    list->items[id].id = id;
    device_template_list_recount(list);
    return true;
}

size_t device_template_serialize_json(const device_template_t *tpl, char *out, size_t out_sz)
{
    if (!tpl || !out || out_sz == 0) return 0;
    char alias[DEVICE_TEMPLATE_ALIAS_MAX * 6];
    char adv_name[DEVICE_TEMPLATE_NAME_MAX * 6];
    char device_name[DEVICE_TEMPLATE_NAME_MAX * 6];
    char manufacturer[DEVICE_TEMPLATE_TEXT_MAX * 6];
    char model[DEVICE_TEMPLATE_TEXT_MAX * 6];
    char firmware[DEVICE_TEMPLATE_VERSION_MAX * 6];
    char hardware[DEVICE_TEMPLATE_VERSION_MAX * 6];
    char software[DEVICE_TEMPLATE_VERSION_MAX * 6];
    char addr[DEVICE_TEMPLATE_ADDR_MAX * 6];
    char adv_raw_hex[DEVICE_TEMPLATE_RAW_HEX_MAX * 6];
    char scan_rsp_raw_hex[DEVICE_TEMPLATE_RAW_HEX_MAX * 6];
    char manufacturer_data_hex[DEVICE_TEMPLATE_RAW_HEX_MAX * 6];
    char service_data_hex[DEVICE_TEMPLATE_RAW_HEX_MAX * 6];
    if (json_escape(tpl->alias, alias, sizeof(alias)) == (size_t)-1) return 0;
    if (json_escape(tpl->adv_name, adv_name, sizeof(adv_name)) == (size_t)-1) return 0;
    if (json_escape(tpl->device_name, device_name, sizeof(device_name)) == (size_t)-1) return 0;
    if (json_escape(tpl->manufacturer, manufacturer, sizeof(manufacturer)) == (size_t)-1) return 0;
    if (json_escape(tpl->model, model, sizeof(model)) == (size_t)-1) return 0;
    if (json_escape(tpl->firmware, firmware, sizeof(firmware)) == (size_t)-1) return 0;
    if (json_escape(tpl->hardware, hardware, sizeof(hardware)) == (size_t)-1) return 0;
    if (json_escape(tpl->software, software, sizeof(software)) == (size_t)-1) return 0;
    if (json_escape(tpl->addr, addr, sizeof(addr)) == (size_t)-1) return 0;
    if (json_escape(tpl->adv_raw_hex, adv_raw_hex, sizeof(adv_raw_hex)) == (size_t)-1) return 0;
    if (json_escape(tpl->scan_rsp_raw_hex, scan_rsp_raw_hex, sizeof(scan_rsp_raw_hex)) == (size_t)-1) return 0;
    if (json_escape(tpl->manufacturer_data_hex, manufacturer_data_hex, sizeof(manufacturer_data_hex)) == (size_t)-1) return 0;
    if (json_escape(tpl->service_data_hex, service_data_hex, sizeof(service_data_hex)) == (size_t)-1) return 0;
    int n = snprintf(out, out_sz,
        "{\"id\":%u,\"alias\":\"%s\",\"adv_name\":\"%s\","
        "\"device_name\":\"%s\",\"manufacturer\":\"%s\",\"model\":\"%s\","
        "\"firmware\":\"%s\",\"hardware\":\"%s\",\"software\":\"%s\","
        "\"addr\":\"%s\",\"addr_type\":%u,\"last_rssi\":%d,"
        "\"tx_power\":%d,\"appearance\":%u,"
        "\"adv_raw_hex\":\"%s\",\"scan_rsp_raw_hex\":\"%s\","
        "\"manufacturer_data_hex\":\"%s\",\"service_data_hex\":\"%s\","
        "\"service_flags\":%u}",
        (unsigned)tpl->id, alias, adv_name, device_name, manufacturer, model,
        firmware, hardware, software, addr, (unsigned)tpl->addr_type,
        (int)tpl->last_rssi, (int)tpl->tx_power, (unsigned)tpl->appearance,
        adv_raw_hex, scan_rsp_raw_hex, manufacturer_data_hex, service_data_hex,
        (unsigned)tpl->service_flags);
    if (n < 0 || (size_t)n >= out_sz) return 0;
    return (size_t)n;
}

size_t device_template_list_serialize_json(const device_template_list_t *list, char *out, size_t out_sz)
{
    if (!list || !out || out_sz == 0) return 0;
    size_t off = 0;
    int n = snprintf(out + off, out_sz - off, "{\"max\":%d,\"count\":%u,\"items\":[",
                     DEVICE_TEMPLATE_MAX, (unsigned)list->count);
    if (n < 0 || (size_t)n >= out_sz - off) return 0;
    off += (size_t)n;
    bool first = true;
    for (int i = 0; i < DEVICE_TEMPLATE_MAX; i++) {
        if (!list->items[i].used) continue;
        if (!first) {
            if (off + 1 >= out_sz) return 0;
            out[off++] = ',';
            out[off] = '\0';
        }
        size_t wrote = device_template_serialize_json(&list->items[i], out + off, out_sz - off);
        if (wrote == 0) return 0;
        off += wrote;
        first = false;
    }
    if (off + 3 > out_sz) return 0;
    out[off++] = ']';
    out[off++] = '}';
    out[off] = '\0';
    return off;
}
