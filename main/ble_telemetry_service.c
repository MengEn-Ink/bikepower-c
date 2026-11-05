#include <stdatomic.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "nimble/nimble_port.h"
#include "host/ble_gap.h"
#include "host/ble_att.h"
#include "bikepower_config.h"
#include "device_telemetry_runtime.h"
#include "ble_telemetry_service.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_records[TELEMETRY_GROUP_COUNT][TELEMETRY_RECORD_LEN];
static uint16_t s_handles[TELEMETRY_GROUP_COUNT];
static bool s_subscribed[TELEMETRY_GROUP_COUNT];
static telemetry_snapshot_t s_link;
static uint32_t s_submitted, s_failed, s_allocation_failed, s_lock_skipped;
static atomic_uint_least16_t s_connection = BLE_HS_CONN_HANDLE_NONE;
static atomic_bool s_running;
static struct ble_npl_callout s_rssi_callout;
static const ble_uuid128_t s_service = BLE_UUID128_INIT(BLE_TELEMETRY_UUID_BYTES(0));
static const ble_uuid128_t s_meta = BLE_UUID128_INIT(BLE_TELEMETRY_UUID_BYTES(1));
static const ble_uuid128_t s_uuids[TELEMETRY_GROUP_COUNT] = {
    BLE_UUID128_INIT(BLE_TELEMETRY_UUID_BYTES(2)),
    BLE_UUID128_INIT(BLE_TELEMETRY_UUID_BYTES(3)),
    BLE_UUID128_INIT(BLE_TELEMETRY_UUID_BYTES(4)),
    BLE_UUID128_INIT(BLE_TELEMETRY_UUID_BYTES(5)),
};

