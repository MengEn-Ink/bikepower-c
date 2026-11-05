#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_hs.h"

#include "bikepower_config.h"
#include "ble_config_codec.h"
#include "ble_config_service.h"

static const char *TAG = "BLE_CONFIG";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static config_data_t s_snapshot;
static bool s_snapshot_valid;
static uint32_t s_revision = BLE_CONFIG_INITIAL_REVISION;
static uint16_t s_control_handle;
static uint8_t s_response[BLE_CONFIG_CONTROL_RESPONSE_LEN];
static ble_config_commit_cb_t s_commit_cb;
static ble_config_indicate_cb_t s_indicate_cb;
static ble_gatt_access_fn *s_identity_access;
static struct {
    bool active;
    bool committing;
    uint32_t id;
    uint32_t revision;
    uint8_t written;
    config_data_t staging;
} s_transaction;

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(BLE_CONFIG_SERVICE_UUID_BYTES);
static const ble_uuid128_t s_meta_uuid = BLE_UUID128_INIT(BLE_CONFIG_META_UUID_BYTES);
static const ble_uuid128_t s_core_uuid = BLE_UUID128_INIT(BLE_CONFIG_CORE_UUID_BYTES);
static const ble_uuid128_t s_profile_uuid = BLE_UUID128_INIT(BLE_CONFIG_PROFILE_UUID_BYTES);
static const ble_uuid128_t s_running_uuid = BLE_UUID128_INIT(BLE_CONFIG_RUNNING_UUID_BYTES);
static const ble_uuid128_t s_led_uuid = BLE_UUID128_INIT(BLE_CONFIG_LED_UUID_BYTES);
static const ble_uuid128_t s_identity_uuid = BLE_UUID128_INIT(BLE_CONFIG_IDENTITY_UUID_BYTES);
static const ble_uuid128_t s_bridge_uuid = BLE_UUID128_INIT(BLE_CONFIG_BRIDGE_UUID_BYTES);
static const ble_uuid128_t s_bridge_backup_1_uuid =
    BLE_UUID128_INIT(BLE_CONFIG_BRIDGE_BACKUP_1_UUID_BYTES);
static const ble_uuid128_t s_bridge_backup_2_uuid =
    BLE_UUID128_INIT(BLE_CONFIG_BRIDGE_BACKUP_2_UUID_BYTES);
static const ble_uuid128_t s_control_uuid = BLE_UUID128_INIT(BLE_CONFIG_CONTROL_UUID_BYTES);
static const ble_uuid128_t s_dis_profile_uuid = BLE_UUID128_INIT(BLE_CONFIG_DIS_PROFILE_UUID_BYTES);

