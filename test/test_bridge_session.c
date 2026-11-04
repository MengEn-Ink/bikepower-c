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
int64_t esp_timer_get_time(void) { return (int64_t)s_now * 1000; }
int ble_gap_conn_cancel(void)
{
    struct ble_gap_event event = { .type = BLE_GAP_EVENT_CONNECT, .connect.status = 4 };
    s_gap(&event, s_arg);
    return 0;
}
int ble_gap_terminate(uint16_t handle, int reason)
{
    s_terminates++;
    struct ble_gap_event event = { .type = BLE_GAP_EVENT_DISCONNECT,
        .disconnect = { .reason = reason, .conn.conn_handle = handle } };
    s_gap(&event, s_arg);
    return 0;
}
int ble_hs_id_infer_auto(int privacy, uint8_t *type) { (void)privacy; *type = 0; return 0; }
esp_err_t ble_scanner_stop(void) { return ESP_OK; }
int ble_gap_connect(uint8_t type, const ble_addr_t *peer, int timeout, void *params, gap_fn callback, void *arg)
{
    (void)type; (void)peer; (void)timeout; (void)params;
    s_connects++; s_gap = callback; s_arg = arg;
    return 0;
}
int ble_gattc_disc_svc_by_uuid(uint16_t handle, const ble_uuid_t *uuid, service_fn cb, void *arg)
{
    assert(ble_uuid_u16(uuid) == 0x1818);
    struct ble_gatt_svc svc = { 10, 20 };
    struct ble_gatt_error ok = { 0 }, done = { BLE_HS_EDONE };
    cb(handle, &ok, &svc, arg); cb(handle, &done, NULL, arg); return 0;
}
int ble_gattc_disc_all_chrs(uint16_t handle, uint16_t first, uint16_t last, char_fn cb, void *arg)
{
    assert(first == 10 && last == 20);
    struct ble_gatt_chr chr = { .def_handle = 11, .val_handle = 12, .properties = 16,
        .uuid.u16 = BLE_UUID16_INIT(0x2a63) };
    struct ble_gatt_error ok = { 0 }, done = { BLE_HS_EDONE };
    cb(handle, &ok, &chr, arg); cb(handle, &done, NULL, arg); return 0;
}
int ble_gattc_disc_all_dscs(uint16_t handle, uint16_t first, uint16_t last, desc_fn cb, void *arg)
{
    assert(first == 12 && last == 20);
    struct ble_gatt_dsc dsc = { .handle = 13, .uuid.u16 = BLE_UUID16_INIT(0x2902) };
    struct ble_gatt_error ok = { 0 }, done = { BLE_HS_EDONE };
    cb(handle, &ok, first, &dsc, arg); cb(handle, &done, first, NULL, arg); return 0;
}
int ble_gattc_write_flat(uint16_t handle, uint16_t attr, const void *data, int length, attr_fn cb, void *arg)
{
    assert(attr == 13 && length == 2 && ((const uint8_t *)data)[0] == 1);
    struct ble_gatt_error ok = { 0 }; cb(handle, &ok, NULL, arg); return 0;
}
int ble_gattc_read_by_uuid(uint16_t handle, uint16_t first, uint16_t last, const ble_uuid_t *uuid, attr_fn cb, void *arg)
{
    (void)first; (void)last;
    assert(ble_uuid_u16(uuid) == s_read_uuids[s_reads % 7]);
    s_reads++;
    struct os_mbuf om = { .data = "Meter", .length = 5 };
    struct ble_gatt_attr attr = { &om };
    struct ble_gatt_error ok = { 0 }, done = { BLE_HS_EDONE };
    uint8_t index = s_bridge.read_index;
    cb(handle, &ok, &attr, arg);
    assert(s_bridge.read_index == index);
    cb(handle, &done, NULL, arg);
    assert(s_bridge.read_index == index + 1);
    return 0;
}
static bridge_config_t s_config;
static void tick(uint32_t now)
{
    s_now = now;
    ble_power_bridge_tick(&s_config, now);
}
static void connected(void)
{
    struct ble_gap_event event = { .type = BLE_GAP_EVENT_CONNECT,
        .connect = { .status = 0, .conn_handle = 7 } };
    s_gap(&event, s_arg);
    for (int i = 0; i < 11; i++) tick(s_now + 1);
    assert(s_bridge.phase == PHASE_READY && s_reads % 7 == 0);
}
static void notification(uint32_t now)
{
    s_now = now;
    struct os_mbuf om = { .data = { 0, 0, 250, 0 }, .length = 4 };
    struct ble_gap_event event = { .type = BLE_GAP_EVENT_NOTIFY_RX,
        .notify_rx = { .conn_handle = 7, .attr_handle = 12, .om = &om } };
    s_gap(&event, s_arg);
}
int main(void)
{
    power_bridge_config_defaults(&s_config);
    s_config.bound = 1;
    s_config.addr[0] = 42;
    tick(0);
    assert(s_connects == 0);
    assert(ble_power_bridge_request(&s_config, true) == ESP_OK);
    tick(10); connected();
    notification(100); tick(100);
    assert(!ble_power_bridge_status(&s_config, 100).sample.valid);
    notification(1100); tick(1100);
    ble_bridge_status_t status = ble_power_bridge_status(&s_config, 1100);
    assert(status.sample.valid && status.tested && status.sample.power_w == 250);
    assert(!strcmp(status.identity.fields[DEVICE_IDENTITY_SERIAL], "Meter"));
    tick(4100);
    assert(!ble_power_bridge_status(&s_config, 4100).sample.valid);
    assert(s_bridge.status.state == BLE_BRIDGE_STALE);
    tick(11100);
    assert(s_terminates == 1 && !s_bridge.status.connected);
    tick(12099); assert(s_connects == 1);
    tick(12100); assert(s_connects == 2);
    connected();
    ble_power_bridge_portal_stop(); tick(12200); tick(12201);
    assert(!s_bridge.desired && !s_bridge.status.connected);
    assert(ble_power_bridge_request(&s_config, false) == ESP_OK);
    tick(20000); connected();
    ble_power_bridge_portal_stop(); tick(20100);
    assert(s_bridge.desired && s_bridge.status.connected);
    void *old_arg = s_arg;
    ble_power_bridge_request(&s_config, true); tick(20200); tick(20201);
    assert(s_bridge.connecting);
    struct ble_gap_event stale = { .type = BLE_GAP_EVENT_CONNECT,
        .connect = { .status = 0, .conn_handle = 99 } };
    gap_cb(&stale, old_arg);
    assert(s_bridge.handle == BLE_HS_CONN_HANDLE_NONE);
    connected(); tick(50201);
    assert(!s_bridge.desired && !s_bridge.status.connected);
    tick(100000); assert(s_connects == 4);
    ble_power_bridge_request(&s_config, false); tick(110000); connected();
    ble_power_bridge_radio_stop(); tick(120000);
    assert(!s_bridge.desired && !s_bridge.status.connected);
    ble_power_bridge_request(&s_config, true); tick(130000); connected();
    notification(131000); notification(132000); tick(132000);
    int connections_before = s_connects;
    ble_power_bridge_request(&s_config, false); tick(133000);
    assert(s_connects == connections_before && s_bridge.status.connected);
    assert(!s_bridge.status.temporary && s_bridge.status.tested);
    assert(ble_power_bridge_status(&s_config, 133000).sample.valid);
    ble_power_bridge_host_reset(); tick(134000);
    assert(s_bridge.connecting && !s_bridge.status.connected);
    connected();
    puts("桥接会话：发现/CCCD/七字段/两样本/过期/重连/取消/代次/关闭通过");
    return 0;
}
