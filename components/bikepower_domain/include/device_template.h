/**
 * BLE 设备信息结构。
 * 扫描、上游 DIS 读取与设备身份复制共用；模板库由 Pages 管理。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DEVICE_TEMPLATE_ALIAS_MAX    24
#define DEVICE_TEMPLATE_NAME_MAX     32
#define DEVICE_TEMPLATE_TEXT_MAX     32
#define DEVICE_TEMPLATE_VERSION_MAX  24
#define DEVICE_TEMPLATE_ADDR_MAX     18
#define DEVICE_TEMPLATE_RAW_HEX_MAX  65

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
    char adv_raw_hex[DEVICE_TEMPLATE_RAW_HEX_MAX];
    char scan_rsp_raw_hex[DEVICE_TEMPLATE_RAW_HEX_MAX];
    char manufacturer_data_hex[DEVICE_TEMPLATE_RAW_HEX_MAX];
    char service_data_hex[DEVICE_TEMPLATE_RAW_HEX_MAX];
    uint8_t addr_type;
    int8_t last_rssi;
    int8_t tx_power;
    uint16_t appearance;
    uint16_t service_flags;
} device_template_t;