static int read_buf(struct ble_gatt_access_ctxt *ctxt, const uint8_t *data, size_t length)
{
    return os_mbuf_append(ctxt->om, data, length) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static uint32_t get_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool snapshot_equal(const config_data_t *lhs, const config_data_t *rhs)
{
    uint8_t a[BLE_CONFIG_GROUP_MAX_LEN], b[BLE_CONFIG_GROUP_MAX_LEN];
    for (int group = 0; group < BLE_CONFIG_GROUP_COUNT; group++) {
        size_t length = ble_config_encode_group((ble_config_group_t)group, lhs, a, sizeof(a));
        size_t other = ble_config_encode_group((ble_config_group_t)group, rhs, b, sizeof(b));
        if (!length || length != other || memcmp(a, b, length)) return false;
    }
    return true;
}

static void send_response(uint8_t opcode, uint8_t result, uint8_t restart,
                           uint32_t transaction, uint32_t revision)
{
    uint8_t response[BLE_CONFIG_CONTROL_RESPONSE_LEN];
    ble_config_encode_control_response(response, sizeof(response), opcode, result,
                                       restart, transaction, revision);
    portENTER_CRITICAL(&s_lock);
    memcpy(s_response, response, sizeof(response));
    portEXIT_CRITICAL(&s_lock);
    if (s_indicate_cb) s_indicate_cb(s_control_handle, response, sizeof(response));
}

static int meta_access(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    portENTER_CRITICAL(&s_lock);
    uint32_t revision = s_revision;
    portEXIT_CRITICAL(&s_lock);
    uint8_t payload[BLE_CONFIG_META_LEN];
    size_t length = ble_config_encode_meta(payload, sizeof(payload), revision);
    return read_buf(ctxt, payload, length);
}

static int group_access(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah;
    ble_config_group_t group = (ble_config_group_t)(uintptr_t)arg;
    if (group < 0 || group >= BLE_CONFIG_GROUP_COUNT) return BLE_ATT_ERR_UNLIKELY;
    uint8_t payload[BLE_CONFIG_GROUP_MAX_LEN];
    config_data_t config;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        portENTER_CRITICAL(&s_lock);
        config = s_snapshot;
        portEXIT_CRITICAL(&s_lock);
        size_t length = ble_config_encode_group(group, &config, payload, sizeof(payload));
        return length ? read_buf(ctxt, payload, length) : BLE_ATT_ERR_UNLIKELY;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint16_t length = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, payload, sizeof(payload), &length) || !length) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    portENTER_CRITICAL(&s_lock);
    bool writable = s_transaction.active && !s_transaction.committing;
    config = s_transaction.staging;
    uint32_t transaction = s_transaction.id;
    portEXIT_CRITICAL(&s_lock);
    if (!writable) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    if (!ble_config_decode_group(group, payload, length, &config)) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    portENTER_CRITICAL(&s_lock);
    if (!s_transaction.active || s_transaction.committing || s_transaction.id != transaction) {
        portEXIT_CRITICAL(&s_lock);
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    s_transaction.staging = config;
    s_transaction.written |= (uint8_t)(1u << group);
    portEXIT_CRITICAL(&s_lock);
    return 0;
}

