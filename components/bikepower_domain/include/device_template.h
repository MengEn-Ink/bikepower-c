/**
 * BLE 设备信息模板纯逻辑。
 * 用于保存从附近蓝牙设备读取到的名称、厂商、型号、版本等身份信息。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_TEMPLATE_MAX          5
#define DEVICE_TEMPLATE_ALIAS_MAX    24
#define DEVICE_TEMPLATE_NAME_MAX     32
#define DEVICE_TEMPLATE_TEXT_MAX     32
#define DEVICE_TEMPLATE_VERSION_MAX  24
#define DEVICE_TEMPLATE_ADDR_MAX     18

#define DEVICE_TEMPLATE_SVC_DIS      0x0001
#define DEVICE_TEMPLATE_SVC_BAS      0x0002
#define DEVICE_TEMPLATE_SVC_CPS      0x0004
#define DEVICE_TEMPLATE_SVC_HRS      0x0008
#define DEVICE_TEMPLATE_SVC_CSC      0x0010
#define DEVICE_TEMPLATE_SVC_FTMS     0x0020

typedef struct {
    bool used;
    uint8_t id;
    char alias[DEVICE_TEMPLATE_ALIAS_MAX];
    char adv_name[DEVICE_TEMPLATE_NAME_MAX];
    char device_name[DEVICE_TEMPLATE_NAME_MAX];
    char manufacturer[DEVICE_TEMPLATE_TEXT_MAX];
    char model[DEVICE_TEMPLATE_TEXT_MAX];
    char firmware[DEVICE_TEMPLATE_VERSION_MAX];
    char hardware[DEVICE_TEMPLATE_VERSION_MAX];
    char software[DEVICE_TEMPLATE_VERSION_MAX];
    char addr[DEVICE_TEMPLATE_ADDR_MAX];
    uint8_t addr_type;
    int8_t last_rssi;
    uint16_t service_flags;
} device_template_t;

typedef struct {
    device_template_t items[DEVICE_TEMPLATE_MAX];
    uint8_t count;
} device_template_list_t;

void device_template_defaults(device_template_t *tpl);
void device_template_list_defaults(device_template_list_t *list);
bool device_template_validate(device_template_t *tpl);
bool device_template_set_addr(device_template_t *tpl, const uint8_t addr[6], uint8_t addr_type);
bool device_template_note_service(device_template_t *tpl, uint16_t uuid16);
bool device_template_has_ride_service(uint16_t service_flags);
void device_template_list_recount(device_template_list_t *list);
int device_template_list_add(device_template_list_t *list, const device_template_t *tpl);
bool device_template_list_delete(device_template_list_t *list, uint8_t id);
size_t device_template_serialize_json(const device_template_t *tpl, char *out, size_t out_sz);
size_t device_template_list_serialize_json(const device_template_list_t *list, char *out, size_t out_sz);
