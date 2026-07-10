#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "os/os_mbuf.h"

#include "bikepower_config.h"
#include "ble_scanner.h"

static const char *TAG = "BLESCAN";

typedef struct {
    bool used;
    device_template_t tpl;
} scan_item_t;

typedef struct {
    SemaphoreHandle_t lock;
    scan_item_t items[BLE_SCAN_MAX_RESULTS];
    uint8_t count;
    bool scanning;
} scan_state_t;

typedef struct {
    SemaphoreHandle_t done;
    SemaphoreHandle_t disconnected;
    device_template_t tpl;
    ble_addr_t peer;
    uint16_t conn_handle;
    uint16_t next_char_uuid;
    esp_err_t result;
    bool connected;
    bool done_flag;
} read_ctx_t;

typedef struct {
    uint16_t uuid16;
    uint16_t val_handle;
    bool found;
    bool subscribed;
} analysis_char_t;

typedef struct {
    SemaphoreHandle_t done;
    SemaphoreHandle_t disconnected;
    device_template_t tpl;
    device_analysis_t analysis;
    ble_addr_t peer;
    uint16_t conn_handle;
    uint16_t next_read_uuid;
    esp_err_t result;
    bool connected;
    bool done_flag;
    uint8_t disc_index;
    analysis_char_t chars[5];
} analysis_ctx_t;

static scan_state_t s_scan;

static bool ensure_init(void)
{
    if (!s_scan.lock) s_scan.lock = xSemaphoreCreateMutex();
    return s_scan.lock != NULL;
}

static void copy_text(char *dst, size_t dst_sz, const uint8_t *src, size_t src_len)
{
    if (!dst || dst_sz == 0) return;
    size_t n = src_len;
    if (n >= dst_sz) n = dst_sz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool parse_addr_text(const char *addr, uint8_t out[6])
{
    if (!addr || !out) return false;
    unsigned int b[6];
    if (sscanf(addr, "%02x:%02x:%02x:%02x:%02x:%02x", &b[5], &b[4], &b[3], &b[2], &b[1], &b[0]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (b[i] > 0xff) return false;
        out[i] = (uint8_t)b[i];
    }
    return true;
}

static bool text_present(const char *s)
{
    return s && s[0] != '\0';
}

static bool same_name(const device_template_t *a, const device_template_t *b)
{
    if (!a || !b) return false;
    const char *names_a[] = { a->device_name, a->adv_name, a->alias };
    const char *names_b[] = { b->device_name, b->adv_name, b->alias };
    for (size_t i = 0; i < sizeof(names_a) / sizeof(names_a[0]); i++) {
        if (!text_present(names_a[i])) continue;
        for (size_t j = 0; j < sizeof(names_b) / sizeof(names_b[0]); j++) {
            if (text_present(names_b[j]) && strcmp(names_a[i], names_b[j]) == 0) return true;
        }
    }
    return false;
}

static void keep_text(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0 || text_present(dst) || !text_present(src)) return;
    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
}

static int find_item_by_identity(const device_template_t *tpl)
{
    if (!tpl) return -1;
    for (int i = 0; i < BLE_SCAN_MAX_RESULTS; i++) {
        if (!s_scan.items[i].used) continue;
        if (text_present(tpl->addr)
            && s_scan.items[i].tpl.addr_type == tpl->addr_type
            && strcmp(s_scan.items[i].tpl.addr, tpl->addr) == 0) {
            return i;
        }
        if (same_name(&s_scan.items[i].tpl, tpl)) {
            return i;
        }
    }
    return -1;
}

static int find_free_or_weakest(void)
{
    int weakest = 0;
    for (int i = 0; i < BLE_SCAN_MAX_RESULTS; i++) {
        if (!s_scan.items[i].used) return i;
        if (s_scan.items[i].tpl.last_rssi < s_scan.items[weakest].tpl.last_rssi) weakest = i;
    }
    return weakest;
}

static bool scan_item_has_name(const device_template_t *tpl)
{
    return tpl && (tpl->alias[0] != '\0' || tpl->device_name[0] != '\0' || tpl->adv_name[0] != '\0');
}

static bool scan_item_is_visible(const device_template_t *tpl)
{
    return scan_item_has_name(tpl) && device_template_has_ride_service(tpl->service_flags);
}

static void merge_scan_item(device_template_t *dst, const device_template_t *src, uint8_t id)
{
    if (!dst || !src) return;
    uint16_t flags = dst->service_flags | src->service_flags;
    if (text_present(src->alias)) strncpy(dst->alias, src->alias, sizeof(dst->alias) - 1);
    if (text_present(src->adv_name)) strncpy(dst->adv_name, src->adv_name, sizeof(dst->adv_name) - 1);
    if (text_present(src->device_name)) strncpy(dst->device_name, src->device_name, sizeof(dst->device_name) - 1);
    keep_text(dst->manufacturer, sizeof(dst->manufacturer), src->manufacturer);
    keep_text(dst->model, sizeof(dst->model), src->model);
    keep_text(dst->firmware, sizeof(dst->firmware), src->firmware);
    keep_text(dst->hardware, sizeof(dst->hardware), src->hardware);
    keep_text(dst->software, sizeof(dst->software), src->software);
    if (text_present(src->addr)) strncpy(dst->addr, src->addr, sizeof(dst->addr) - 1);
    dst->addr_type = src->addr_type <= 1 ? src->addr_type : dst->addr_type;
    if (src->last_rssi <= 0) dst->last_rssi = src->last_rssi;
    dst->service_flags = flags;
    dst->id = id;
    dst->used = true;
    device_template_validate(dst);
}

static void parse_adv(device_template_t *tpl, const uint8_t *data, uint8_t len)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    if (ble_hs_adv_parse_fields(&fields, data, len) != 0) return;
    if (fields.name && fields.name_len > 0) copy_text(tpl->adv_name, sizeof(tpl->adv_name), fields.name, fields.name_len);
    for (int i = 0; i < fields.num_uuids16; i++) device_template_note_service(tpl, fields.uuids16[i].value);
}

