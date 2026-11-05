#include <assert.h>
#include <stdio.h>

#include "bridge_stubs.h"
#include "../main/ble_power_bridge.c"

static uint32_t s_now;
static int s_connects;
static int s_terminates;
static int s_reads;
static gap_fn s_gap;
static void *s_arg;
static bridge_config_t s_calibration;
static bridge_device_t s_peer;

int64_t esp_timer_get_time(void) { return (int64_t)s_now * 1000; }

int ble_gap_conn_cancel(void)
{
    struct ble_gap_event event = {
        .type = BLE_GAP_EVENT_CONNECT,
        .connect.status = BLE_HS_ETIMEOUT,
    };
    s_gap(&event, s_arg);
    return 0;
}

int ble_gap_terminate(uint16_t handle, int reason)
{
    s_terminates++;
    struct ble_gap_event event = {
        .type = BLE_GAP_EVENT_DISCONNECT,
        .disconnect = {
            .reason = reason,
            .conn.conn_handle = handle,
        },
    };
    s_gap(&event, s_arg);
    return 0;
}

int ble_hs_id_infer_auto(int privacy, uint8_t *type)
{
    (void)privacy;
    *type = 0;
    return 0;
}

esp_err_t ble_scanner_stop(void) { return ESP_OK; }

int ble_gap_connect(uint8_t type, const ble_addr_t *peer, int timeout,
                    void *params, gap_fn callback, void *arg)
{
    (void)type;
    (void)peer;
    (void)timeout;
    (void)params;
    s_connects++;
    s_gap = callback;
    s_arg = arg;
    return 0;
}

int ble_gattc_disc_svc_by_uuid(uint16_t handle, const ble_uuid_t *uuid,
                               service_fn cb, void *arg)
{
    assert(ble_uuid_u16(uuid) == 0x1818);
    struct ble_gatt_svc svc = {10, 20};
    struct ble_gatt_error ok = {0};
    struct ble_gatt_error done = {BLE_HS_EDONE};
    cb(handle, &ok, &svc, arg);
    cb(handle, &done, NULL, arg);
    return 0;
}

int ble_gattc_disc_all_chrs(uint16_t handle, uint16_t first, uint16_t last,
                            char_fn cb, void *arg)
{
    assert(first == 10 && last == 20);
    struct ble_gatt_chr chr = {
        .def_handle = 11,
        .val_handle = 12,
        .properties = BLE_GATT_CHR_PROP_NOTIFY,
        .uuid.u16 = BLE_UUID16_INIT(0x2a63),
    };
    struct ble_gatt_error ok = {0};
    struct ble_gatt_error done = {BLE_HS_EDONE};
    cb(handle, &ok, &chr, arg);
    cb(handle, &done, NULL, arg);
    return 0;
}

int ble_gattc_disc_all_dscs(uint16_t handle, uint16_t first, uint16_t last,
                            desc_fn cb, void *arg)
{
    assert(first == 12 && last == 20);
    struct ble_gatt_dsc dsc = {
        .handle = 13,
        .uuid.u16 = BLE_UUID16_INIT(0x2902),
    };
    struct ble_gatt_error ok = {0};
    struct ble_gatt_error done = {BLE_HS_EDONE};
    cb(handle, &ok, first, &dsc, arg);
    cb(handle, &done, first, NULL, arg);
    return 0;
}

int ble_gattc_write_flat(uint16_t handle, uint16_t attr, const void *data,
                         int length, attr_fn cb, void *arg)
{
    assert(attr == 13 && length == 2 && ((const uint8_t *)data)[0] == 1);
    struct ble_gatt_error ok = {0};
    cb(handle, &ok, NULL, arg);
    return 0;
}

int ble_gattc_read_by_uuid(uint16_t handle, uint16_t first, uint16_t last,
                           const ble_uuid_t *uuid, attr_fn cb, void *arg)
{
    (void)first;
    (void)last;
    assert(ble_uuid_u16(uuid) == s_read_uuids[s_reads % 7]);
    s_reads++;
    struct os_mbuf om = {.data = "Meter", .length = 5};
    struct ble_gatt_attr attr = {&om};
    struct ble_gatt_error ok = {0};
    struct ble_gatt_error done = {BLE_HS_EDONE};
    uint8_t index = s_bridge.read_index;
    cb(handle, &ok, &attr, arg);
    assert(s_bridge.read_index == index);
    cb(handle, &done, NULL, arg);
    assert(s_bridge.read_index == index + 1);
    return 0;
}

static void reset_session(uint8_t tail)
{
    s_now = 0;
    s_connects = 0;
    s_terminates = 0;
    s_reads = 0;
    s_gap = NULL;
    s_arg = NULL;
    s_bridge = (bridge_context_t){.handle = BLE_HS_CONN_HANDLE_NONE};
    power_bridge_config_defaults(&s_calibration);
    s_peer = (bridge_device_t){
        .bound = 1,
        .addr_type = 0,
        .addr = {0, 0, 0, 0, 0, tail},
    };
    snprintf(s_peer.name, sizeof(s_peer.name), "Meter-%u", tail);
}

static void tick(uint32_t now)
{
    s_now = now;
    ble_power_bridge_tick(&s_calibration, now);
}

