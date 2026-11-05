#include <stdatomic.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bikepower_config.h"
#include "board_detect.h"
#include "ble_telemetry_service.h"
#include "device_telemetry_runtime.h"

static atomic_uint_least32_t s_boot;
static int64_t s_last_sample;
static uint16_t s_sequence;
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static telemetry_cpu_window_t s_cpu;
#endif

static void init_boot(void)
{
    if (!s_boot) {
        uint32_t value = esp_random();
        uint_least32_t empty = 0;
        atomic_compare_exchange_strong(&s_boot, &empty, value ? value : 1);
    }
}

void device_telemetry_meta(uint8_t out[TELEMETRY_RECORD_LEN])
{
    init_boot();
    memset(out, 0, TELEMETRY_RECORD_LEN);
    out[0] = TELEMETRY_VERSION;
    out[1] = 0x0f;
    out[2] = BLE_TELEMETRY_INTERVAL_MS & 0xff;
    out[3] = BLE_TELEMETRY_INTERVAL_MS >> 8;
    for (unsigned i = 0; i < 4; ++i) out[4 + i] = (uint8_t)(s_boot >> (8 * i));
    const char *version = FIRMWARE_VERSION;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned value = 0;
        while (*version >= '0' && *version <= '9') value = value * 10 + (unsigned)(*version++ - '0');
        out[8 + i] = (uint8_t)value;
        if (*version == '.') ++version;
    }
    out[11] = strcmp(board_get_desc()->name, "m5stamp-c3") == 0 ? 1 : 0;
    out[12] = (uint8_t)esp_reset_reason();
    size_t length = strlen(BIKEPOWER_GIT_SHA);
    if (length > 7) length = 7;
    memcpy(out + 13, BIKEPOWER_GIT_SHA, length);
}

void device_telemetry_sample(uint8_t state, uint8_t profile)
{
    int64_t now = esp_timer_get_time();
    if (s_last_sample && now - s_last_sample < (int64_t)BLE_TELEMETRY_INTERVAL_MS * 1000) return;
    s_last_sample = now;
    init_boot();
    multi_heap_info_t heap;
    heap_caps_get_info(&heap, MALLOC_CAP_8BIT);
    telemetry_snapshot_t s = {
        .sequence = ++s_sequence, .free_heap = heap.total_free_bytes,
        .minimum_heap = heap.minimum_free_bytes, .largest_block = heap.largest_free_block,
        .total_heap = heap.total_free_bytes + heap.total_allocated_bytes,
        .uptime_s = (uint32_t)(now / 1000000),
        .cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .main_stack = uxTaskGetStackHighWaterMark(NULL), .state = state, .profile = profile,
    };
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    TaskStatus_t main_status;
    vTaskGetInfo(NULL, &main_status, pdFALSE, eInvalid);
    uint32_t total = (uint32_t)portGET_RUN_TIME_COUNTER_VALUE();
    uint32_t idle = (uint32_t)ulTaskGetIdleRunTimeCounter();
    s.cpu_valid = telemetry_cpu_sample(&s_cpu, total, idle,
        (uint32_t)main_status.ulRunTimeCounter, &s.cpu_busy, &s.main_cpu);
#endif
    ble_telemetry_service_publish(&s);
}