static void on_disc(const struct ble_gap_disc_desc *desc)
{
    if (!desc || !ensure_init()) return;
    device_template_t tpl;
    device_template_defaults(&tpl);
    tpl.used = true;
    device_template_set_addr(&tpl, desc->addr.val, desc->addr.type);
    tpl.last_rssi = desc->rssi == 127 ? 0 : desc->rssi;
    parse_adv(&tpl, desc->data, desc->length_data);
    xSemaphoreTake(s_scan.lock, portMAX_DELAY);
    int idx = find_item_by_identity(&tpl);
    bool matched_existing = idx >= 0;
    if (idx < 0 && !scan_item_is_visible(&tpl)) {
        xSemaphoreGive(s_scan.lock);
        return;
    }
    if (idx < 0) idx = find_free_or_weakest();
    bool was_free = !s_scan.items[idx].used;
    if (!matched_existing && !was_free && tpl.last_rssi < s_scan.items[idx].tpl.last_rssi && s_scan.count >= BLE_SCAN_MAX_RESULTS) {
        xSemaphoreGive(s_scan.lock);
        return;
    }
    if (!matched_existing) device_template_defaults(&s_scan.items[idx].tpl);
    s_scan.items[idx].used = true;
    merge_scan_item(&s_scan.items[idx].tpl, &tpl, (uint8_t)idx);
    if (was_free && s_scan.count < BLE_SCAN_MAX_RESULTS) s_scan.count++;
    xSemaphoreGive(s_scan.lock);
}

static int scan_gap_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        on_disc(&event->disc);
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (ensure_init()) {
            xSemaphoreTake(s_scan.lock, portMAX_DELAY);
            s_scan.scanning = false;
            xSemaphoreGive(s_scan.lock);
        }
        ESP_LOGI(TAG, "扫描完成 reason=%d", event->disc_complete.reason);
        return 0;
    default:
        return 0;
    }
}

