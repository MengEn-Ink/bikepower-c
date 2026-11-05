#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TELEMETRY_VERSION 1
#define TELEMETRY_GROUP_COUNT 4
#define TELEMETRY_RECORD_LEN 20

typedef struct {
    uint16_t sequence;
    uint32_t free_heap, minimum_heap, largest_block, total_heap;
    uint32_t uptime_s, main_stack;
    uint16_t cpu_busy, cpu_mhz, main_cpu;
    uint8_t state, profile;
    bool cpu_valid, connected, rssi_valid;
    int8_t rssi;
    uint16_t mtu, interval, latency, timeout;
    uint32_t submitted, failed, allocation_failed, lock_skipped;
} telemetry_snapshot_t;

typedef struct {
    bool valid;
    uint32_t total, idle, main;
} telemetry_cpu_window_t;

/* 万分比：首样本或异常窗口返回 false，仍更新基线以便下一窗口恢复。 */
bool telemetry_cpu_sample(telemetry_cpu_window_t *window, uint32_t total,
                          uint32_t idle, uint32_t main,
                          uint16_t *busy, uint16_t *main_cpu);
/* 四组依次为内存、系统、链路、发送计数；短缓冲和非法组不修改输出。 */
size_t telemetry_encode(unsigned group, const telemetry_snapshot_t *snapshot,
                        uint8_t *out, size_t capacity);
