/**
 * 跑步模式数据引擎（纯 C，可 host 单测）。
 * 职责：生成 RSC/FTMS Treadmill 可复用的速度、步频、步幅、距离和用时快照。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "domain_config.h"

typedef struct {
    int speed_x100_kmh;
    int cadence_spm;
    int incline_x10;
    uint32_t total_distance_m;
    uint32_t elapsed_time_s;
    uint16_t rsc_speed_1_256_mps;
    uint16_t stride_cm;
} running_snapshot_t;

typedef struct {
    float value;
    int target;
    int hold;
    bool init;
} running_walk_t;

typedef struct {
    int speed_x100_kmh;
    int pace_float_sec;
    int cadence_spm;
    int cadence_float_spm;
    int incline_x10;
    running_walk_t pace_walk;
    running_walk_t cadence_walk;
    uint32_t elapsed_ms;
    uint32_t distance_x1000_m;
    running_snapshot_t snapshot;
} running_engine_t;

void running_engine_init(running_engine_t *e);
running_snapshot_t running_engine_snapshot(running_engine_t *e, uint32_t dt_ms);
bool running_engine_set_speed(running_engine_t *e, int speed_x100_kmh);
bool running_engine_set_pace_float(running_engine_t *e, int seconds);
bool running_engine_set_cadence(running_engine_t *e, int cadence_spm);
bool running_engine_set_cadence_float(running_engine_t *e, int amp_spm);
bool running_engine_set_incline(running_engine_t *e, int incline_x10);
