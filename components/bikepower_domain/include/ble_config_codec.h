/**
 * BikePower 自定义 BLE 配置服务二进制编解码（纯 C，可 host 单测）。
 * 所有多字节整数都使用小端；不直接传输 C struct，避免 padding 与 ABI 差异。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config_store.h"

#define BLE_CONFIG_PROTOCOL_VERSION 1

typedef enum {
    BLE_CONFIG_GROUP_CORE = 0,
    BLE_CONFIG_GROUP_PROFILE,
    BLE_CONFIG_GROUP_RUNNING,
    BLE_CONFIG_GROUP_LED,
    BLE_CONFIG_GROUP_IDENTITY,
    BLE_CONFIG_GROUP_COUNT,
} ble_config_group_t;

#define BLE_CONFIG_GROUP_MASK_ALL ((1u << BLE_CONFIG_GROUP_COUNT) - 1u)
#define BLE_CONFIG_META_LEN 12
#define BLE_CONFIG_CORE_LEN 15
#define BLE_CONFIG_LED_LEN 12
#define BLE_CONFIG_GROUP_MAX_LEN 48
#define BLE_CONFIG_CONTROL_RESPONSE_LEN 12

#define BLE_CONFIG_CAP_READ_ALL      (1u << 0)
#define BLE_CONFIG_CAP_WRITE_ALL     (1u << 1)
#define BLE_CONFIG_CAP_TRANSACTIONAL (1u << 2)
#define BLE_CONFIG_CAP_RESTART_HINT  (1u << 3)
#define BLE_CONFIG_CAPABILITIES (BLE_CONFIG_CAP_READ_ALL | BLE_CONFIG_CAP_WRITE_ALL \
                                 | BLE_CONFIG_CAP_TRANSACTIONAL | BLE_CONFIG_CAP_RESTART_HINT)

typedef enum {
    BLE_CONFIG_OP_BEGIN = 0x01,
    BLE_CONFIG_OP_COMMIT = 0x02,
    BLE_CONFIG_OP_CANCEL = 0x03,
    BLE_CONFIG_OP_RESPONSE = 0x80,
} ble_config_opcode_t;

typedef enum {
    BLE_CONFIG_RESULT_SUCCESS = 0x00,
    BLE_CONFIG_RESULT_INVALID_LENGTH = 0x01,
    BLE_CONFIG_RESULT_PROTOCOL_MISMATCH = 0x02,
    BLE_CONFIG_RESULT_BUSY = 0x03,
    BLE_CONFIG_RESULT_STALE_REVISION = 0x04,
    BLE_CONFIG_RESULT_NO_TRANSACTION = 0x05,
    BLE_CONFIG_RESULT_TRANSACTION_MISMATCH = 0x06,
    BLE_CONFIG_RESULT_INVALID_VALUE = 0x07,
    BLE_CONFIG_RESULT_INCOMPLETE = 0x08,
    BLE_CONFIG_RESULT_SAVE_FAILED = 0x09,
} ble_config_result_t;

#define BLE_CONFIG_RESTART_GATT (1u << 0)
#define BLE_CONFIG_RESTART_NAME (1u << 1)

size_t ble_config_encode_meta(uint8_t *out, size_t out_size, uint32_t revision);
size_t ble_config_encode_group(ble_config_group_t group, const config_data_t *config,
                               uint8_t *out, size_t out_size);
bool ble_config_decode_group(ble_config_group_t group, const uint8_t *data, size_t len,
                             config_data_t *config);
size_t ble_config_encode_control_response(uint8_t *out, size_t out_size,
                                          uint8_t request_opcode, uint8_t result,
                                          uint8_t restart_flags, uint32_t transaction_id,
                                          uint32_t revision);
