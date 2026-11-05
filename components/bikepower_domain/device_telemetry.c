#include <string.h>
#include "device_telemetry.h"

static void put16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *out, uint32_t value)
{
    put16(out, (uint16_t)value);
    put16(out + 2, (uint16_t)(value >> 16));
}

bool telemetry_cpu_sample(telemetry_cpu_window_t *w, uint32_t total,
                          uint32_t idle, uint32_t main,
                          uint16_t *busy, uint16_t *main_cpu)
{
    uint32_t dt = total - w->total, di = idle - w->idle, dm = main - w->main;
    bool valid = w->valid && dt && di <= dt && dm <= dt - di;
    *w = (telemetry_cpu_window_t){true, total, idle, main};
    if (!valid) return false;
    *busy = (uint16_t)((uint64_t)(dt - di) * 10000 / dt);
    *main_cpu = (uint16_t)((uint64_t)dm * 10000 / dt);
    return true;
}

size_t telemetry_encode(unsigned group, const telemetry_snapshot_t *s,
                        uint8_t *out, size_t capacity)
{
    if (!s || !out || group >= TELEMETRY_GROUP_COUNT || capacity < TELEMETRY_RECORD_LEN) return 0;
    memset(out, 0, TELEMETRY_RECORD_LEN);
    out[0] = TELEMETRY_VERSION;
    out[1] = 1;
    put16(out + 2, s->sequence);
    switch (group) {
    case 0:
        put32(out + 4, s->free_heap);
        put32(out + 8, s->minimum_heap);
        put32(out + 12, s->largest_block);
        put32(out + 16, s->total_heap);
        break;
    case 1:
        out[1] = s->cpu_valid ? 1 : 0;
        put32(out + 4, s->uptime_s);
        put16(out + 8, s->cpu_valid ? s->cpu_busy : 0);
        put16(out + 10, s->cpu_mhz);
        put16(out + 12, s->cpu_valid ? s->main_cpu : 0);
        put32(out + 14, s->main_stack);
        out[18] = s->state;
        out[19] = s->profile;
        break;
    case 2:
        out[1] = (s->connected ? 1 : 0) | (s->rssi_valid ? 2 : 0);
        out[4] = s->rssi_valid ? (uint8_t)s->rssi : 0;
        out[5] = s->connected;
        put16(out + 6, s->mtu);
        put16(out + 8, s->interval);
        put16(out + 10, s->latency);
        put16(out + 12, s->timeout);
        break;
    case 3:
        put32(out + 4, s->submitted);
        put32(out + 8, s->failed);
        put32(out + 12, s->allocation_failed);
        put32(out + 16, s->lock_skipped);
        break;
    }
    return TELEMETRY_RECORD_LEN;
}
