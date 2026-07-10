/**
 * 跑步模式数据引擎实现。
 */
#include "domain_rng.h"
#include "running_engine.h"
#include <math.h>
#include <string.h>

const run_pacer_t RUN_PACERS[RUN_PACER_PRESET_COUNT] = {
    { "慢跑", 390, 450, 165 },
    { "有氧", 330, 390, 170 },
    { "节奏", 270, 315, 174 },
    { "间歇", 210, 255, 180 },
};

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
    e->mode = RUN_MODE_DEFAULT;
    e->pacer_type = RUN_PACER_DEFAULT;
    e->pace_low_sec = RUN_PACERS[RUN_PACER_DEFAULT].pace_low_sec;
    e->pace_high_sec = RUN_PACERS[RUN_PACER_DEFAULT].pace_high_sec;
    strncpy(e->custom_name, RUN_CUSTOM_NAME_DEFAULT, RUN_CUSTOM_NAME_MAX - 1);
    e->custom_name[RUN_CUSTOM_NAME_MAX - 1] = '\0';
    e->incline_x10 = RUN_INCLINE_X10_DEFAULT;
    running_walk_reset(&e->pace_walk);
    running_walk_reset(&e->cadence_walk);
    running_walk_reset(&e->pacer_pace_walk);
    e->elapsed_ms = 0;
    e->distance_x1000_m = 0;
    e->snapshot = (running_snapshot_t){ 0 };
}

running_snapshot_t running_engine_snapshot(running_engine_t *e, uint32_t dt_ms)
{
    if (!e) return (running_snapshot_t){ 0 };

    e->elapsed_ms += dt_ms;
    int pace_sec;
    if (e->mode == RUN_MODE_PACER) {
        int lo = clamp_int(e->pace_low_sec, RUN_PACE_SEC_MIN, RUN_PACE_SEC_MAX);
        int hi = clamp_int(e->pace_high_sec, RUN_PACE_SEC_MIN, RUN_PACE_SEC_MAX);
        if (lo > hi) hi = lo;
        int mid = (lo + hi) / 2;
        int amp = (hi - lo) / 2;
        pace_sec = mid + running_walk_step(&e->pacer_pace_walk, amp);
        pace_sec = clamp_int(pace_sec, lo, hi);
    } else {
        pace_sec = pace_sec_from_speed(e->speed_x100_kmh) + running_walk_step(&e->pace_walk, e->pace_float_sec);
    }
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
    bool changed = e->speed_x100_kmh != clamped || e->mode != RUN_MODE_FIXED;
    e->speed_x100_kmh = clamped;
    e->mode = RUN_MODE_FIXED;
    if (changed) running_walk_reset(&e->pace_walk);
    return changed;
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

bool running_engine_set_mode(running_engine_t *e, run_mode_t mode)
{
    if (!e) return false;
    if (mode < 0 || mode >= RUN_MODE_COUNT) return false;
    if (e->mode == mode) return false;
    e->mode = mode;
    running_walk_reset(mode == RUN_MODE_PACER ? &e->pacer_pace_walk : &e->pace_walk);
    return true;
}

bool running_engine_set_pace_range(running_engine_t *e, int pace_low_sec, int pace_high_sec)
{
    if (!e) return false;
    int lo = clamp_int(pace_low_sec, RUN_PACE_SEC_MIN, RUN_PACE_SEC_MAX);
    int hi = clamp_int(pace_high_sec, RUN_PACE_SEC_MIN, RUN_PACE_SEC_MAX);
    if (lo > hi) hi = lo;
    if (e->pace_low_sec == lo && e->pace_high_sec == hi) return false;
    e->pace_low_sec = lo;
    e->pace_high_sec = hi;
    running_walk_reset(&e->pacer_pace_walk);
    return true;
}

bool running_engine_set_pacer(running_engine_t *e, run_pacer_type_t pacer)
{
    if (!e) return false;
    if (pacer < 0 || pacer >= RUN_PACER_COUNT) return false;

    bool changed = false;
    if (e->pacer_type != pacer) {
        e->pacer_type = pacer;
        changed = true;
    }
    if (pacer < RUN_PACER_PRESET_COUNT) {
        const run_pacer_t *tpl = &RUN_PACERS[pacer];
        if (running_engine_set_pace_range(e, tpl->pace_low_sec, tpl->pace_high_sec)) changed = true;
        int cad = clamp_int(tpl->cadence_spm, RUN_CADENCE_MIN, RUN_CADENCE_MAX);
        if (e->cadence_spm != cad) {
            e->cadence_spm = cad;
            changed = true;
        }
    }
    if (e->mode != RUN_MODE_PACER) {
        e->mode = RUN_MODE_PACER;
        changed = true;
    }
    if (changed) running_walk_reset(&e->pacer_pace_walk);
    return changed;
}

bool running_engine_set_custom_pacer(running_engine_t *e, const char *name, int pace_low_sec, int pace_high_sec, int cadence_spm)
{
    if (!e) return false;
    bool changed = false;
    char next_name[RUN_CUSTOM_NAME_MAX];
    const char *src = (name && name[0]) ? name : RUN_CUSTOM_NAME_DEFAULT;
    strncpy(next_name, src, RUN_CUSTOM_NAME_MAX - 1);
    next_name[RUN_CUSTOM_NAME_MAX - 1] = '\0';
    if (strncmp(e->custom_name, next_name, RUN_CUSTOM_NAME_MAX) != 0) {
        strncpy(e->custom_name, next_name, RUN_CUSTOM_NAME_MAX - 1);
        e->custom_name[RUN_CUSTOM_NAME_MAX - 1] = '\0';
        changed = true;
    }
    if (running_engine_set_pace_range(e, pace_low_sec, pace_high_sec)) changed = true;
    int cad = clamp_int(cadence_spm, RUN_CADENCE_MIN, RUN_CADENCE_MAX);
    if (e->cadence_spm != cad) {
        e->cadence_spm = cad;
        changed = true;
    }
    if (e->pacer_type != RUN_PACER_CUSTOM) {
        e->pacer_type = RUN_PACER_CUSTOM;
        changed = true;
    }
    if (e->mode != RUN_MODE_PACER) {
        e->mode = RUN_MODE_PACER;
        changed = true;
    }
    if (changed) running_walk_reset(&e->pacer_pace_walk);
    return changed;
}

bool running_engine_set_incline(running_engine_t *e, int incline_x10)
{
    if (!e) return false;
    int clamped = clamp_int(incline_x10, RUN_INCLINE_X10_MIN, RUN_INCLINE_X10_MAX);
    if (e->incline_x10 == clamped) return false;
    e->incline_x10 = clamped;
    return true;
}

void running_engine_reset_session(running_engine_t *e)
{
    if (!e) return;
    e->elapsed_ms = 0;
    e->distance_x1000_m = 0;
    e->snapshot.total_distance_m = 0;
    e->snapshot.elapsed_time_s = 0;
}
