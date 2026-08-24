#include "ble_payload_codec.h"

static void write_le16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)(value >> 8);
}

static void write_le24(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
    out[2] = (uint8_t)((value >> 16) & 0xffu);
}

static void write_le32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
    out[2] = (uint8_t)((value >> 16) & 0xffu);
    out[3] = (uint8_t)(value >> 24);
}

size_t ble_payload_encode_cps(uint8_t *out, size_t cap,
                              int16_t power_w, uint16_t crank_revs,
                              uint16_t crank_event_time)
{
    if (!out || cap < 8) return 0;
    write_le16(out, 0x0020);
    write_le16(out + 2, (uint16_t)power_w);
    write_le16(out + 4, crank_revs);
    write_le16(out + 6, crank_event_time);
    return 8;
}

size_t ble_payload_encode_csc(uint8_t *out, size_t cap,
                              uint16_t crank_revs,
                              uint16_t crank_event_time)
{
    if (!out || cap < 5) return 0;
    out[0] = 0x02;
    write_le16(out + 1, crank_revs);
    write_le16(out + 3, crank_event_time);
    return 5;
}

size_t ble_payload_encode_hrs(uint8_t *out, size_t cap,
                              uint8_t heart_rate_bpm)
{
    if (!out || cap < 2) return 0;
    out[0] = 0;
    out[1] = heart_rate_bpm;
    return 2;
}

size_t ble_payload_encode_indoor_bike(uint8_t *out, size_t cap,
                                      bool include_cadence,
                                      bool include_power,
                                      uint16_t speed_x100_kmh,
                                      uint16_t cadence_rpm,
                                      int16_t power_w)
{
    size_t required = 2 + (include_cadence ? 4 : 0) + (include_power ? 2 : 0);
    if (!out || cap < required || (!include_cadence && !include_power)) return 0;

    uint16_t flags = include_cadence ? 0x0004 : 0x0001;
    if (include_power) flags |= 0x0040;
    write_le16(out, flags);

    size_t offset = 2;
    if (include_cadence) {
        write_le16(out + offset, speed_x100_kmh);
        offset += 2;
        write_le16(out + offset, (uint16_t)(cadence_rpm * 2u));
        offset += 2;
    }
    if (include_power) {
        write_le16(out + offset, (uint16_t)power_w);
        offset += 2;
    }
    return offset;
}

size_t ble_payload_encode_treadmill(uint8_t *out, size_t cap,
                                    uint16_t speed_x100_kmh,
                                    uint32_t distance_m,
                                    uint8_t heart_rate_bpm,
                                    uint16_t elapsed_s)
{
    if (!out || cap < 10) return 0;
    write_le16(out, 0x0504);
    write_le16(out + 2, speed_x100_kmh);
    write_le24(out + 4, distance_m);
    out[7] = heart_rate_bpm;
    write_le16(out + 8, elapsed_s);
    return 10;
}

size_t ble_payload_encode_rsc(uint8_t *out, size_t cap,
                              bool running,
                              uint16_t speed_1_256_mps,
                              uint8_t cadence_spm,
                              uint16_t stride_cm,
                              uint32_t total_distance_x10_m)
{
    if (!out || cap < 10) return 0;
    out[0] = running ? 0x07 : 0x03;
    write_le16(out + 1, running ? speed_1_256_mps : 0);
    out[3] = running ? cadence_spm : 0;
    write_le16(out + 4, running ? stride_cm : 0);
    write_le32(out + 6, total_distance_x10_m);
    return 10;
}
