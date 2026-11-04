#include "cps_input_codec.h"

static uint16_t get_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

bool cps_input_decode(const uint8_t *data, size_t length, cps_input_t *out)
{
    if (!data || !out || length < 4) return false;
    uint16_t flags = get_u16(data);
    if (flags & 0xe000) return false;
    cps_input_t next = { .power_w = (int16_t)get_u16(data + 2) };
    size_t offset = 4;
    static const uint8_t field_lengths[12] = {1, 0, 2, 0, 6, 4, 4, 4, 3, 2, 2, 2};
    for (unsigned bit = 0; bit < 12; bit++) {
        if (!(flags & (1u << bit))) continue;
        size_t size = field_lengths[bit];
        if (size > length - offset) return false;
        if (bit == 5) {
            next.has_crank = true;
            next.crank_revolutions = get_u16(data + offset);
            next.crank_event_time_1024 = get_u16(data + offset + 2);
        }
        offset += size;
    }
    if (offset != length) return false;
    *out = next;
    return true;
}