static int control_access(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t response[BLE_CONFIG_CONTROL_RESPONSE_LEN];
        portENTER_CRITICAL(&s_lock);
        memcpy(response, s_response, sizeof(response));
        portEXIT_CRITICAL(&s_lock);
        return read_buf(ctxt, response, sizeof(response));
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    uint8_t payload[BLE_CONFIG_CONTROL_BUF_SIZE];
    uint16_t length = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, payload, sizeof(payload), &length) || !length) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    uint8_t opcode = payload[0];
    uint8_t result = BLE_CONFIG_RESULT_SUCCESS;
    uint32_t transaction = 0;
    portENTER_CRITICAL(&s_lock);
    uint32_t revision = s_revision;
    portEXIT_CRITICAL(&s_lock);
    if (opcode == BLE_CONFIG_OP_BEGIN) {
        if (length != 10) result = BLE_CONFIG_RESULT_INVALID_LENGTH;
        else if (payload[1] != BLE_CONFIG_PROTOCOL_VERSION) result = BLE_CONFIG_RESULT_PROTOCOL_MISMATCH;
        else {
            transaction = get_u32(payload + 2);
            uint32_t expected = get_u32(payload + 6);
            portENTER_CRITICAL(&s_lock);
            revision = s_revision;
            if (s_transaction.active || s_transaction.committing) result = BLE_CONFIG_RESULT_BUSY;
            else if (!transaction || expected != revision) result = BLE_CONFIG_RESULT_STALE_REVISION;
            else {
                s_transaction.active = true;
                s_transaction.id = transaction;
                s_transaction.revision = revision;
                s_transaction.written = 0;
                s_transaction.staging = s_snapshot;
            }
            portEXIT_CRITICAL(&s_lock);
        }
    } else if (opcode == BLE_CONFIG_OP_COMMIT) {
        if (length != 6) result = BLE_CONFIG_RESULT_INVALID_LENGTH;
        else {
            transaction = get_u32(payload + 1);
            uint8_t required = payload[5];
            config_data_t staged;
            uint32_t base_revision = 0;
            bool submit = false;
            portENTER_CRITICAL(&s_lock);
            revision = s_revision;
            if (!s_transaction.active) result = BLE_CONFIG_RESULT_NO_TRANSACTION;
            else if (s_transaction.id != transaction) result = BLE_CONFIG_RESULT_TRANSACTION_MISMATCH;
            else if (s_transaction.revision != revision) {
                result = BLE_CONFIG_RESULT_STALE_REVISION;
                s_transaction.active = false;
            } else if (!required || (required & ~BLE_CONFIG_GROUP_MASK_ALL)
                       || (s_transaction.written & required) != required) {
                result = BLE_CONFIG_RESULT_INCOMPLETE;
            } else {
                staged = s_transaction.staging;
                base_revision = s_transaction.revision;
                s_transaction.active = false;
                s_transaction.committing = true;
                submit = true;
            }
            portEXIT_CRITICAL(&s_lock);
            if (submit) {
                if (s_commit_cb && s_commit_cb(&staged, transaction, base_revision)) return 0;
                portENTER_CRITICAL(&s_lock);
                s_transaction.committing = false;
                portEXIT_CRITICAL(&s_lock);
                result = BLE_CONFIG_RESULT_BUSY;
            }
        }
    } else if (opcode == BLE_CONFIG_OP_CANCEL) {
        if (length != 5) result = BLE_CONFIG_RESULT_INVALID_LENGTH;
        else {
            transaction = get_u32(payload + 1);
            portENTER_CRITICAL(&s_lock);
            revision = s_revision;
            if (!s_transaction.active) result = s_transaction.committing
                ? BLE_CONFIG_RESULT_BUSY : BLE_CONFIG_RESULT_NO_TRANSACTION;
            else if (s_transaction.id != transaction) result = BLE_CONFIG_RESULT_TRANSACTION_MISMATCH;
            else {
                s_transaction.active = false;
                s_transaction.written = 0;
            }
            portEXIT_CRITICAL(&s_lock);
        }
    } else result = BLE_CONFIG_RESULT_PROTOCOL_MISMATCH;
    send_response(opcode, result, 0, transaction, revision);
    return 0;
}

