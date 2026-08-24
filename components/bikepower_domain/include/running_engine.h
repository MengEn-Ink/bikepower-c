/**
 * 跑步模式数据引擎（纯 C，可 host 单测）。
 * 职责：生成 RSC/FTMS Treadmill 可复用的速度、步频、步幅、距离和用时快照。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "domain_config.h"

typedef struct {
    bool running;
    int speed_x100_kmh;
    int cadence_spm;
    uint32_t total_distance_m;
    uint32_t total_distance_x10_m;
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
    bool running;
    int speed_x100_kmh;
    int pace_float_sec;
    int cadence_spm;
    int cadence_float_spm;
    run_mode_t mode;
    run_pacer_type_t pacer_type;
    int pace_low_sec;
    int pace_high_sec;
    char custom_name[RUN_CUSTOM_NAME_MAX];
    running_walk_t pace_walk;
    running_walk_t cadence_walk;
    running_walk_t pacer_pace_walk;
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
bool running_engine_set_mode(running_engine_t *e, run_mode_t mode);
bool running_engine_set_pacer(running_engine_t *e, run_pacer_type_t pacer);
bool running_engine_set_custom_pacer(running_engine_t *e, const char *name, int pace_low_sec, int pace_high_sec, int cadence_spm);
bool running_engine_set_pace_range(running_engine_t *e, int pace_low_sec, int pace_high_sec);
bool running_engine_set_running(running_engine_t *e, bool running);
void running_engine_reset_session(running_engine_t *e);
