#include <assert.h>
#include <string.h>
#include "device_telemetry.h"

int main(void)
{
    telemetry_snapshot_t s = {
        .sequence = 0x1234, .free_heap = 0x01020304,
        .minimum_heap = 5, .largest_block = 6, .total_heap = 7,
        .uptime_s = 8, .cpu_valid = true, .cpu_busy = 2500,
        .cpu_mhz = 160, .main_cpu = 100, .main_stack = 2048,
        .state = 2, .profile = 1, .connected = true, .rssi_valid = true,
        .rssi = -65, .mtu = 23, .interval = 24, .latency = 0,
        .timeout = 400, .submitted = 9, .failed = 10,
        .allocation_failed = 11, .lock_skipped = 12,
    };
    uint8_t out[20];
    const uint8_t memory[20] = {
        1, 1, 0x34, 0x12, 4, 3, 2, 1, 5, 0, 0, 0,
        6, 0, 0, 0, 7, 0, 0, 0,
    };
    const uint8_t system[20] = {
        1, 1, 0x34, 0x12, 8, 0, 0, 0, 0xc4, 9, 160, 0,
        100, 0, 0, 8, 0, 0, 2, 1,
    };
    const uint8_t link[20] = {
        1, 3, 0x34, 0x12, 0xbf, 1, 23, 0, 24, 0, 0, 0,
        0x90, 1, 0, 0, 0, 0, 0, 0,
    };
    const uint8_t counters[20] = {
        1, 1, 0x34, 0x12, 9, 0, 0, 0, 10, 0, 0, 0,
        11, 0, 0, 0, 12, 0, 0, 0,
    };
    const uint8_t *expected[] = {memory, system, link, counters};
    for (int i = 0; i < 4; ++i) {
        assert(telemetry_encode(i, &s, out, sizeof(out)) == 20);
        assert(memcmp(out, expected[i], 20) == 0);
        memset(out, 0xaa, 20);
        assert(telemetry_encode(i, &s, out, 19) == 0);
        assert(out[0] == 0xaa);
    }
    assert(!telemetry_encode(4, &s, out, sizeof(out)));
    telemetry_cpu_window_t window = {0};
    uint16_t busy = 0, main_cpu = 0;
    assert(!telemetry_cpu_sample(&window, 100, 50, 10, &busy, &main_cpu));
    assert(telemetry_cpu_sample(&window, 1100, 800, 35, &busy, &main_cpu));
    assert(busy == 2500 && main_cpu == 250);
    assert(!telemetry_cpu_sample(&window, 1100, 800, 35, &busy, &main_cpu));
    assert(!telemetry_cpu_sample(&window, 1200, 1000, 36, &busy, &main_cpu));
    window = (telemetry_cpu_window_t){.valid = true, .total = 0xfffffff0,
        .idle = 0xfffffff0, .main = 0xfffffff0};
    assert(telemetry_cpu_sample(&window, 0x54, 0x22, 0xfffffff5, &busy, &main_cpu));
    assert(busy == 5000 && main_cpu == 500);
    s.cpu_valid = false;
    assert(telemetry_encode(1, &s, out, sizeof(out)));
    assert(out[1] == 0 && out[8] == 0 && out[9] == 0);
    return 0;
}