static int identity_access(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return s_identity_access ? s_identity_access(ch, ah, ctxt, arg) : BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_service = {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &s_service_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]){
        { .uuid = &s_meta_uuid.u, .access_cb = meta_access, .flags = BLE_GATT_CHR_F_READ },
        { .uuid = &s_core_uuid.u, .access_cb = group_access,
          .arg = (void *)(uintptr_t)BLE_CONFIG_GROUP_CORE, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
        { .uuid = &s_profile_uuid.u, .access_cb = group_access,
          .arg = (void *)(uintptr_t)BLE_CONFIG_GROUP_PROFILE, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
        { .uuid = &s_running_uuid.u, .access_cb = group_access,
          .arg = (void *)(uintptr_t)BLE_CONFIG_GROUP_RUNNING, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
        { .uuid = &s_led_uuid.u, .access_cb = group_access,
          .arg = (void *)(uintptr_t)BLE_CONFIG_GROUP_LED, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
        { .uuid = &s_identity_uuid.u, .access_cb = group_access,
          .arg = (void *)(uintptr_t)BLE_CONFIG_GROUP_IDENTITY, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
        { .uuid = &s_control_uuid.u, .access_cb = control_access,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE,
          .val_handle = &s_control_handle },
        { .uuid = &s_dis_profile_uuid.u, .access_cb = identity_access, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
        { .uuid = &s_bridge_uuid.u, .access_cb = group_access,
          .arg = (void *)(uintptr_t)BLE_CONFIG_GROUP_BRIDGE, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
        { .uuid = &s_bridge_backup_1_uuid.u, .access_cb = group_access,
          .arg = (void *)(uintptr_t)BLE_CONFIG_GROUP_BRIDGE_BACKUP_1,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
        { .uuid = &s_bridge_backup_2_uuid.u, .access_cb = group_access,
          .arg = (void *)(uintptr_t)BLE_CONFIG_GROUP_BRIDGE_BACKUP_2,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE },
        { 0 }
    }
};

void ble_config_service_reset(ble_config_indicate_cb_t indicate, ble_gatt_access_fn *identity)
{
    s_indicate_cb = indicate;
    s_identity_access = identity;
    s_control_handle = 0;
    portENTER_CRITICAL(&s_lock);
    s_transaction.active = false;
    s_transaction.committing = false;
    s_transaction.written = 0;
    uint32_t revision = s_revision;
    portEXIT_CRITICAL(&s_lock);
    uint8_t response[BLE_CONFIG_CONTROL_RESPONSE_LEN];
    ble_config_encode_control_response(response, sizeof(response), BLE_CONFIG_OP_CANCEL,
                                       BLE_CONFIG_RESULT_SUCCESS, 0, 0, revision);
    portENTER_CRITICAL(&s_lock);
    memcpy(s_response, response, sizeof(response));
    portEXIT_CRITICAL(&s_lock);
}

const struct ble_gatt_svc_def *ble_config_service_definition(void) { return &s_service; }

void ble_config_service_disconnect(void)
{
    portENTER_CRITICAL(&s_lock);
    s_transaction.active = false;
    s_transaction.written = 0;
    portEXIT_CRITICAL(&s_lock);
}

void ble_config_service_set_commit_callback(ble_config_commit_cb_t cb) { s_commit_cb = cb; }

void ble_config_service_set_snapshot(const power_engine_t *engine)
{
    if (!engine) return;
    config_data_t next;
    config_store_from_engine(engine, &next);
    portENTER_CRITICAL(&s_lock);
    config_data_t previous = s_snapshot;
    bool was_valid = s_snapshot_valid;
    portEXIT_CRITICAL(&s_lock);
    bool changed = was_valid && !snapshot_equal(&previous, &next);
    portENTER_CRITICAL(&s_lock);
    s_snapshot = next;
    if (!was_valid) {
        s_snapshot_valid = true;
        s_revision = BLE_CONFIG_INITIAL_REVISION;
    } else if (changed && ++s_revision == 0) s_revision = BLE_CONFIG_INITIAL_REVISION;
    portEXIT_CRITICAL(&s_lock);
}

bool ble_config_service_revision_matches(uint32_t revision)
{
    portENTER_CRITICAL(&s_lock);
    bool matches = s_snapshot_valid && s_revision == revision;
    portEXIT_CRITICAL(&s_lock);
    return matches;
}

void ble_config_service_commit_result(uint32_t transaction, uint8_t result,
                                      uint8_t restart, const config_data_t *applied)
{
    portENTER_CRITICAL(&s_lock);
    if (!s_transaction.committing || s_transaction.id != transaction) {
        portEXIT_CRITICAL(&s_lock);
        ESP_LOGW(TAG, "忽略未知配置事务结果 id=%lu", (unsigned long)transaction);
        return;
    }
    if (result == BLE_CONFIG_RESULT_SUCCESS && applied) {
        s_snapshot = *applied;
        s_snapshot_valid = true;
        if (++s_revision == 0) s_revision = BLE_CONFIG_INITIAL_REVISION;
    } else if (result == BLE_CONFIG_RESULT_SUCCESS) {
        result = BLE_CONFIG_RESULT_SAVE_FAILED;
        restart = 0;
    }
    s_transaction.committing = false;
    s_transaction.written = 0;
    uint32_t revision = s_revision;
    portEXIT_CRITICAL(&s_lock);
    send_response(BLE_CONFIG_OP_COMMIT, result, restart, transaction, revision);
}