esp_err_t ble_scanner_start(uint32_t duration_ms)
{
    if (!ensure_init()) return ESP_ERR_NO_MEM;
    if (duration_ms == 0) duration_ms = BLE_SCAN_DEFAULT_MS;
    uint8_t own_addr_type = 0;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) return ESP_FAIL;
    struct ble_gap_disc_params params;
    memset(&params, 0, sizeof(params));
    params.itvl = BLE_GAP_SCAN_ITVL_MS(60);
    params.window = BLE_GAP_SCAN_WIN_MS(30);
    params.passive = 0;
    params.filter_duplicates = 0;
    xSemaphoreTake(s_scan.lock, portMAX_DELAY);
    memset(s_scan.items, 0, sizeof(s_scan.items));
    s_scan.count = 0;
    s_scan.scanning = true;
    xSemaphoreGive(s_scan.lock);
    rc = ble_gap_disc(own_addr_type, (int32_t)duration_ms, &params, scan_gap_cb, NULL);
    if (rc != 0) {
        xSemaphoreTake(s_scan.lock, portMAX_DELAY);
        s_scan.scanning = false;
        xSemaphoreGive(s_scan.lock);
        ESP_LOGW(TAG, "启动扫描失败 rc=%d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "开始扫描 %ums", (unsigned)duration_ms);
    return ESP_OK;
}

esp_err_t ble_scanner_stop(void)
{
    if (!ensure_init()) return ESP_ERR_NO_MEM;
    int rc = ble_gap_disc_cancel();
    xSemaphoreTake(s_scan.lock, portMAX_DELAY);
    s_scan.scanning = false;
    xSemaphoreGive(s_scan.lock);
    if (rc != 0 && rc != BLE_HS_EALREADY) return ESP_FAIL;
    return ESP_OK;
}

bool ble_scanner_is_scanning(void)
{
    if (!ensure_init()) return false;
    xSemaphoreTake(s_scan.lock, portMAX_DELAY);
    bool scanning = s_scan.scanning;
    xSemaphoreGive(s_scan.lock);
    return scanning;
}

size_t ble_scanner_serialize_results(char *out, size_t out_sz)
{
    if (!out || out_sz == 0 || !ensure_init()) return 0;
    xSemaphoreTake(s_scan.lock, portMAX_DELAY);
    size_t off = 0;
    int n = snprintf(out, out_sz, "{\"scanning\":%s,\"devices\":[", s_scan.scanning ? "true" : "false");
    if (n < 0 || (size_t)n >= out_sz) {
        xSemaphoreGive(s_scan.lock);
        return 0;
    }
    off = (size_t)n;
    bool first = true;
    for (int i = 0; i < BLE_SCAN_MAX_RESULTS; i++) {
        if (!s_scan.items[i].used) continue;
        if (!scan_item_is_visible(&s_scan.items[i].tpl)) continue;
        if (!first) {
            if (off + 1 >= out_sz) { xSemaphoreGive(s_scan.lock); return 0; }
            out[off++] = ',';
            out[off] = '\0';
        }
        size_t wrote = device_template_serialize_json(&s_scan.items[i].tpl, out + off, out_sz - off);
        if (wrote == 0) { xSemaphoreGive(s_scan.lock); return 0; }
        off += wrote;
        first = false;
    }
    if (off + 3 > out_sz) {
        xSemaphoreGive(s_scan.lock);
        return 0;
    }
    out[off++] = ']';
    out[off++] = '}';
    out[off] = '\0';
    xSemaphoreGive(s_scan.lock);
    return off;
}

static void read_ctx_finish(read_ctx_t *ctx, esp_err_t result)
{
    if (!ctx || ctx->done_flag) return;
    ctx->result = result;
    ctx->done_flag = true;
    if (ctx->done) xSemaphoreGive(ctx->done);
}

static uint16_t next_dis_uuid(uint16_t current)
{
    switch (current) {
    case 0x2A00: return 0x2A29;
    case 0x2A29: return 0x2A24;
    case 0x2A24: return 0x2A26;
    case 0x2A26: return 0x2A27;
    case 0x2A27: return 0x2A28;
    default: return 0;
    }
}

static void apply_read_value(read_ctx_t *ctx, uint16_t uuid16, const struct ble_gatt_attr *attr)
{
    if (!ctx || !attr || !attr->om) return;
    uint8_t buf[DEVICE_TEMPLATE_TEXT_MAX];
    int len = OS_MBUF_PKTLEN(attr->om);
    if (len <= 0) return;
    if (len > (int)sizeof(buf)) len = sizeof(buf);
    if (os_mbuf_copydata(attr->om, 0, len, buf) != 0) return;
    switch (uuid16) {
    case 0x2A00: copy_text(ctx->tpl.device_name, sizeof(ctx->tpl.device_name), buf, len); break;
    case 0x2A29: copy_text(ctx->tpl.manufacturer, sizeof(ctx->tpl.manufacturer), buf, len); break;
    case 0x2A24: copy_text(ctx->tpl.model, sizeof(ctx->tpl.model), buf, len); break;
    case 0x2A26: copy_text(ctx->tpl.firmware, sizeof(ctx->tpl.firmware), buf, len); break;
    case 0x2A27: copy_text(ctx->tpl.hardware, sizeof(ctx->tpl.hardware), buf, len); break;
    case 0x2A28: copy_text(ctx->tpl.software, sizeof(ctx->tpl.software), buf, len); break;
    default: break;
    }
}

static void read_next_char(read_ctx_t *ctx);

static int read_attr_cb(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    read_ctx_t *ctx = (read_ctx_t *)arg;
    if (!ctx) return 0;
    if (error && error->status == 0 && attr) {
        apply_read_value(ctx, ctx->next_char_uuid, attr);
        return 0;
    }
    if (error && error->status == BLE_HS_EDONE) {
        ctx->next_char_uuid = next_dis_uuid(ctx->next_char_uuid);
        read_next_char(ctx);
        return 0;
    }
    ctx->next_char_uuid = next_dis_uuid(ctx->next_char_uuid);
    read_next_char(ctx);
    return 0;
}

static void read_next_char(read_ctx_t *ctx)
{
    if (!ctx || ctx->done_flag) return;
    if (ctx->next_char_uuid == 0) {
        read_ctx_finish(ctx, ESP_OK);
        return;
    }
    ble_uuid16_t uuid = BLE_UUID16_INIT(ctx->next_char_uuid);
    int rc = ble_gattc_read_by_uuid(ctx->conn_handle, 1, 0xffff, &uuid.u, read_attr_cb, ctx);
    if (rc != 0) {
        ctx->next_char_uuid = next_dis_uuid(ctx->next_char_uuid);
        read_next_char(ctx);
    }
}

static analysis_char_t *analysis_find_char(analysis_ctx_t *ctx, uint16_t uuid16)
{
    if (!ctx) return NULL;
    for (size_t i = 0; i < sizeof(ctx->chars) / sizeof(ctx->chars[0]); i++) {
        if (ctx->chars[i].uuid16 == uuid16) return &ctx->chars[i];
    }
    return NULL;
}

static analysis_char_t *analysis_find_char_by_handle(analysis_ctx_t *ctx, uint16_t val_handle)
{
    if (!ctx) return NULL;
    for (size_t i = 0; i < sizeof(ctx->chars) / sizeof(ctx->chars[0]); i++) {
        if (ctx->chars[i].val_handle == val_handle) return &ctx->chars[i];
    }
    return NULL;
}

static void analysis_ctx_finish(analysis_ctx_t *ctx, esp_err_t result)
{
    if (!ctx || ctx->done_flag) return;
    ctx->result = result;
    ctx->done_flag = true;
    if (ctx->done) xSemaphoreGive(ctx->done);
}

static uint16_t next_analysis_read_uuid(uint16_t current)
{
    switch (current) {
    case 0x2A00: return 0x2A29;
    case 0x2A29: return 0x2A24;
    case 0x2A24: return 0x2A26;
    case 0x2A26: return 0x2A27;
    case 0x2A27: return 0x2A28;
    case 0x2A28: return FTMS_FEATURE_CHAR_UUID;
    default: return 0;
    }
}

static void analysis_apply_read_value(analysis_ctx_t *ctx, uint16_t uuid16, const struct ble_gatt_attr *attr)
{
    if (!ctx || !attr || !attr->om) return;
    uint8_t buf[32];
    int len = OS_MBUF_PKTLEN(attr->om);
    if (len <= 0) return;
    if (len > (int)sizeof(buf)) len = sizeof(buf);
    if (os_mbuf_copydata(attr->om, 0, len, buf) != 0) return;
    switch (uuid16) {
    case 0x2A00: copy_text(ctx->tpl.device_name, sizeof(ctx->tpl.device_name), buf, len); break;
    case 0x2A29: copy_text(ctx->tpl.manufacturer, sizeof(ctx->tpl.manufacturer), buf, len); break;
    case 0x2A24: copy_text(ctx->tpl.model, sizeof(ctx->tpl.model), buf, len); break;
    case 0x2A26: copy_text(ctx->tpl.firmware, sizeof(ctx->tpl.firmware), buf, len); break;
    case 0x2A27: copy_text(ctx->tpl.hardware, sizeof(ctx->tpl.hardware), buf, len); break;
    case 0x2A28: copy_text(ctx->tpl.software, sizeof(ctx->tpl.software), buf, len); break;
    case FTMS_FEATURE_CHAR_UUID:
        if (device_analysis_parse_ftms_feature(&ctx->analysis, buf, (size_t)len)) {
            device_template_note_service(&ctx->tpl, FTMS_SERVICE_UUID);
        }
        break;
    default:
        break;
    }
}

static void analysis_read_next_char(analysis_ctx_t *ctx);

static int analysis_read_attr_cb(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    analysis_ctx_t *ctx = (analysis_ctx_t *)arg;
    if (!ctx) return 0;
    if (error && error->status == 0 && attr) {
        analysis_apply_read_value(ctx, ctx->next_read_uuid, attr);
        return 0;
    }
    if (!error || error->status == BLE_HS_EDONE || error->status != 0) {
        ctx->next_read_uuid = next_analysis_read_uuid(ctx->next_read_uuid);
        analysis_read_next_char(ctx);
    }
    return 0;
}

static void analysis_read_next_char(analysis_ctx_t *ctx)
{
    if (!ctx || ctx->done_flag) return;
    if (ctx->next_read_uuid == 0) return;
    ble_uuid16_t uuid = BLE_UUID16_INIT(ctx->next_read_uuid);
    int rc = ble_gattc_read_by_uuid(ctx->conn_handle, 1, 0xffff, &uuid.u, analysis_read_attr_cb, ctx);
    if (rc != 0) {
        ctx->next_read_uuid = next_analysis_read_uuid(ctx->next_read_uuid);
        analysis_read_next_char(ctx);
    }
}

static void analysis_handle_sample(analysis_ctx_t *ctx, uint16_t uuid16, struct os_mbuf *om)
{
    if (!ctx || !om) return;
    uint8_t buf[32];
    int len = OS_MBUF_PKTLEN(om);
    if (len <= 0) return;
    if (len > (int)sizeof(buf)) len = sizeof(buf);
    if (os_mbuf_copydata(om, 0, len, buf) != 0) return;
    switch (uuid16) {
    case POWER_CHAR_UUID:
        if (device_analysis_parse_cps(&ctx->analysis, buf, (size_t)len)) {
            device_template_note_service(&ctx->tpl, POWER_SERVICE_UUID);
        }
        break;
    case HR_CHAR_UUID:
        if (device_analysis_parse_hr(&ctx->analysis, buf, (size_t)len)) {
            device_template_note_service(&ctx->tpl, HR_SERVICE_UUID);
        }
        break;
    case CSC_CHAR_UUID:
        if (device_analysis_parse_csc(&ctx->analysis, buf, (size_t)len)) {
            device_template_note_service(&ctx->tpl, CSC_SERVICE_UUID);
        }
        break;
    case FTMS_POWER_CHAR_UUID:
        if (device_analysis_parse_ftms_bike(&ctx->analysis, buf, (size_t)len)) {
            device_template_note_service(&ctx->tpl, FTMS_SERVICE_UUID);
        }
        break;
    default:
        break;
    }
}

static void analysis_start_next_disc(analysis_ctx_t *ctx);

static int analysis_subscribe_desc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                                      uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                                      void *arg)
{
    (void)conn_handle;
    analysis_ctx_t *ctx = (analysis_ctx_t *)arg;
    if (!ctx) return 0;
    if (error && error->status == 0 && dsc) {
        if (ble_uuid_u16(&dsc->uuid.u) == 0x2902) {
            analysis_char_t *ch = analysis_find_char_by_handle(ctx, chr_val_handle);
            if (ch) {
                uint8_t value[2] = {0x01, 0x00};
                int rc = ble_gattc_write_flat(ctx->conn_handle, dsc->handle, value, sizeof(value), NULL, NULL);
                if (rc == 0) ch->subscribed = true;
            }
        }
        return 0;
    }
    if (!error || error->status == BLE_HS_EDONE || error->status != 0) {
        ctx->disc_index++;
        analysis_start_next_disc(ctx);
    }
    return 0;
}

