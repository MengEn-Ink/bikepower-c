#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

size_t ble_payload_encode_cps(uint8_t *out, size_t cap,
                              int16_t power_w, uint16_t crank_revs,
                              uint16_t crank_event_time);
size_t ble_payload_encode_csc(uint8_t *out, size_t cap,
                              uint16_t crank_revs,
                              uint16_t crank_event_time);
size_t ble_payload_encode_hrs(uint8_t *out, size_t cap,
                              uint8_t heart_rate_bpm);
size_t ble_payload_encode_indoor_bike(uint8_t *out, size_t cap,
                                      bool include_cadence,
                                      bool include_power,
                                      uint16_t speed_x100_kmh,
                                      uint16_t cadence_rpm,
                                      int16_t power_w);
size_t ble_payload_encode_treadmill(uint8_t *out, size_t cap,
                                    uint16_t speed_x100_kmh,
                                    uint32_t distance_m,
                                    uint8_t heart_rate_bpm,
                                    uint16_t elapsed_s);
size_t ble_payload_encode_rsc(uint8_t *out, size_t cap,
                              bool running,
                              uint16_t speed_1_256_mps,
                              uint8_t cadence_spm,
                              uint16_t stride_cm,
                              uint32_t total_distance_x10_m);
