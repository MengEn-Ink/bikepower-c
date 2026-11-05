#include <assert.h>
#include <stdio.h>
#include "ble_service_stubs.h"
#include "../main/ble_config_service.c"

unsigned s_lock_depth;
static uint8_t s_response[BLE_CONFIG_CONTROL_RESPONSE_LEN];
static unsigned s_responses;
static config_data_t s_submitted;
static bool s_accept = true;

static void indicate(uint16_t handle, const uint8_t *data, size_t length)
{
    (void)handle;
    assert(s_lock_depth == 0);
    assert(length == sizeof(s_response));
    memcpy(s_response, data, length);
    s_responses++;
}

static bool commit(const config_data_t *config, uint32_t transaction, uint32_t revision)
{
    assert(s_lock_depth == 0);
    assert(transaction == 7);
    assert(revision == BLE_CONFIG_INITIAL_REVISION);
    s_submitted = *config;
    return s_accept;
}

static int control(const uint8_t *data, size_t length)
{
    struct os_mbuf om = { .length = length };
    memcpy(om.data, data, length);
    struct ble_gatt_access_ctxt ctxt = { .op = BLE_GATT_ACCESS_OP_WRITE_CHR, .om = &om };
    const struct ble_gatt_svc_def *svc = ble_config_service_definition();
    return svc->characteristics[6].access_cb(1, 1, &ctxt, NULL);
}

int main(void)
{
    power_engine_t engine;
    power_engine_init(&engine);
    ble_config_service_set_snapshot(&engine);
    ble_config_service_reset(indicate, NULL);
    ble_config_service_set_commit_callback(commit);
    const struct ble_gatt_svc_def *svc = ble_config_service_definition();
    assert(svc->characteristics[8].arg == (void *)(uintptr_t)BLE_CONFIG_GROUP_BRIDGE);
    assert(svc->characteristics[9].arg
           == (void *)(uintptr_t)BLE_CONFIG_GROUP_BRIDGE_BACKUP_1);
    assert(svc->characteristics[10].arg
           == (void *)(uintptr_t)BLE_CONFIG_GROUP_BRIDGE_BACKUP_2);
    assert(svc->characteristics[11].uuid == NULL);
    uint8_t begin[] = { BLE_CONFIG_OP_BEGIN, BLE_CONFIG_PROTOCOL_VERSION, 7, 0, 0, 0, 1, 0, 0, 0 };
    uint8_t finish[] = { BLE_CONFIG_OP_COMMIT, 7, 0, 0, 0, 1 };
    assert(control(begin, sizeof(begin)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_SUCCESS);
    assert(control(begin, sizeof(begin)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_BUSY);
    assert(control(finish, sizeof(finish)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_INCOMPLETE);
    uint8_t payload[BLE_CONFIG_GROUP_MAX_LEN];
    config_data_t config;
    config_store_defaults(&config);
    config.power = 321;
    struct os_mbuf om = { 0 };
    om.length = ble_config_encode_group(BLE_CONFIG_GROUP_CORE, &config, payload, sizeof(payload));
    memcpy(om.data, payload, om.length);
    struct ble_gatt_access_ctxt ctxt = { .op = BLE_GATT_ACCESS_OP_WRITE_CHR, .om = &om };
    assert(svc->characteristics[1].access_cb(1, 1, &ctxt, svc->characteristics[1].arg) == 0);
    s_accept = false;
    assert(control(finish, sizeof(finish)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_BUSY);
    assert(!s_transaction.active && !s_transaction.committing);
    assert(control(begin, sizeof(begin)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_SUCCESS);
    assert(svc->characteristics[1].access_cb(1, 1, &ctxt, svc->characteristics[1].arg) == 0);
    s_accept = true;
    unsigned before = s_responses;
    assert(control(finish, sizeof(finish)) == 0);
    assert(s_responses == before);
    assert(s_submitted.power == 321);
    ble_config_service_disconnect();
    ble_config_service_commit_result(7, BLE_CONFIG_RESULT_SUCCESS, 0, &s_submitted);
    assert(s_response[2] == BLE_CONFIG_RESULT_SUCCESS);
    assert(ble_config_service_revision_matches(2));
    assert(control(begin, sizeof(begin)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_STALE_REVISION);
    begin[6] = 2;
    assert(control(begin, sizeof(begin)) == 0);
    ble_config_service_disconnect();
    assert(control(finish, sizeof(finish)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_NO_TRANSACTION);
    uint8_t unknown[] = { 0xff };
    assert(control(unknown, sizeof(unknown)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_PROTOCOL_MISMATCH);
    assert(s_lock_depth == 0);
    puts("配置 GATT：提交确认、八组布局、过期 revision 与断连取消通过");
    return 0;
}