static int analysis_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                           const struct ble_gatt_chr *chr, void *arg)
{
    (void)conn_handle;
    analysis_ctx_t *ctx = (analysis_ctx_t *)arg;
    if (!ctx) return 0;
    if (error && error->status == 0 && chr) {
        uint16_t uuid16 = ble_uuid_u16(&chr->uuid.u);
        analysis_char_t *ch = analysis_find_char(ctx, uuid16);
        if (ch) {
            ch->found = true;
            ch->val_handle = chr->val_handle;
            device_template_note_service(&ctx->tpl,
                uuid16 == POWER_CHAR_UUID ? POWER_SERVICE_UUID :
                uuid16 == HR_CHAR_UUID ? HR_SERVICE_UUID :
                uuid16 == CSC_CHAR_UUID ? CSC_SERVICE_UUID : FTMS_SERVICE_UUID);
            uint16_t dsc_end = chr->val_handle > 0xfffb ? 0xffff : chr->val_handle + 4;
            int rc = ble_gattc_disc_all_dscs(ctx->conn_handle, chr->val_handle, dsc_end,
                                             analysis_subscribe_desc_cb, ctx);
            if (rc != 0) {
                ESP_LOGD(TAG, "订阅描述发现失败 uuid=0x%04x rc=%d", uuid16, rc);
                ctx->disc_index++;
                analysis_start_next_disc(ctx);
            }
        }
        return 0;
    }
    if (!error || error->status == BLE_HS_EDONE || error->status != 0) {
        ctx->disc_index++;
        analysis_start_next_disc(ctx);
    }
    return 0;
}

