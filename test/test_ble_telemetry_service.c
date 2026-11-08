#include <assert.h>
#include "telemetry_service_stubs.h"
unsigned s_lock_depth;
#include "../main/ble_telemetry_service.c"

void device_telemetry_meta(uint8_t out[TELEMETRY_RECORD_LEN])
{
    assert(!s_lock_depth);
    memset(out, 0, TELEMETRY_RECORD_LEN);
    out[0] = 1;
}

static unsigned s_sent;
static uint16_t s_expected_sequence = 1;
static telemetry_snapshot_t s_new;
static void send(uint16_t connection, uint16_t handle, const uint8_t *data, size_t length)
{
    assert(connection == 1);
    assert(!s_lock_depth);
    assert(handle >= 11 && handle <= 14 && length == 20);
    assert(data[2] == s_expected_sequence && data[3] == 0);
    if (++s_sent == 1) {
        s_new.sequence = 2;
        ble_telemetry_service_publish(&s_new);
    }
}

static unsigned s_multi_sent[3];
static void send_multi(uint16_t connection, uint16_t handle, const uint8_t *data, size_t length)
{
    assert(connection == 1 || connection == 2);
    assert(!s_lock_depth && handle >= 11 && handle <= 14 && length == 20);
    assert(data[2] == 1 && data[3] == 0);
    ++s_multi_sent[connection];
    if (connection == 1 && s_multi_sent[1] == 1) {
        telemetry_snapshot_t next = {.sequence = 2};
        ble_telemetry_service_publish(&next);
    }
}

int main(void)
{
    assert(!ble_telemetry_service_register());
    telemetry_snapshot_t s = {.sequence=1,.free_heap=81920,.total_heap=327680};
    ble_telemetry_service_publish(&s);
    ble_telemetry_service_notify(send);
    assert(!s_sent && !s_queries);
    ble_telemetry_service_connect(1);
    ble_telemetry_service_sync();
    read_link(NULL);
    assert(s_queries == 1 && s_callout_resets == 2);
    for(unsigned i=0;i<4;i++)ble_telemetry_service_subscribe(1, s_handles[i], true);
    ble_telemetry_service_publish(&s);
    ble_telemetry_service_notify(send);
    assert(s_sent==4);
    for(unsigned i=0;i<4;i++)ble_telemetry_service_subscribe(1, s_handles[i], false);
    ble_telemetry_service_notify(send);
    assert(s_sent==4);
    struct os_mbuf om={0};
    struct ble_gatt_access_ctxt ctxt={BLE_GATT_ACCESS_OP_READ_CHR,&om};
    unsigned queries=s_queries;
    assert(!access_cb(1,11,&ctxt,NULL));
    assert(om.length==20 && s_queries==queries);
    ctxt.op=BLE_GATT_ACCESS_OP_WRITE_CHR;
    assert(access_cb(1,11,&ctxt,NULL)==BLE_ATT_ERR_READ_NOT_PERMITTED);
    s_test_rssi=127;
    read_link(NULL);
    ble_telemetry_service_publish(&s);
    assert(!s_clients[0].link.rssi_valid);
    s_test_rssi=-65;s_test_rssi_error=1;
    read_link(NULL);ble_telemetry_service_publish(&s);
    assert(!s_clients[0].link.rssi_valid);
    ble_telemetry_service_tx(0,false);ble_telemetry_service_tx(3,false);
    ble_telemetry_service_tx(0,true);ble_telemetry_service_lock_skipped();
    ble_telemetry_service_publish(&s);
    assert(s.submitted==1 && s.failed==1 && s.allocation_failed==1 && s.lock_skipped==1);
    ble_telemetry_service_disconnect(1);ble_telemetry_service_publish(&s);
    assert(!s_clients[0].link.connected && !s_clients[0].link.rssi_valid);
    ble_telemetry_service_connect(1);ble_telemetry_service_connect(2);
    s_test_rssi_error=0;read_link(NULL);
    assert(s_clients[0].link.interval==24 && s_clients[1].link.interval==40);
    for(unsigned i=0;i<4;i++) {
        ble_telemetry_service_subscribe(1,s_handles[i],true);
        ble_telemetry_service_subscribe(2,s_handles[i],true);
    }
    ble_telemetry_service_publish(&s);ble_telemetry_service_notify(send_multi);
    assert(s_multi_sent[1]==4 && s_multi_sent[2]==4);
    ble_telemetry_service_disconnect(1);ble_telemetry_service_publish(&s);
    ble_telemetry_service_notify(send_multi);
    assert(s_multi_sent[1]==4 && s_multi_sent[2]==8);
    ble_telemetry_service_connect(1);ble_telemetry_service_publish(&s);
    ble_telemetry_service_notify(send_multi);
    assert(s_multi_sent[1]==4 && s_multi_sent[2]==12);
    ble_telemetry_service_stop();queries=s_queries;
    read_link(NULL);assert(s_queries==queries);
    ble_telemetry_service_deinit();assert(s_deinits==1 && !s_lock_depth);
    return 0;
}
