#include <string.h>
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "host/ble_hs.h"
#include "store/config/ble_store_config.h"
#include "services/gatt/ble_svc_gatt.h"
#include "bikepower_config.h"
#include "ble_bond_cache.h"

static mbedtls_sha256_context s_layout;
static bool s_layout_started;
static int s_layout_error;
static uint8_t s_layout_digest[BLE_BOND_CACHE_DIGEST_SIZE];
static bool s_layout_valid;

static void layout_bytes(const void *data, size_t size)
{
    if (!s_layout_error) s_layout_error = mbedtls_sha256_update(&s_layout, data, size);
}

static void layout_u16(uint16_t value)
{
    const uint8_t wire[] = { value & 0xff, value >> 8 };
    layout_bytes(wire, sizeof(wire));
}

static void layout_uuid(const ble_uuid_t *uuid)
{
    layout_bytes(&uuid->type, sizeof(uuid->type));
    switch (uuid->type) {
    case BLE_UUID_TYPE_16:
        layout_u16(((const ble_uuid16_t *)uuid)->value);
        break;
    case BLE_UUID_TYPE_32: {
        uint32_t value = ((const ble_uuid32_t *)uuid)->value;
        layout_u16(value & 0xffff);
        layout_u16(value >> 16);
        break;
    }
    case BLE_UUID_TYPE_128:
        layout_bytes(((const ble_uuid128_t *)uuid)->value,
                     sizeof(((const ble_uuid128_t *)uuid)->value));
        break;
    default:
        s_layout_error = ESP_ERR_INVALID_ARG;
        break;
    }
}

void ble_bond_cache_begin(void)
{
    if (s_layout_started) mbedtls_sha256_free(&s_layout);
    mbedtls_sha256_init(&s_layout);
    s_layout_started = true;
    s_layout_valid = false;
    s_layout_error = mbedtls_sha256_starts(&s_layout, 0);
    const uint8_t version = BLE_BOND_CACHE_FORMAT_VERSION;
    layout_bytes(&version, sizeof(version));
}

void ble_bond_cache_register(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    (void)arg;
    if (!s_layout_started || !ctxt) {
        s_layout_error = ESP_ERR_INVALID_STATE;
        return;
    }
    layout_bytes(&ctxt->op, sizeof(ctxt->op));
    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        layout_u16(ctxt->svc.handle);
        layout_bytes(&ctxt->svc.svc_def->type, sizeof(ctxt->svc.svc_def->type));
        layout_uuid(ctxt->svc.svc_def->uuid);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        layout_u16(ctxt->chr.def_handle);
        layout_u16(ctxt->chr.val_handle);
        layout_uuid(ctxt->chr.svc_def->uuid);
        layout_uuid(ctxt->chr.chr_def->uuid);
        layout_u16(ctxt->chr.chr_def->flags);
        break;
    case BLE_GATT_REGISTER_OP_DSC:
        layout_u16(ctxt->dsc.handle);
        layout_uuid(ctxt->dsc.svc_def->uuid);
        layout_uuid(ctxt->dsc.chr_def->uuid);
        layout_uuid(ctxt->dsc.dsc_def->uuid);
        layout_bytes(&ctxt->dsc.dsc_def->att_flags, sizeof(ctxt->dsc.dsc_def->att_flags));
        break;
    default:
        s_layout_error = ESP_ERR_INVALID_ARG;
        break;
    }
}

