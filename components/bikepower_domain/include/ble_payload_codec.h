/**
 * BLE 运动协议 payload 编码器（纯 C，可 host 单测）。
 *
 * 只负责 flags、字段顺序和小端编码；不维护累计值、时钟或 BLE 状态。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    BLE_PAYLOAD_CPS_LEN = 8,
    BLE_PAYLOAD_CSC_LEN = 5,
    BLE_PAYLOAD_HRS_LEN = 2,
    BLE_PAYLOAD_FTMS_BIKE_MAX_LEN = 8,
    BLE_PAYLOAD_FTMS_TREADMILL_LEN = 10,
    BLE_PAYLOAD_RSC_LEN = 10,
};

size_t ble_payload_encode_cps(uint8_t *out, size_t capacity,
                              int16_t power_w,
                              uint16_t crank_revolutions,
                              uint16_t crank_event_time_1024);

size_t ble_payload_encode_csc(uint8_t *out, size_t capacity,
                              uint16_t crank_revolutions,
                              uint16_t crank_event_time_1024);

size_t ble_payload_encode_hrs(uint8_t *out, size_t capacity, uint8_t heart_rate_bpm);

size_t ble_payload_encode_ftms_bike(uint8_t *out, size_t capacity,
                                    bool include_cadence,
                                    bool include_power,
                                    uint16_t speed_x100_kmh,
                                    uint16_t cadence_half_rpm,
                                    int16_t power_w);

size_t ble_payload_encode_ftms_treadmill(uint8_t *out, size_t capacity,
                                         uint16_t speed_x100_kmh,
                                         uint32_t total_distance_m,
                                         uint8_t heart_rate_bpm,
                                         uint16_t elapsed_time_s);

size_t ble_payload_encode_rsc(uint8_t *out, size_t capacity,
                              bool running,
                              uint16_t speed_1_256_mps,
                              uint8_t cadence_spm,
                              uint16_t stride_cm,
                              uint32_t total_distance_dm);
