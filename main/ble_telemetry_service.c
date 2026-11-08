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
typedef struct {
    uint16_t connection;
    uint32_t generation;
    bool subscribed[TELEMETRY_GROUP_COUNT];
    telemetry_snapshot_t link;
    uint8_t records[TELEMETRY_GROUP_COUNT][TELEMETRY_RECORD_LEN];
} telemetry_client_t;
static telemetry_client_t s_clients[BLE_DOWNSTREAM_MAX] = {
    {.connection = BLE_HS_CONN_HANDLE_NONE}, {.connection = BLE_HS_CONN_HANDLE_NONE},
};
static uint8_t s_records[TELEMETRY_GROUP_COUNT][TELEMETRY_RECORD_LEN];
static uint16_t s_handles[TELEMETRY_GROUP_COUNT];
static telemetry_snapshot_t s_snapshot;
static uint32_t s_submitted, s_failed, s_allocation_failed, s_lock_skipped;
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

static telemetry_client_t *find_client(uint16_t connection)
{
    if (connection == BLE_HS_CONN_HANDLE_NONE) return NULL;
    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i)
        if (s_clients[i].connection == connection) return &s_clients[i];
    return NULL;
}

static void encode_records(const telemetry_snapshot_t *snapshot, const telemetry_snapshot_t *link,
                           uint8_t records[TELEMETRY_GROUP_COUNT][TELEMETRY_RECORD_LEN])
{
    telemetry_snapshot_t value = *snapshot;
    value.connected = link->connected;
    value.rssi_valid = link->rssi_valid;
    value.rssi = link->rssi;
    value.mtu = link->mtu;
    value.interval = link->interval;
    value.latency = link->latency;
    value.timeout = link->timeout;
    for (unsigned i = 0; i < TELEMETRY_GROUP_COUNT; ++i)
        telemetry_encode(i, &value, records[i], TELEMETRY_RECORD_LEN);
}

static int access_cb(uint16_t connection, uint16_t handle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)handle;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
    uint8_t value[TELEMETRY_RECORD_LEN];
    unsigned group = (unsigned)(uintptr_t)arg;
    if (group == TELEMETRY_GROUP_COUNT) device_telemetry_meta(value);
    else {
        portENTER_CRITICAL(&s_lock);
        telemetry_client_t *client = find_client(connection);
        memcpy(value, client ? client->records[group] : s_records[group], sizeof(value));
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
    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i) {
        portENTER_CRITICAL(&s_lock);
        uint16_t connection = s_clients[i].connection;
        uint32_t generation = s_clients[i].generation;
        portEXIT_CRITICAL(&s_lock);
        telemetry_snapshot_t link = {0};
        struct ble_gap_conn_desc desc;
        if (connection != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(connection, &desc) == 0) {
            link.connected = true;
            link.interval = desc.conn_itvl;
            link.latency = desc.conn_latency;
            link.timeout = desc.supervision_timeout;
            link.mtu = ble_att_mtu(connection);
            link.rssi_valid = ble_gap_conn_rssi(connection, &link.rssi) == 0 && link.rssi != 127;
        }
        portENTER_CRITICAL(&s_lock);
        if (s_running && connection == s_clients[i].connection && generation == s_clients[i].generation)
            s_clients[i].link = link;
        portEXIT_CRITICAL(&s_lock);
    }
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
    ble_telemetry_service_disconnect(BLE_HS_CONN_HANDLE_NONE);
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

void ble_telemetry_service_disconnect(uint16_t connection)
{
    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i) {
        if (connection == BLE_HS_CONN_HANDLE_NONE || s_clients[i].connection == connection) {
            s_clients[i].connection = BLE_HS_CONN_HANDLE_NONE;
            ++s_clients[i].generation;
            memset(s_clients[i].subscribed, 0, sizeof(s_clients[i].subscribed));
            s_clients[i].link = (telemetry_snapshot_t){0};
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

void ble_telemetry_service_stop(void)
{
    s_running = false;
    ble_npl_callout_stop(&s_rssi_callout);
    ble_telemetry_service_disconnect(BLE_HS_CONN_HANDLE_NONE);
}

void ble_telemetry_service_deinit(void)
{
    ble_npl_callout_deinit(&s_rssi_callout);
}

void ble_telemetry_service_connect(uint16_t connection)
{
    portENTER_CRITICAL(&s_lock);
    if (!find_client(connection)) for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i) {
        if (s_clients[i].connection == BLE_HS_CONN_HANDLE_NONE) {
            s_clients[i].connection = connection;
            ++s_clients[i].generation;
            s_clients[i].link = (telemetry_snapshot_t){.connected = true};
            memset(s_clients[i].subscribed, 0, sizeof(s_clients[i].subscribed));
            encode_records(&s_snapshot, &s_clients[i].link, s_clients[i].records);
            break;
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

void ble_telemetry_service_subscribe(uint16_t connection, uint16_t handle, bool notify)
{
    portENTER_CRITICAL(&s_lock);
    telemetry_client_t *client = find_client(connection);
    if (client) for (unsigned i = 0; i < TELEMETRY_GROUP_COUNT; ++i)
        if (s_handles[i] == handle) client->subscribed[i] = notify;
    portEXIT_CRITICAL(&s_lock);
}

void ble_telemetry_service_publish(telemetry_snapshot_t *snapshot)
{
    portENTER_CRITICAL(&s_lock);
    snapshot->submitted = s_submitted;
    snapshot->failed = s_failed;
    snapshot->allocation_failed = s_allocation_failed;
    snapshot->lock_skipped = s_lock_skipped;
    s_snapshot = *snapshot;
    telemetry_snapshot_t empty = {0};
    encode_records(snapshot, &empty, s_records);
    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i)
        encode_records(snapshot, &s_clients[i].link, s_clients[i].records);
    portEXIT_CRITICAL(&s_lock);
}

void ble_telemetry_service_notify(void (*send)(uint16_t, uint16_t, const uint8_t *, size_t))
{
    telemetry_client_t clients[BLE_DOWNSTREAM_MAX];
    portENTER_CRITICAL(&s_lock);
    memcpy(clients, s_clients, sizeof(clients));
    portEXIT_CRITICAL(&s_lock);
    for (size_t c = 0; c < BLE_DOWNSTREAM_MAX; ++c) {
        for (unsigned i = 0; i < TELEMETRY_GROUP_COUNT; ++i) {
            if (clients[c].connection != BLE_HS_CONN_HANDLE_NONE && clients[c].subscribed[i]
                    && clients[c].records[i][0] == TELEMETRY_VERSION)
                send(clients[c].connection, s_handles[i], clients[c].records[i], TELEMETRY_RECORD_LEN);
        }
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