static esp_err_t invalidate_subscriptions(uint16_t changed_handle)
{
    unsigned index = 0;
    for (;;) {
        union ble_store_key key = {0};
        union ble_store_value value = {0};
        key.cccd.peer_addr = *BLE_ADDR_ANY;
        key.cccd.idx = index;
        int rc = ble_store_config_read(BLE_STORE_OBJ_TYPE_CCCD, &key, &value);
        if (rc == BLE_HS_ENOENT) return ESP_OK;
        if (rc != 0) return ESP_FAIL;
        if (value.cccd.chr_val_handle == changed_handle) {
            value.cccd.value_changed = 1;
            if (ble_store_write_cccd(&value.cccd) != 0) return ESP_FAIL;
            ++index;
            if (index > CONFIG_BT_NIMBLE_MAX_CCCDS) return ESP_ERR_INVALID_STATE;
            continue;
        }
        ble_store_key_from_value(BLE_STORE_OBJ_TYPE_CCCD, &key, &value);
        rc = ble_store_config_delete(BLE_STORE_OBJ_TYPE_CCCD, &key);
        if (rc != 0) return ESP_FAIL;
        if (ble_store_config_read(BLE_STORE_OBJ_TYPE_CCCD, &key, &value) != BLE_HS_ENOENT) {
            return ESP_ERR_INVALID_STATE;
        }
    }
}

static esp_err_t verify_subscriptions(uint16_t changed_handle)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(BLE_BOND_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    nvs_iterator_t it = NULL;
    err = nvs_entry_find_in_handle(handle, NVS_TYPE_ANY, &it);
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        err = nvs_entry_info(it, &info);
        if (err != ESP_OK) break;
        if (strncmp(info.key, BLE_BOND_NVS_CCCD_PREFIX, strlen(BLE_BOND_NVS_CCCD_PREFIX)) == 0) {
            struct ble_store_value_cccd value;
            size_t size = sizeof(value);
            err = nvs_get_blob(handle, info.key, &value, &size);
            if (err != ESP_OK) break;
            if (size != sizeof(value) || value.chr_val_handle != changed_handle
                    || !value.value_changed) {
                err = ESP_ERR_INVALID_STATE;
                break;
            }
        }
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    nvs_close(handle);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t ble_bond_cache_sync(bool *changed)
{
    if (!changed || (!s_layout_started && !s_layout_valid)) return ESP_ERR_INVALID_STATE;
    *changed = false;
    if (s_layout_started) {
        int rc = s_layout_error;
        if (!rc) rc = mbedtls_sha256_finish(&s_layout, s_layout_digest);
        mbedtls_sha256_free(&s_layout);
        s_layout_started = false;
        if (rc != 0) return ESP_FAIL;
        s_layout_valid = true;
    }
    const uint8_t *digest = s_layout_digest;
    int rc;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(BLE_BOND_CACHE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    uint8_t prior[BLE_BOND_CACHE_DIGEST_SIZE];
    size_t size = sizeof(prior);
    err = nvs_get_blob(handle, BLE_BOND_CACHE_LAYOUT_KEY, prior, &size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto done;
    if (err == ESP_OK && size != sizeof(prior)) {
        err = ESP_ERR_INVALID_SIZE;
        goto done;
    }
    *changed = err == ESP_ERR_NVS_NOT_FOUND || memcmp(prior, digest, sizeof(prior)) != 0;
    if (!*changed) {
        err = ESP_OK;
        goto done;
    }
    uint16_t changed_handle;
    rc = ble_gatts_find_chr(BLE_UUID16_DECLARE(BLE_GATT_SVC_UUID16),
                           BLE_UUID16_DECLARE(BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16),
                           NULL, &changed_handle);
    if (rc != 0) {
        err = ESP_ERR_INVALID_STATE;
        goto done;
    }
    /* 先清旧订阅再提交新布局；中断后仍检测为变化，不会恢复错位句柄。 */
    err = invalidate_subscriptions(changed_handle);
    if (err == ESP_OK) err = verify_subscriptions(changed_handle);
    if (err != ESP_OK) goto done;
    err = nvs_set_blob(handle, BLE_BOND_CACHE_LAYOUT_KEY, digest, BLE_BOND_CACHE_DIGEST_SIZE);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err == ESP_OK) {
        size = sizeof(prior);
        err = nvs_get_blob(handle, BLE_BOND_CACHE_LAYOUT_KEY, prior, &size);
        if (err == ESP_OK && (size != sizeof(prior) || memcmp(prior, digest, size) != 0)) {
            err = ESP_ERR_INVALID_STATE;
        }
    }
done:
    nvs_close(handle);
    return err;
}
