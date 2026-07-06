/**
 * 跑步模式数据引擎实现。
 */
#include "running_engine.h"

static int clamp_int(int value, int lo, int hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void running_engine_init(running_engine_t *e)
{
    if (!e) return;
    e->speed_x100_kmh = RUN_SPEED_X100_DEFAULT;
    e->cadence_spm = RUN_CADENCE_DEFAULT;
    e->incline_x10 = RUN_INCLINE_X10_DEFAULT;
    e->elapsed_ms = 0;
    e->distance_x1000_m = 0;
    e->snapshot = (running_snapshot_t){ 0 };
}

running_snapshot_t running_engine_snapshot(running_engine_t *e, uint32_t dt_ms)
{
    if (!e) return (running_snapshot_t){ 0 };

    e->elapsed_ms += dt_ms;
    uint32_t delta_x1000_m = (uint32_t)(((uint64_t)e->speed_x100_kmh * 10000u * dt_ms) / 3600000u);
    e->distance_x1000_m += delta_x1000_m;

    uint16_t rsc_speed = (uint16_t)(((uint32_t)e->speed_x100_kmh * 256u + 180u) / 360u);
    uint16_t stride_cm = 0;
    if (e->cadence_spm > 0) {
        stride_cm = (uint16_t)(((uint32_t)e->speed_x100_kmh * 100u + (uint32_t)e->cadence_spm * 3u) / ((uint32_t)e->cadence_spm * 6u));
    }

    e->snapshot.speed_x100_kmh = e->speed_x100_kmh;
    e->snapshot.cadence_spm = e->cadence_spm;
    e->snapshot.incline_x10 = e->incline_x10;
    e->snapshot.total_distance_m = e->distance_x1000_m / 1000u;
    e->snapshot.elapsed_time_s = e->elapsed_ms / 1000u;
    e->snapshot.rsc_speed_1_256_mps = rsc_speed;
    e->snapshot.stride_cm = stride_cm;
    return e->snapshot;
}

bool running_engine_set_speed(running_engine_t *e, int speed_x100_kmh)
{
    if (!e) return false;
    int clamped = clamp_int(speed_x100_kmh, RUN_SPEED_X100_MIN, RUN_SPEED_X100_MAX);
    if (e->speed_x100_kmh == clamped) return false;
    e->speed_x100_kmh = clamped;
    return true;
}

bool running_engine_set_cadence(running_engine_t *e, int cadence_spm)
{
    if (!e) return false;
    int clamped = clamp_int(cadence_spm, RUN_CADENCE_MIN, RUN_CADENCE_MAX);
    if (e->cadence_spm == clamped) return false;
    e->cadence_spm = clamped;
    return true;
}

bool running_engine_set_incline(running_engine_t *e, int incline_x10)
{
    if (!e) return false;
    int clamped = clamp_int(incline_x10, RUN_INCLINE_X10_MIN, RUN_INCLINE_X10_MAX);
    if (e->incline_x10 == clamped) return false;
    e->incline_x10 = clamped;
    return true;
}
