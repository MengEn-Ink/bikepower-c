#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int esp_err_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
extern unsigned s_lock_depth;
#define portENTER_CRITICAL(lock) ((void)(lock), ++s_lock_depth)
#define portEXIT_CRITICAL(lock) ((void)(lock), --s_lock_depth)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define BLE_ATT_ERR_INSUFFICIENT_RES 1
#define BLE_ATT_ERR_UNLIKELY 2
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 3
#define BLE_ATT_ERR_WRITE_NOT_PERMITTED 4
#define BLE_ATT_ERR_VALUE_NOT_ALLOWED 5
#define BLE_GATT_ACCESS_OP_READ_CHR 1
#define BLE_GATT_ACCESS_OP_WRITE_CHR 2
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GATT_CHR_F_READ 1
#define BLE_GATT_CHR_F_WRITE 2
#define BLE_GATT_CHR_F_INDICATE 4
typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint8_t value[16]; } ble_uuid128_t;
#define BLE_UUID128_INIT(...) { .u = { .type = 128 }, .value = { __VA_ARGS__ } }
struct os_mbuf { uint8_t data[256]; uint16_t length; };
struct ble_gatt_access_ctxt { uint8_t op; struct os_mbuf *om; };
typedef int ble_gatt_access_fn(uint16_t, uint16_t, struct ble_gatt_access_ctxt *, void *);
struct ble_gatt_chr_def {
    const ble_uuid_t *uuid;
    ble_gatt_access_fn *access_cb;
    void *arg;
    uint16_t flags;
    uint16_t *val_handle;
};
struct ble_gatt_svc_def {
    uint8_t type;
    const ble_uuid_t *uuid;
    struct ble_gatt_chr_def *characteristics;
};
static inline int os_mbuf_append(struct os_mbuf *om, const void *data, size_t length)
{
    if (om->length + length > sizeof(om->data)) return -1;
    memcpy(om->data + om->length, data, length);
    om->length += length;
    return 0;
}
static inline int ble_hs_mbuf_to_flat(struct os_mbuf *om, void *data, size_t capacity, uint16_t *length)
{
    if (om->length > capacity) return -1;
    *length = om->length;
    memcpy(data, om->data, om->length);
    return 0;
}
