#include <assert.h>
#include <stdio.h>
#include "ble_service_stubs.h"
#include "../main/ble_config_service.c"

unsigned s_lock_depth;
static uint8_t s_response[BLE_CONFIG_CONTROL_RESPONSE_LEN];
static unsigned response_count;
static config_data_t s_submitted;
static bool s_accept = true;

uint16_t ble_bikepower_response_connection(uint32_t token){assert(token==1);return 1;}
static bool s_capacity = true;
static uint32_t reserve(uint16_t connection, uint16_t handle)
{
    (void)handle;
    assert((connection == 1 || connection == 2) && s_lock_depth == 0);
    return s_capacity ? 1 : 0;
}

static void indicate(uint32_t token, const uint8_t *data, size_t length)
{
    assert(token == 1);
    assert(s_lock_depth == 0);
    assert(length == sizeof(s_response));
    memcpy(s_response, data, length);
    response_count++;
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
    ble_config_service_reset(reserve, indicate, NULL);
    ble_config_service_connect(1);
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
    s_capacity = false;
    assert(control(begin, sizeof(begin)) == BLE_ATT_ERR_INSUFFICIENT_RES);
    s_capacity = true;
    assert(control(begin, sizeof(begin)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_SUCCESS);
    assert(control(begin, sizeof(begin)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_BUSY);
    assert(control(finish, sizeof(finish)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_INCOMPLETE);
    ble_config_service_connect(2);
    ble_config_service_disconnect(2);
    assert(s_transaction.active && s_transaction.owner == 1);
    struct os_mbuf other = {.length = sizeof(finish)};
    memcpy(other.data, finish, sizeof(finish));
    struct ble_gatt_access_ctxt other_ctxt = {.op = BLE_GATT_ACCESS_OP_WRITE_CHR, .om = &other};
    s_capacity = false;
    assert(control_access(2,1,&other_ctxt,NULL) == BLE_ATT_ERR_INSUFFICIENT_RES);
    s_capacity = true;
    uint8_t payload[BLE_CONFIG_GROUP_MAX_LEN];
    config_data_t config;
    config_store_defaults(&config);
    config.power = 321;
    struct os_mbuf om = { 0 };
    om.length = ble_config_encode_group(BLE_CONFIG_GROUP_CORE, &config, payload, sizeof(payload));
    memcpy(om.data, payload, om.length);
    struct ble_gatt_access_ctxt ctxt = { .op = BLE_GATT_ACCESS_OP_WRITE_CHR, .om = &om };
    assert(svc->characteristics[1].access_cb(2, 1, &ctxt, svc->characteristics[1].arg) == BLE_ATT_ERR_WRITE_NOT_PERMITTED);
    assert(svc->characteristics[1].access_cb(1, 1, &ctxt, svc->characteristics[1].arg) == 0);
    s_accept = false;
    assert(control(finish, sizeof(finish)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_BUSY);
    assert(!s_transaction.active && !s_transaction.committing);
    assert(control(begin, sizeof(begin)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_SUCCESS);
    assert(svc->characteristics[1].access_cb(2, 1, &ctxt, svc->characteristics[1].arg) == BLE_ATT_ERR_WRITE_NOT_PERMITTED);
    assert(svc->characteristics[1].access_cb(1, 1, &ctxt, svc->characteristics[1].arg) == 0);
    s_accept = true;
    unsigned before = response_count;
    assert(control(finish, sizeof(finish)) == 0);
    assert(response_count == before);
    assert(s_submitted.power == 321);
    ble_config_service_disconnect(1);
    ble_config_service_connect(1);
    ble_config_service_commit_result(7, BLE_CONFIG_RESULT_SUCCESS, 0, &s_submitted);
    assert(s_response[2] == BLE_CONFIG_RESULT_SUCCESS);
    assert(ble_config_service_revision_matches(2));
    assert(control(begin, sizeof(begin)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_STALE_REVISION);
    begin[6] = 2;
    assert(control(begin, sizeof(begin)) == 0);
    ble_config_service_disconnect(1);
    ble_config_service_connect(1);
    assert(control(finish, sizeof(finish)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_NO_TRANSACTION);
    uint8_t unknown[] = { 0xff };
    assert(control(unknown, sizeof(unknown)) == 0);
    assert(s_response[2] == BLE_CONFIG_RESULT_PROTOCOL_MISMATCH);
    assert(s_lock_depth == 0);
    puts("配置 GATT：提交确认、八组布局、过期 revision 与断连取消通过");
    return 0;
}
