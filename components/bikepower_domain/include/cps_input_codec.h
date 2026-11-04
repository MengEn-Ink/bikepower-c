#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t power_w;
    bool has_crank;
    uint16_t crank_revolutions;
    uint16_t crank_event_time_1024;
} cps_input_t;

/* 完整检查 CPS 可选字段长度；失败时不修改输出。 */
bool cps_input_decode(const uint8_t *data, size_t length, cps_input_t *out);
