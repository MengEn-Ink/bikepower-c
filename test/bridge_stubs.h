#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_STATE 3
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_HS_EDONE 1
#define BLE_HS_ENOENT 2
#define BLE_HS_ENOTCONN 3
#define BLE_ERR_REM_USER_CONN_TERM 19
#define BLE_GATT_CHR_PROP_NOTIFY 16
#define BLE_GAP_EVENT_CONNECT 1
#define BLE_GAP_EVENT_DISCONNECT 2
#define BLE_GAP_EVENT_NOTIFY_RX 3
#define BLE_GAP_EVENT_DISC 4
#define BLE_GAP_EVENT_DISC_COMPLETE 5
#define BLE_HS_EALREADY 4
#define BLE_GAP_SCAN_ITVL_MS(ms) (ms)
#define BLE_GAP_SCAN_WIN_MS(ms) (ms)

typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint16_t value; } ble_uuid16_t;
typedef union { ble_uuid_t u; ble_uuid16_t u16; } ble_uuid_any_t;
#define BLE_UUID16_INIT(v) { .u = { .type = 16 }, .value = (v) }
static inline uint16_t ble_uuid_u16(const ble_uuid_t *uuid)
{
    return ((const ble_uuid16_t *)uuid)->value;
}
typedef struct { uint8_t type; uint8_t val[6]; } ble_addr_t;
struct os_mbuf { uint8_t data[64]; int length; };
#define OS_MBUF_PKTLEN(om) ((om)->length)
static inline int os_mbuf_copydata(struct os_mbuf *om, int offset, int length, void *out)
{
    if (offset + length > om->length) return -1;
    memcpy(out, om->data + offset, length);
    return 0;
}
struct ble_gatt_error { int status; };
struct ble_gatt_svc { uint16_t start_handle; uint16_t end_handle; };
struct ble_gatt_chr { uint16_t def_handle; uint16_t val_handle; uint8_t properties; ble_uuid_any_t uuid; };
struct ble_gatt_dsc { uint16_t handle; ble_uuid_any_t uuid; };
struct ble_gatt_attr { struct os_mbuf *om; };
struct ble_gap_event {
    int type;
    struct { int status; uint16_t conn_handle; } connect;
    struct { int reason; struct { uint16_t conn_handle; } conn; } disconnect;
    struct { uint16_t conn_handle; uint16_t attr_handle; struct os_mbuf *om; } notify_rx;
    struct ble_gap_disc_desc {
        ble_addr_t addr;
        int8_t rssi;
        uint8_t *data;
        uint8_t length_data;
    } disc;
};
struct ble_gap_disc_params { uint16_t itvl; uint16_t window; uint8_t filter_duplicates; };
struct ble_hs_adv_fields {
    uint8_t *name;
    uint8_t name_len;
    uint8_t num_uuids16;
    ble_uuid16_t *uuids16;
};
typedef int (*gap_fn)(struct ble_gap_event *, void *);
typedef int (*service_fn)(uint16_t, const struct ble_gatt_error *, const struct ble_gatt_svc *, void *);
typedef int (*char_fn)(uint16_t, const struct ble_gatt_error *, const struct ble_gatt_chr *, void *);
typedef int (*desc_fn)(uint16_t, const struct ble_gatt_error *, uint16_t, const struct ble_gatt_dsc *, void *);
typedef int (*attr_fn)(uint16_t, const struct ble_gatt_error *, struct ble_gatt_attr *, void *);
int64_t esp_timer_get_time(void);
int ble_gap_conn_cancel(void);
int ble_gap_terminate(uint16_t handle, int reason);
int ble_hs_id_infer_auto(int privacy, uint8_t *type);
int ble_gap_connect(uint8_t type, const ble_addr_t *peer, int timeout, void *params, gap_fn callback, void *arg);
int ble_gattc_disc_svc_by_uuid(uint16_t handle, const ble_uuid_t *uuid, service_fn callback, void *arg);
int ble_gattc_disc_all_chrs(uint16_t handle, uint16_t first, uint16_t last, char_fn callback, void *arg);
int ble_gattc_disc_all_dscs(uint16_t handle, uint16_t first, uint16_t last, desc_fn callback, void *arg);
int ble_gattc_write_flat(uint16_t handle, uint16_t attr, const void *data, int length, attr_fn callback, void *arg);
int ble_gattc_read_by_uuid(uint16_t handle, uint16_t first, uint16_t last, const ble_uuid_t *uuid, attr_fn callback, void *arg);
int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *fields, const uint8_t *data, uint8_t length);
int ble_gap_disc(uint8_t type, int32_t duration, const struct ble_gap_disc_params *params, gap_fn callback, void *arg);
int ble_gap_disc_cancel(void);
