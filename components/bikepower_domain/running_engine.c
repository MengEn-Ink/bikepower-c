/**
 * 跑步模式数据引擎实现。
 */
#include "domain_rng.h"
#include "running_engine.h"
#include <math.h>

static int clamp_int(int value, int lo, int hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static void running_walk_reset(running_walk_t *w)
{
    w->value = 0.0f;
    w->target = 0;
    w->hold = 0;
    w->init = false;
}

static int running_walk_step(running_walk_t *w, int amp)
{
    if (amp <= 0) {
        w->value = 0.0f;
        w->target = 0;
        w->hold = 0;
        w->init = true;
        return 0;
    }
    if (!w->init || w->value < (float)(-amp) || w->value > (float)amp) {
        w->value = 0.0f;
        w->hold = 0;
        w->init = true;
    }
    if (w->hold <= 0) {
        w->target = domain_rng_range(-amp, amp);
        w->hold = FLUCT_WALK_HOLD_LOOPS;
    } else {
        w->hold--;
    }
    float dt = (float)HR_LOOP_INTERVAL_MS / 1000.0f;
    float alpha = 1.0f - expf(-dt / (float)FLUCT_WALK_TAU);
    w->value = w->value + alpha * ((float)w->target - w->value);
    if (w->value < (float)(-amp)) w->value = (float)(-amp);
    else if (w->value > (float)amp) w->value = (float)amp;
    return (int)(w->value >= 0 ? w->value + 0.5f : w->value - 0.5f);
}

static int pace_sec_from_speed(int speed_x100_kmh)
{
    if (speed_x100_kmh <= 0) return 0;
    return (360000 + speed_x100_kmh / 2) / speed_x100_kmh;
}

static int speed_from_pace_sec(int pace_sec)
{
    if (pace_sec <= 0) return RUN_SPEED_X100_DEFAULT;
    return clamp_int((360000 + pace_sec / 2) / pace_sec,
                     RUN_SPEED_X100_MIN, RUN_SPEED_X100_MAX);
}

void running_engine_init(running_engine_t *e)
{
    if (!e) return;
    e->speed_x100_kmh = RUN_SPEED_X100_DEFAULT;
    e->pace_float_sec = RUN_PACE_FLOAT_SEC_DEFAULT;
    e->cadence_spm = RUN_CADENCE_DEFAULT;
    e->cadence_float_spm = RUN_CADENCE_FLOAT_DEFAULT;
    running_walk_reset(&e->pace_walk);
    running_walk_reset(&e->cadence_walk);
    e->elapsed_ms = 0;
    e->distance_x1000_m = 0;
    e->snapshot = (running_snapshot_t){ 0 };
}

running_snapshot_t running_engine_snapshot(running_engine_t *e, uint32_t dt_ms)
{
    if (!e) return (running_snapshot_t){ 0 };

    e->elapsed_ms += dt_ms;
    int pace_sec = pace_sec_from_speed(e->speed_x100_kmh) + running_walk_step(&e->pace_walk, e->pace_float_sec);
    int fastest_pace = pace_sec_from_speed(RUN_SPEED_X100_MAX);
    int slowest_pace = pace_sec_from_speed(RUN_SPEED_X100_MIN);
    pace_sec = clamp_int(pace_sec, fastest_pace, slowest_pace);
    int output_speed_x100 = speed_from_pace_sec(pace_sec);
    int output_cadence_spm = clamp_int(e->cadence_spm + running_walk_step(&e->cadence_walk, e->cadence_float_spm),
                                       RUN_CADENCE_MIN, RUN_CADENCE_MAX);

    uint32_t delta_x1000_m = (uint32_t)(((uint64_t)output_speed_x100 * 10000u * dt_ms) / 3600000u);
    e->distance_x1000_m += delta_x1000_m;

    uint16_t rsc_speed = (uint16_t)(((uint32_t)output_speed_x100 * 256u + 180u) / 360u);
    uint16_t stride_cm = 0;
    if (output_cadence_spm > 0) {
        stride_cm = (uint16_t)(((uint32_t)output_speed_x100 * 100u + (uint32_t)output_cadence_spm * 3u) / ((uint32_t)output_cadence_spm * 6u));
    }

    e->snapshot.speed_x100_kmh = output_speed_x100;
    e->snapshot.cadence_spm = output_cadence_spm;
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

bool running_engine_set_pace_float(running_engine_t *e, int seconds)
{
    if (!e) return false;
    int clamped = clamp_int(seconds, RUN_PACE_FLOAT_SEC_MIN, RUN_PACE_FLOAT_SEC_MAX);
    if (e->pace_float_sec == clamped) return false;
    e->pace_float_sec = clamped;
    running_walk_reset(&e->pace_walk);
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

bool running_engine_set_cadence_float(running_engine_t *e, int amp_spm)
{
    if (!e) return false;
    int clamped = clamp_int(amp_spm, RUN_CADENCE_FLOAT_MIN, RUN_CADENCE_FLOAT_MAX);
    if (e->cadence_float_spm == clamped) return false;
    e->cadence_float_spm = clamped;
    running_walk_reset(&e->cadence_walk);
    return true;
}