static void connected(void)
{
    struct ble_gap_event event = {
        .type = BLE_GAP_EVENT_CONNECT,
        .connect = {.status = 0, .conn_handle = 7},
    };
    s_gap(&event, s_arg);
    for (int i = 0; i < 11; i++) tick(s_now + 1);
    assert(s_bridge.phase == PHASE_READY && s_reads % 7 == 0);
}

static void notification(uint32_t now, uint16_t power)
{
    s_now = now;
    struct os_mbuf om = {
        .data = {0, 0, (uint8_t)power, (uint8_t)(power >> 8)},
        .length = 4,
    };
    struct ble_gap_event event = {
        .type = BLE_GAP_EVENT_NOTIFY_RX,
        .notify_rx = {
            .conn_handle = 7,
            .attr_handle = 12,
            .om = &om,
        },
    };
    s_gap(&event, s_arg);
}

static void failed_connect(void)
{
    struct ble_gap_event event = {
        .type = BLE_GAP_EVENT_CONNECT,
        .connect.status = BLE_HS_ETIMEOUT,
    };
    s_gap(&event, s_arg);
}

static void connect_failure_is_terminal(void)
{
    reset_session(0x11);
    assert(ble_power_bridge_request(&s_peer, false) == ESP_OK);
    tick(100);
    assert(s_connects == 1);
    failed_connect();
    assert(ble_power_bridge_status(&s_calibration, 100).state
           == BLE_BRIDGE_ERROR);
    int connects_before = s_connects;
    tick(9000);
    tick(60000);
    assert(s_connects == connects_before);
    assert(ble_power_bridge_status(&s_calibration, 60000).state
           == BLE_BRIDGE_ERROR);
}

static void samples_expire_without_internal_retry(void)
{
    reset_session(0x22);
    assert(ble_power_bridge_request(&s_peer, false) == ESP_OK);
    tick(10);
    connected();
    notification(100, 250);
    tick(100);
    assert(!ble_power_bridge_status(&s_calibration, 100).sample.valid);
    notification(1100, 250);
    tick(1100);
    ble_bridge_status_t status =
        ble_power_bridge_status(&s_calibration, 1100);
    assert(status.sample.valid && status.tested
           && status.sample.power_w == 250);
    assert(!strcmp(status.identity.fields[DEVICE_IDENTITY_SERIAL], "Meter"));

    tick(4100);
    assert(!ble_power_bridge_status(&s_calibration, 4100).sample.valid);
    assert(s_bridge.status.state == BLE_BRIDGE_STALE);
    tick(11100);
    assert(s_terminates == 1 && !s_bridge.status.connected);
    assert(s_bridge.status.state == BLE_BRIDGE_ERROR);
    int connects_before = s_connects;
    tick(60000);
    assert(s_connects == connects_before);
}

static void temporary_session_stops_after_thirty_seconds(void)
{
    reset_session(0x33);
    assert(ble_power_bridge_request(&s_peer, true) == ESP_OK);
    tick(100000);
    connected();
    notification(100100, 180);
    notification(101100, 180);
    notification(109000, 180);
    notification(118000, 180);
    notification(127000, 180);
    tick(129999);
    assert(s_bridge.status.connected);
    tick(130000);
    assert(!s_bridge.desired && !s_bridge.status.connected);
    assert(s_bridge.status.state == BLE_BRIDGE_IDLE);
    int connects_before = s_connects;
    tick(200000);
    assert(s_connects == connects_before);
}

static void generation_and_promotion_are_isolated(void)
{
    reset_session(0x44);
    assert(ble_power_bridge_request(&s_peer, true) == ESP_OK);
    tick(100);
    void *old_arg = s_arg;

    bridge_device_t replacement = s_peer;
    replacement.addr[5] = 0x55;
    strcpy(replacement.name, "Replacement");
    assert(ble_power_bridge_request(&replacement, false) == ESP_OK);
    tick(200);
    tick(201);
    assert(s_bridge.connecting);
    assert(bridge_device_equal(&s_bridge.status.peer, &replacement));

    struct ble_gap_event stale = {
        .type = BLE_GAP_EVENT_CONNECT,
        .connect = {.status = 0, .conn_handle = 99},
    };
    gap_cb(&stale, old_arg);
    assert(s_bridge.handle == BLE_HS_CONN_HANDLE_NONE);
    connected();
    notification(300, 200);
    notification(1300, 200);
    tick(1300);

    int connects_before = s_connects;
    assert(ble_power_bridge_request(&replacement, false) == ESP_OK);
    tick(1400);
    assert(s_connects == connects_before && s_bridge.status.connected);
    assert(!s_bridge.status.temporary && s_bridge.status.tested);
    assert(ble_power_bridge_status(&s_calibration, 1400).sample.valid);

    tick(1500);
    assert(s_bridge.desired && s_bridge.status.connected);
    ble_power_bridge_host_reset();
    tick(60000);
    assert(!s_bridge.connecting && !s_bridge.status.connected);
    assert(s_connects == connects_before);
}

int main(void)
{
    connect_failure_is_terminal();
    samples_expire_without_internal_retry();
    temporary_session_stops_after_thirty_seconds();
    generation_and_promotion_are_isolated();
    puts("桥接会话：单候选终态/通知门槛/过期/临时测试/代次隔离通过");
    return 0;
}
