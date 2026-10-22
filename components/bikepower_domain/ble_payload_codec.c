#include "ble_payload_codec.h"

#define CPS_FLAG_CRANK_REVOLUTION_DATA 0x0020u
#define CSC_FLAG_CRANK_REVOLUTION_DATA 0x02u
#define FTMS_BIKE_FLAG_MORE_DATA        0x0001u
#define FTMS_BIKE_FLAG_CADENCE          0x0004u
#define FTMS_BIKE_FLAG_POWER            0x0040u
#define FTMS_TREADMILL_FLAGS             0x0504u
#define RSC_FLAG_STRIDE                  0x01u
#define RSC_FLAG_DISTANCE                0x02u
#define RSC_FLAG_RUNNING                 0x04u
#define FTMS_DISTANCE_MAX                0x00ffffffu

static void put_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)(value >> 8);
}

static void put_u24_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
    out[2] = (uint8_t)((value >> 16) & 0xffu);
}

static void put_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
    out[2] = (uint8_t)((value >> 16) & 0xffu);
    out[3] = (uint8_t)(value >> 24);
}

size_t ble_payload_encode_cps(uint8_t *out, size_t capacity,
                              int16_t power_w,
                              uint16_t crank_revolutions,
                              uint16_t crank_event_time_1024)
{
    if (!out || capacity < BLE_PAYLOAD_CPS_LEN) return 0;
    put_u16_le(out, CPS_FLAG_CRANK_REVOLUTION_DATA);
    put_u16_le(out + 2, (uint16_t)power_w);
    put_u16_le(out + 4, crank_revolutions);
    put_u16_le(out + 6, crank_event_time_1024);
    return BLE_PAYLOAD_CPS_LEN;
}

size_t ble_payload_encode_csc(uint8_t *out, size_t capacity,
                              uint16_t crank_revolutions,
                              uint16_t crank_event_time_1024)
{
    if (!out || capacity < BLE_PAYLOAD_CSC_LEN) return 0;
    out[0] = CSC_FLAG_CRANK_REVOLUTION_DATA;
    put_u16_le(out + 1, crank_revolutions);
    put_u16_le(out + 3, crank_event_time_1024);
    return BLE_PAYLOAD_CSC_LEN;
}

size_t ble_payload_encode_hrs(uint8_t *out, size_t capacity, uint8_t heart_rate_bpm)
{
    if (!out || capacity < BLE_PAYLOAD_HRS_LEN) return 0;
    out[0] = 0x00;
    out[1] = heart_rate_bpm;
    return BLE_PAYLOAD_HRS_LEN;
}

size_t ble_payload_encode_ftms_bike(uint8_t *out, size_t capacity,
                                    bool include_cadence,
                                    bool include_power,
                                    uint16_t speed_x100_kmh,
                                    uint16_t cadence_half_rpm,
                                    int16_t power_w)
{
    if (!out || (!include_cadence && !include_power)) return 0;
    size_t required = 2u + (include_cadence ? 4u : 0u) + (include_power ? 2u : 0u);
    if (capacity < required) return 0;

    uint16_t flags = include_cadence ? 0u : FTMS_BIKE_FLAG_MORE_DATA;
    size_t offset = 2;
    if (include_cadence) {
        flags |= FTMS_BIKE_FLAG_CADENCE;
        put_u16_le(out + offset, speed_x100_kmh);
        offset += 2;
        put_u16_le(out + offset, cadence_half_rpm);
        offset += 2;
    }
    if (include_power) {
        flags |= FTMS_BIKE_FLAG_POWER;
        put_u16_le(out + offset, (uint16_t)power_w);
        offset += 2;
    }
    put_u16_le(out, flags);
    return offset;
}

size_t ble_payload_encode_ftms_treadmill(uint8_t *out, size_t capacity,
                                         uint16_t speed_x100_kmh,
                                         uint32_t total_distance_m,
                                         uint8_t heart_rate_bpm,
                                         uint16_t elapsed_time_s)
{
    if (!out || capacity < BLE_PAYLOAD_FTMS_TREADMILL_LEN
            || total_distance_m > FTMS_DISTANCE_MAX) {
        return 0;
    }
    put_u16_le(out, FTMS_TREADMILL_FLAGS);
    put_u16_le(out + 2, speed_x100_kmh);
    put_u24_le(out + 4, total_distance_m);
    out[7] = heart_rate_bpm;
    put_u16_le(out + 8, elapsed_time_s);
    return BLE_PAYLOAD_FTMS_TREADMILL_LEN;
}

size_t ble_payload_encode_rsc(uint8_t *out, size_t capacity,
                              bool running,
                              uint16_t speed_1_256_mps,
                              uint8_t cadence_spm,
                              uint16_t stride_cm,
                              uint32_t total_distance_dm)
{
    if (!out || capacity < BLE_PAYLOAD_RSC_LEN) return 0;
    out[0] = RSC_FLAG_STRIDE | RSC_FLAG_DISTANCE | (running ? RSC_FLAG_RUNNING : 0u);
    put_u16_le(out + 1, speed_1_256_mps);
    out[3] = cadence_spm;
    put_u16_le(out + 4, stride_cm);
    put_u32_le(out + 6, total_distance_dm);
    return BLE_PAYLOAD_RSC_LEN;
}