static int access_cb(uint16_t connection, uint16_t handle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)connection; (void)handle;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
    uint8_t value[TELEMETRY_RECORD_LEN];
    unsigned group = (unsigned)(uintptr_t)arg;
    if (group == TELEMETRY_GROUP_COUNT) device_telemetry_meta(value);
    else {
        portENTER_CRITICAL(&s_lock);
        memcpy(value, s_records[group], sizeof(value));
        portEXIT_CRITICAL(&s_lock);
    }
    return os_mbuf_append(ctxt->om, value, sizeof(value)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static struct ble_gatt_chr_def s_characteristics[TELEMETRY_GROUP_COUNT + 2];
static struct ble_gatt_svc_def s_services[2];

static void read_link(struct ble_npl_event *event)
{
    (void)event;
    if (!s_running) return;
    uint16_t connection = s_connection;
    telemetry_snapshot_t link = {0};
    struct ble_gap_conn_desc desc;
    if (connection != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(connection, &desc) == 0) {
        link.connected = true;
        link.interval = desc.conn_itvl;
        link.latency = desc.conn_latency;
        link.timeout = desc.supervision_timeout;
        link.mtu = ble_att_mtu(connection);
        link.rssi_valid = ble_gap_conn_rssi(connection, &link.rssi) == 0
            && link.rssi != 127;
    }
    portENTER_CRITICAL(&s_lock);
    if (s_running && connection == s_connection) s_link = link;
    portEXIT_CRITICAL(&s_lock);
    if (s_running) ble_npl_callout_reset(&s_rssi_callout,
        ble_npl_time_ms_to_ticks32(BLE_TELEMETRY_INTERVAL_MS));
}

int ble_telemetry_service_register(void)
{
    memset(s_handles, 0, sizeof(s_handles));
    memset(s_characteristics, 0, sizeof(s_characteristics));
    s_characteristics[0] = (struct ble_gatt_chr_def){
        .uuid = &s_meta.u, .access_cb = access_cb,
        .arg = (void *)(uintptr_t)TELEMETRY_GROUP_COUNT, .flags = BLE_GATT_CHR_F_READ,
    };
    for (unsigned i = 0; i < TELEMETRY_GROUP_COUNT; ++i) {
        s_characteristics[i + 1] = (struct ble_gatt_chr_def){
            .uuid = &s_uuids[i].u, .access_cb = access_cb, .arg = (void *)(uintptr_t)i,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_handles[i],
        };
    }
    s_services[0] = (struct ble_gatt_svc_def){
        .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &s_service.u,
        .characteristics = s_characteristics,
    };
    ble_telemetry_service_disconnect();
    int init_rc = ble_npl_callout_init(&s_rssi_callout, nimble_port_get_dflt_eventq(), read_link, NULL);
    if (init_rc) return BLE_HS_ENOMEM;
    int rc = ble_gatts_count_cfg(s_services);
    return rc ? rc : ble_gatts_add_svcs(s_services);
}

void ble_telemetry_service_sync(void)
{
    s_running = true;
    ble_npl_callout_reset(&s_rssi_callout, ble_npl_time_ms_to_ticks32(BLE_TELEMETRY_INTERVAL_MS));
}

void ble_telemetry_service_disconnect(void)
{
    portENTER_CRITICAL(&s_lock);
    s_connection = BLE_HS_CONN_HANDLE_NONE;
    memset(s_subscribed, 0, sizeof(s_subscribed));
    s_link = (telemetry_snapshot_t){0};
    portEXIT_CRITICAL(&s_lock);
}

void ble_telemetry_service_stop(void)
{
    s_running = false;
    ble_npl_callout_stop(&s_rssi_callout);
    ble_telemetry_service_disconnect();
}

void ble_telemetry_service_deinit(void)
{
    ble_npl_callout_deinit(&s_rssi_callout);
}

void ble_telemetry_service_connect(uint16_t connection)
{
    ble_telemetry_service_disconnect();
    s_connection = connection;
}

void ble_telemetry_service_subscribe(uint16_t handle, bool notify)
{
    portENTER_CRITICAL(&s_lock);
    for (unsigned i = 0; i < TELEMETRY_GROUP_COUNT; ++i) {
        if (s_handles[i] == handle) s_subscribed[i] = notify;
    }
    portEXIT_CRITICAL(&s_lock);
}

void ble_telemetry_service_publish(telemetry_snapshot_t *s)
{
    uint8_t records[TELEMETRY_GROUP_COUNT][TELEMETRY_RECORD_LEN];
    portENTER_CRITICAL(&s_lock);
    s->connected = s_link.connected; s->rssi_valid = s_link.rssi_valid;
    s->rssi = s_link.rssi; s->mtu = s_link.mtu; s->interval = s_link.interval;
    s->latency = s_link.latency; s->timeout = s_link.timeout;
    s->submitted = s_submitted; s->failed = s_failed;
    s->allocation_failed = s_allocation_failed; s->lock_skipped = s_lock_skipped;
    portEXIT_CRITICAL(&s_lock);
    for (unsigned i = 0; i < TELEMETRY_GROUP_COUNT; ++i) telemetry_encode(i, s, records[i], sizeof(records[i]));
    portENTER_CRITICAL(&s_lock);
    memcpy(s_records, records, sizeof(records));
    portEXIT_CRITICAL(&s_lock);
}

void ble_telemetry_service_notify(void (*send)(uint16_t, const uint8_t *, size_t))
{
    uint8_t records[TELEMETRY_GROUP_COUNT][TELEMETRY_RECORD_LEN];
    bool subscriptions[TELEMETRY_GROUP_COUNT];
    portENTER_CRITICAL(&s_lock);
    memcpy(records, s_records, sizeof(records));
    memcpy(subscriptions, s_subscribed, sizeof(subscriptions));
    portEXIT_CRITICAL(&s_lock);
    for (unsigned i = 0; i < TELEMETRY_GROUP_COUNT; ++i) {
        if (subscriptions[i] && records[i][0] == TELEMETRY_VERSION) send(s_handles[i], records[i], sizeof(records[i]));
    }
}

void ble_telemetry_service_tx(int result, bool allocation_failed)
{
    portENTER_CRITICAL(&s_lock);
    if (allocation_failed) ++s_allocation_failed;
    else if (result) ++s_failed;
    else ++s_submitted;
    portEXIT_CRITICAL(&s_lock);
}

void ble_telemetry_service_lock_skipped(void)
{
    portENTER_CRITICAL(&s_lock);
    ++s_lock_skipped;
    portEXIT_CRITICAL(&s_lock);
}