static void analysis_start_next_disc(analysis_ctx_t *ctx)
{
    if (!ctx) return;
    while (ctx->disc_index < sizeof(ctx->chars) / sizeof(ctx->chars[0])) {
        uint16_t uuid16 = ctx->chars[ctx->disc_index].uuid16;
        if (uuid16 == 0) {
            ctx->disc_index++;
            continue;
        }
        ble_uuid16_t uuid = BLE_UUID16_INIT(uuid16);
        int rc = ble_gattc_disc_chrs_by_uuid(ctx->conn_handle, 1, 0xffff, &uuid.u, analysis_chr_cb, ctx);
        if (rc == 0) return;
        ESP_LOGD(TAG, "特征发现启动失败 uuid=0x%04x rc=%d", uuid16, rc);
        ctx->disc_index++;
    }
}

static int analysis_gap_cb(struct ble_gap_event *event, void *arg)
{
    analysis_ctx_t *ctx = (analysis_ctx_t *)arg;
    if (!ctx) return 0;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ctx->connected = true;
            ctx->conn_handle = event->connect.conn_handle;
            ctx->next_read_uuid = 0x2A00;
            device_template_note_service(&ctx->tpl, DIS_SERVICE_UUID);
            analysis_read_next_char(ctx);
            analysis_start_next_disc(ctx);
            if (ctx->done) xSemaphoreGive(ctx->done);
        } else {
            analysis_ctx_finish(ctx, ESP_FAIL);
        }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        {
            analysis_char_t *ch = analysis_find_char_by_handle(ctx, event->notify_rx.attr_handle);
            if (ch) analysis_handle_sample(ctx, ch->uuid16, event->notify_rx.om);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ctx->connected = false;
        if (ctx->disconnected) xSemaphoreGive(ctx->disconnected);
        if (!ctx->done_flag) analysis_ctx_finish(ctx, ESP_OK);
        return 0;
    default:
        return 0;
    }
}

