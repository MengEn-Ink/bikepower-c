#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"

#include "bikepower_config.h"
#include "ble_scanner.h"
#include "device_identity.h"

typedef struct {
    bool used;
    bool cps;
    ble_addr_t address;
    char name[BRIDGE_NAME_MAX];
    int8_t rssi;
} candidate_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static candidate_t s_candidates[BLE_SCAN_MAX_RESULTS];
static uint32_t s_generation;
static bool s_scanning;

static int scan_cb(struct ble_gap_event *event, void *arg)
{
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        portENTER_CRITICAL(&s_lock);
        if ((uint32_t)(uintptr_t)arg == s_generation) s_scanning = false;
        portEXIT_CRITICAL(&s_lock);
    } else if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_hs_adv_fields fields = { 0 };
        const struct ble_gap_disc_desc *desc = &event->disc;
        if (desc->addr.type > 1 || ble_hs_adv_parse_fields(&fields, desc->data, desc->length_data)) return 0;
        candidate_t next = { .used = true, .address = desc->addr, .rssi = desc->rssi };
        for (int i = 0; i < fields.num_uuids16; i++) {
            if (fields.uuids16[i].value == POWER_SERVICE_UUID) next.cps = true;
        }
        if (fields.name && device_identity_text_valid(fields.name, fields.name_len)) {
            memcpy(next.name, fields.name, fields.name_len);
            next.name[fields.name_len] = '\0';
        }
        portENTER_CRITICAL(&s_lock);
        if (s_scanning && (uint32_t)(uintptr_t)arg == s_generation) {
            int slot = -1;
            int free_slot = -1;
            for (int i = 0; i < BLE_SCAN_MAX_RESULTS; i++) {
                candidate_t *item = &s_candidates[i];
                if (item->used && item->address.type == next.address.type
                        && memcmp(item->address.val, next.address.val, 6) == 0) {
                    slot = i;
                    break;
                }
                if (!item->used || !item->cps) free_slot = i;
            }
            if (slot < 0) {
                slot = free_slot;
                if (slot >= 0) s_candidates[slot] = next;
            } else {
                s_candidates[slot].cps |= next.cps;
                s_candidates[slot].rssi = next.rssi;
                if (next.name[0]) memcpy(s_candidates[slot].name, next.name, sizeof(next.name));
            }
        }
        portEXIT_CRITICAL(&s_lock);
    }
    return 0;
}

esp_err_t ble_scanner_start(uint32_t duration_ms)
{
    if (!duration_ms) duration_ms = BLE_SCAN_DEFAULT_MS;
    if (duration_ms > BLE_BRIDGE_TEST_MS) return ESP_ERR_INVALID_ARG;
    uint8_t own_type;
    if (ble_hs_id_infer_auto(0, &own_type)) return ESP_FAIL;
    portENTER_CRITICAL(&s_lock);
    if (s_scanning) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_generation = (s_generation + 1) & 0x07ffffffu;
    if (!s_generation) s_generation = 1;
    uint32_t generation = s_generation;
    memset(s_candidates, 0, sizeof(s_candidates));
    s_scanning = true;
    portEXIT_CRITICAL(&s_lock);
    struct ble_gap_disc_params params = { 0 };
    params.itvl = BLE_GAP_SCAN_ITVL_MS(60);
    params.window = BLE_GAP_SCAN_WIN_MS(30);
    params.filter_duplicates = 0;
    int rc = ble_gap_disc(own_type, duration_ms, &params, scan_cb, (void *)(uintptr_t)generation);
    if (rc) {
        portENTER_CRITICAL(&s_lock);
        s_scanning = false;
        portEXIT_CRITICAL(&s_lock);
    }
    return rc ? ESP_FAIL : ESP_OK;
}

esp_err_t ble_scanner_stop(void)
{
    portENTER_CRITICAL(&s_lock);
    bool active = s_scanning;
    s_scanning = false;
    portEXIT_CRITICAL(&s_lock);
    if (!active) return ESP_OK;
    int rc = ble_gap_disc_cancel();
    return rc && rc != BLE_HS_EALREADY ? ESP_FAIL : ESP_OK;
}

bool ble_scanner_is_scanning(void)
{
    portENTER_CRITICAL(&s_lock);
    bool active = s_scanning;
    portEXIT_CRITICAL(&s_lock);
    return active;
}

bool ble_scanner_candidate(uint32_t candidate, bridge_device_t *out)
{
    if (!out) return false;
    unsigned slot = candidate & 15u;
    uint32_t generation = candidate >> 4;
    portENTER_CRITICAL(&s_lock);
    bool valid = generation == s_generation && slot < BLE_SCAN_MAX_RESULTS
        && s_candidates[slot].used && s_candidates[slot].cps;
    candidate_t item = valid ? s_candidates[slot] : (candidate_t) { 0 };
    portEXIT_CRITICAL(&s_lock);
    if (!valid) return false;
    *out = (bridge_device_t){0};
    out->bound = 1;
    out->addr_type = item.address.type;
    memcpy(out->addr, item.address.val, sizeof(out->addr));
    memcpy(out->name, item.name, sizeof(out->name));
    return true;
}

static size_t escape_name(const char *input, char *output)
{
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)input; *p; p++) {
        if (*p == '"' || *p == '\\') output[used++] = '\\';
        output[used++] = *p;
    }
    output[used] = '\0';
    return used;
}

size_t ble_scanner_serialize_results(char *out, size_t capacity)
{
    candidate_t items[BLE_SCAN_MAX_RESULTS];
    portENTER_CRITICAL(&s_lock);
    memcpy(items, s_candidates, sizeof(items));
    bool active = s_scanning;
    uint32_t generation = s_generation;
    portEXIT_CRITICAL(&s_lock);
    int n = snprintf(out, capacity, "{\"scanning\":%s,\"devices\":[", active ? "true" : "false");
    if (n < 0 || (size_t)n >= capacity) return 0;
    size_t used = n;
    bool first = true;
    for (unsigned i = 0; i < BLE_SCAN_MAX_RESULTS; i++) {
        if (!items[i].used || !items[i].cps) continue;
        char name[BRIDGE_NAME_MAX * 2];
        escape_name(items[i].name, name);
        n = snprintf(out + used, capacity - used,
            "%s{\"candidate\":%lu,\"name\":\"%s\",\"rssi\":%d,\"addr_type\":%u,"
            "\"address\":\"%02x%02x%02x%02x%02x%02x\"}",
            first ? "" : ",", (unsigned long)((generation << 4) | i), name,
            items[i].rssi, items[i].address.type,
            items[i].address.val[0], items[i].address.val[1], items[i].address.val[2],
            items[i].address.val[3], items[i].address.val[4], items[i].address.val[5]);
        if (n < 0 || (size_t)n >= capacity - used) return 0;
        used += n;
        first = false;
    }
    if (used + 3 > capacity) return 0;
    memcpy(out + used, "]}", 3);
    return used + 2;
}