static int read_gap_cb(struct ble_gap_event *event, void *arg)
{
    read_ctx_t *ctx = (read_ctx_t *)arg;
    if (!ctx) return 0;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ctx->connected = true;
            ctx->conn_handle = event->connect.conn_handle;
            ctx->next_char_uuid = 0x2A00;
            device_template_note_service(&ctx->tpl, DIS_SERVICE_UUID);
            read_next_char(ctx);
        } else {
            read_ctx_finish(ctx, ESP_FAIL);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ctx->connected = false;
        if (ctx->disconnected) xSemaphoreGive(ctx->disconnected);
        if (!ctx->done_flag) read_ctx_finish(ctx, ESP_ERR_TIMEOUT);
        return 0;
    default:
        return 0;
    }
}

esp_err_t ble_scanner_read_template(const char *addr, uint8_t addr_type, uint32_t timeout_ms, device_template_t *out)
{
    if (!addr || !out) return ESP_ERR_INVALID_ARG;
    if (!ensure_init()) return ESP_ERR_NO_MEM;
    if (timeout_ms == 0) timeout_ms = BLE_SCAN_READ_TIMEOUT_MS;
    read_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.done = xSemaphoreCreateBinary();
    ctx.disconnected = xSemaphoreCreateBinary();
    if (!ctx.done || !ctx.disconnected) {
        if (ctx.done) vSemaphoreDelete(ctx.done);
        if (ctx.disconnected) vSemaphoreDelete(ctx.disconnected);
        return ESP_ERR_NO_MEM;
    }
    device_template_defaults(&ctx.tpl);
    ctx.tpl.used = true;
    if (!parse_addr_text(addr, ctx.peer.val)) {
        vSemaphoreDelete(ctx.done);
        vSemaphoreDelete(ctx.disconnected);
        return ESP_ERR_INVALID_ARG;
    }
    ctx.peer.type = addr_type;
    device_template_set_addr(&ctx.tpl, ctx.peer.val, ctx.peer.type);
    ble_scanner_stop();
    uint8_t own_addr_type = 0;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        vSemaphoreDelete(ctx.done);
        vSemaphoreDelete(ctx.disconnected);
        return ESP_FAIL;
    }
    struct ble_gap_conn_params params;
    memset(&params, 0, sizeof(params));
    params.scan_itvl = BLE_GAP_SCAN_ITVL_MS(60);
    params.scan_window = BLE_GAP_SCAN_WIN_MS(30);
    params.itvl_min = BLE_GAP_CONN_ITVL_MS(30);
    params.itvl_max = BLE_GAP_CONN_ITVL_MS(60);
    params.latency = 0;
    params.supervision_timeout = BLE_GAP_SUPERVISION_TIMEOUT_MS(4000);
    rc = ble_gap_connect(own_addr_type, &ctx.peer, (int32_t)timeout_ms, &params, read_gap_cb, &ctx);
    if (rc != 0) {
        ESP_LOGW(TAG, "连接外部设备失败 rc=%d", rc);
        vSemaphoreDelete(ctx.done);
        vSemaphoreDelete(ctx.disconnected);
        return ESP_FAIL;
    }
    bool ok = xSemaphoreTake(ctx.done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
    if (!ok) {
        if (ctx.connected) {
            ble_gap_terminate(ctx.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            xSemaphoreTake(ctx.disconnected, pdMS_TO_TICKS(1000));
        } else {
            ble_gap_conn_cancel();
            xSemaphoreTake(ctx.done, pdMS_TO_TICKS(1000));
        }
        vSemaphoreDelete(ctx.done);
        vSemaphoreDelete(ctx.disconnected);
        return ESP_ERR_TIMEOUT;
    }
    if (ctx.connected) {
        ble_gap_terminate(ctx.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        xSemaphoreTake(ctx.disconnected, pdMS_TO_TICKS(1000));
    }
    if (ctx.tpl.alias[0] == '\0') {
        const char *name = ctx.tpl.device_name[0] ? ctx.tpl.device_name : ctx.tpl.addr;
        strncpy(ctx.tpl.alias, name, sizeof(ctx.tpl.alias) - 1);
    }
    device_template_validate(&ctx.tpl);
    *out = ctx.tpl;
    esp_err_t result = ctx.result;
    vSemaphoreDelete(ctx.done);
    vSemaphoreDelete(ctx.disconnected);
    return result;
}

esp_err_t ble_scanner_analyze_device(const char *addr,
                                     uint8_t addr_type,
                                     uint32_t timeout_ms,
                                     device_template_t *tpl_out,
                                     device_analysis_t *analysis_out)
{
    if (!addr || !tpl_out || !analysis_out) return ESP_ERR_INVALID_ARG;
    if (!ensure_init()) return ESP_ERR_NO_MEM;
    if (timeout_ms == 0) timeout_ms = DEVICE_ANALYSIS_SAMPLE_MS;
    analysis_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.done = xSemaphoreCreateBinary();
    ctx.disconnected = xSemaphoreCreateBinary();
    if (!ctx.done || !ctx.disconnected) {
        if (ctx.done) vSemaphoreDelete(ctx.done);
        if (ctx.disconnected) vSemaphoreDelete(ctx.disconnected);
        return ESP_ERR_NO_MEM;
    }
    device_template_defaults(&ctx.tpl);
    device_analysis_defaults(&ctx.analysis);
    ctx.tpl.used = true;
    ctx.chars[0].uuid16 = POWER_CHAR_UUID;
    ctx.chars[1].uuid16 = HR_CHAR_UUID;
    ctx.chars[2].uuid16 = CSC_CHAR_UUID;
    ctx.chars[3].uuid16 = FTMS_POWER_CHAR_UUID;
    ctx.chars[4].uuid16 = FTMS_TREADMILL_CHAR_UUID;
    if (!parse_addr_text(addr, ctx.peer.val)) {
        vSemaphoreDelete(ctx.done);
        vSemaphoreDelete(ctx.disconnected);
        return ESP_ERR_INVALID_ARG;
    }
    ctx.peer.type = addr_type;
    device_template_set_addr(&ctx.tpl, ctx.peer.val, ctx.peer.type);
    ble_scanner_stop();
    uint8_t own_addr_type = 0;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        vSemaphoreDelete(ctx.done);
        vSemaphoreDelete(ctx.disconnected);
        return ESP_FAIL;
    }
    struct ble_gap_conn_params params;
    memset(&params, 0, sizeof(params));
    params.scan_itvl = BLE_GAP_SCAN_ITVL_MS(60);
    params.scan_window = BLE_GAP_SCAN_WIN_MS(30);
    params.itvl_min = BLE_GAP_CONN_ITVL_MS(30);
    params.itvl_max = BLE_GAP_CONN_ITVL_MS(60);
    params.latency = 0;
    params.supervision_timeout = BLE_GAP_SUPERVISION_TIMEOUT_MS(4000);
    rc = ble_gap_connect(own_addr_type, &ctx.peer, (int32_t)timeout_ms, &params, analysis_gap_cb, &ctx);
    if (rc != 0) {
        ESP_LOGW(TAG, "连接外部设备分析失败 rc=%d", rc);
        vSemaphoreDelete(ctx.done);
        vSemaphoreDelete(ctx.disconnected);
        return ESP_FAIL;
    }

    bool connected = xSemaphoreTake(ctx.done, pdMS_TO_TICKS(1500)) == pdTRUE && ctx.connected;
    if (connected) {
        vTaskDelay(pdMS_TO_TICKS(timeout_ms));
        if (ctx.connected) {
            ble_gap_terminate(ctx.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            xSemaphoreTake(ctx.disconnected, pdMS_TO_TICKS(1000));
        }
    } else {
        ble_gap_conn_cancel();
        xSemaphoreTake(ctx.done, pdMS_TO_TICKS(1000));
    }

    if (ctx.tpl.alias[0] == '\0') {
        const char *name = ctx.tpl.device_name[0] ? ctx.tpl.device_name : ctx.tpl.addr;
        strncpy(ctx.tpl.alias, name, sizeof(ctx.tpl.alias) - 1);
    }
    device_analysis_finalize(&ctx.analysis);
    device_template_validate(&ctx.tpl);
    *tpl_out = ctx.tpl;
    *analysis_out = ctx.analysis;
    esp_err_t result = ctx.result == ESP_OK ? ESP_OK : ctx.result;
    vSemaphoreDelete(ctx.done);
    vSemaphoreDelete(ctx.disconnected);
    return result;
}
