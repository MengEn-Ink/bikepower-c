/**
 * 功率数据引擎实现 — 见 power_engine.h
 */
#include "domain_config.h"
#include "domain_rng.h"
#include "power_engine.h"
#include <math.h>
#include <string.h>

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int clamp_power(int v)   { return clampi(v, POWER_MIN, POWER_MAX); }
static int clamp_cadence(int v) { return clampi(v, CADENCE_MIN, CADENCE_MAX); }

static int running_equivalent_power(power_engine_t *e, int speed_x100_kmh)
{
    int speed = clampi(speed_x100_kmh, RUN_SPEED_X100_MIN, RUN_SPEED_X100_MAX);
    int span = RUN_SPEED_X100_MAX - RUN_SPEED_X100_MIN;
    int progress = span > 0 ? (speed - RUN_SPEED_X100_MIN) * 1000 / span : 0;
    /*
     * 跑步没有功率源，使用速度在线性强度轴上的位置折算为 FTP 百分比。
     * 3km/h 约恢复强度，25km/h 约 VO2 强度；只要求联动心率单调、保守、可解释。
     */
    int ftp = e->profile.ftp > 0 ? e->profile.ftp : PROFILE_FTP_DEFAULT;
    int pct = 35 + progress * 85 / 1000;
    return clamp_power(ftp * pct / 100);
}

/*
 * 在浮动带内推进一次有界平滑随机游走，返回相对基准值的整数偏移。
 * 参数：w 为游走状态；amp 为 ± 浮动幅度（<=0 表示恒定，偏移恒为 0）。
 * 返回值：当前偏移量（四舍五入到整数），始终落在 [-amp, +amp] 内。
 * 特殊行为：每 FLUCT_WALK_HOLD_LOOPS 拍在带内重采样目标，再一阶指数逼近，
 *           保证输出平稳变化而非逐拍随机跳变；amp 变化导致越界时会重置到带内。
 */
static int fluct_walk_step(fluct_walk_t *w, int amp)
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

static void fluct_walk_reset(fluct_walk_t *w)
{
    w->value = 0.0f;
    w->target = 0;
    w->hold = 0;
    w->init = false;
}

/*
 * 初始化功率引擎的全部运行时状态和默认配置。
 * 参数：e 为待初始化的功率引擎对象。
 * 返回值：无。
 * 特殊行为：会同时初始化用户档案、心率模型、默认快照和设备名，并按默认模式重置内部状态。
 */
void power_engine_init(power_engine_t *e)
{
    e->base_power = DEFAULT_POWER;
    e->cadence_base = DEFAULT_CADENCE;
    e->last_heartrate = DEFAULT_HEARTRATE;
    e->user_heartrate = 0;
    e->has_user_hr = false;
    e->power_float = POWER_FLOAT_DEFAULT;
    e->cadence_float = CADENCE_FLOAT_DEFAULT;
    e->hr_float = HR_FLOAT_DEFAULT;
    e->power_step = POWER_STEP_DEFAULT;
    fluct_walk_reset(&e->power_walk);
    fluct_walk_reset(&e->cadence_walk);
    e->mode = DEFAULT_RIDE_MODE;
    e->erg_active_target = 0;
    e->has_erg_target = false;
    e->last_power = DEFAULT_POWER;
    e->last_cadence = DEFAULT_CADENCE;
    profile_init(&e->profile);
    led_config_defaults(&e->led);
    hr_model_init(&e->hr, DEFAULT_HEARTRATE);
    e->sport_profile = DEFAULT_SPORT_PROFILE;
    e->broadcast_mask = BROADCAST_FIELD_ALL;
    running_engine_init(&e->running);
    e->snapshot.power = DEFAULT_POWER;
    e->snapshot.cadence = DEFAULT_CADENCE;
    e->snapshot.heartrate = DEFAULT_HEARTRATE;
    e->snapshot.mode = DEFAULT_RIDE_MODE;
    e->snapshot_count = 0;
    e->dirty = false;
    strncpy(e->device_name, DEVICE_NAME_DEFAULT, DEVICE_NAME_MAX - 1);
    e->device_name[DEVICE_NAME_MAX - 1] = '\0';
    power_engine_reset_mode_state(e);
}

static int target_heartrate(power_engine_t *e, int power)
{
    if (e->has_user_hr && e->mode == RIDE_MODE_STEADY) {
        return e->user_heartrate;
    }
    return hr_model_ftp_zone(&e->hr, power, e->profile.ftp, e->profile.thr_hr);
}

/*
 * 按当前模式重置内部功率、踏频和心率变异状态。
 * 参数：e 为功率引擎对象。
 * 返回值：无。
 * 特殊行为：road 模式按配速机器人下限功体比初始化功率，ERG 模式优先使用激活目标功率，并重置 HRV 累加器。
 */
void power_engine_reset_mode_state(power_engine_t *e)
{
    if (e->mode == RIDE_MODE_ROAD) {
        int wkg_low, wkg_high;
        profile_effective_wkg(&e->profile, &wkg_low, &wkg_high);
        (void)wkg_high;
        e->last_power = e->profile.weight * wkg_low / 10;
        e->last_cadence = e->cadence_base;
    } else if (e->mode == RIDE_MODE_ERG) {
        e->last_power = e->has_erg_target ? e->erg_active_target : e->base_power;
        e->last_cadence = e->cadence_base;
    } else {
        e->last_power = e->base_power;
        e->last_cadence = e->cadence_base;
    }
    fluct_walk_reset(&e->power_walk);
    fluct_walk_reset(&e->cadence_walk);
    hr_model_reset_variation(&e->hr);
}

/*
 * 为 ERG 模式生成一次功率、踏频和心率输出。
 * 参数：e 为功率引擎对象；po、co、ho 分别返回功率、踏频和心率结果。
 * 返回值：无。
 * 特殊行为：ERG 目标优先使用当前激活目标功率，心率会叠加平滑与 HRV 变异后再做对外发射限速。
 */
static void build_erg(power_engine_t *e, int *po, int *co, int *ho)
{
    int target = e->has_erg_target ? e->erg_active_target : e->base_power;
    int power = clamp_power(target + domain_rng_range(STEADY_POWER_FLUCT_LO, STEADY_POWER_FLUCT_HI));
    int cadence = clamp_cadence(e->cadence_base + domain_rng_range(STEADY_CADENCE_FLUCT_LO, STEADY_CADENCE_FLUCT_HI));
    int target_hr = target_heartrate(e, power);
    int hr = hr_model_smooth(&e->hr, target_hr);
    hr = hr_model_emit(&e->hr, hr_clamp((int)(hr + hr_model_variation(&e->hr))));
    *po = power; *co = cadence; *ho = hr;
}

/*
 * 为 steady 模式生成一次功率、踏频和心率输出。
 * 参数：e 为功率引擎对象；po、co、ho 分别返回功率、踏频和心率结果。
 * 返回值：无。
 * 特殊行为：功率与踏频在各自浮动带内做有界平滑随机游走（非逐拍随机）；
 *           若已设置固定用户心率，则走固定心率平滑路径并应用心率浮动幅度，不再按 FTP 分区推导目标心率。
 */
static void build_steady(power_engine_t *e, int *po, int *co, int *ho)
{
    int power = clamp_power(e->base_power + fluct_walk_step(&e->power_walk, e->power_float));
    int cadence = clamp_cadence(e->cadence_base + fluct_walk_step(&e->cadence_walk, e->cadence_float));
    if (e->has_user_hr) {
        *po = power; *co = cadence;
        *ho = hr_model_steady_user(&e->hr, e->user_heartrate, e->hr_float);
        return;
    }
    int target_hr = target_heartrate(e, power);
    int hr = hr_model_smooth(&e->hr, target_hr);
    hr = hr_model_emit(&e->hr, hr_clamp((int)(hr + hr_model_variation(&e->hr))));
    *po = power; *co = cadence; *ho = hr;
}

/*
 * 为 road 模式生成一次功率、踏频和心率输出。
 * 参数：e 为功率引擎对象；po、co、ho 分别返回功率、踏频和心率结果。
 * 返回值：无。
 * 特殊行为：功率由当前配速机器人功体比范围随机采样得到，再基于采样功率推导目标心率。
 */
static void build_road(power_engine_t *e, int *po, int *co, int *ho)
{
    int wkg_low, wkg_high;
    profile_effective_wkg(&e->profile, &wkg_low, &wkg_high);
    int wkg = domain_rng_range(wkg_low, wkg_high);
    int raw_power = e->profile.weight * wkg / 10;
    int power = clamp_power(raw_power);
    int cadence = clamp_cadence(e->cadence_base + domain_rng_range(STEADY_CADENCE_FLUCT_LO, STEADY_CADENCE_FLUCT_HI));
    int target_hr = hr_model_ftp_zone(&e->hr, power, e->profile.ftp, e->profile.thr_hr);
    int hr = hr_model_smooth(&e->hr, target_hr);
    hr = hr_model_emit(&e->hr, hr_clamp((int)(hr + hr_model_variation(&e->hr))));
    *po = power; *co = cadence; *ho = hr;
}

/*
 * 生成当前模式下一次完整的领域层快照。
 * 参数：e 为功率引擎对象。
 * 返回值：返回包含功率、踏频、心率和模式的最新快照。
 * 特殊行为：会根据当前模式分派到不同构建路径，并同步更新 last_* 字段和 snapshot_count。
 */
snapshot_t power_engine_snapshot(power_engine_t *e)
{
    int power, cadence, heartrate;
    if (e->mode == RIDE_MODE_ROAD) {
        build_road(e, &power, &cadence, &heartrate);
    } else if (e->mode == RIDE_MODE_ERG) {
        build_erg(e, &power, &cadence, &heartrate);
    } else {
        build_steady(e, &power, &cadence, &heartrate);
    }
    e->last_power = power;
    e->last_cadence = cadence;
    e->last_heartrate = heartrate;
    e->snapshot.power = power;
    e->snapshot.cadence = cadence;
    e->snapshot.heartrate = heartrate;
    e->snapshot.mode = e->mode;
    e->snapshot_count++;
    return e->snapshot;
}

/*
 * 按增量调整基准功率。
 * 参数：e 为功率引擎对象；delta 为待增加的功率偏移量。
 * 返回值：返回调整后的基准功率。
 * 特殊行为：仅 steady 模式允许调整，结果会被钳制到合法功率范围，并在数值变化时置 dirty。
 */
int power_engine_adjust_power(power_engine_t *e, int delta)
{
    if (e->mode != RIDE_MODE_STEADY) return e->base_power;
    int old_power = e->base_power;
    e->base_power = clamp_power(e->base_power + delta);
    if (old_power != e->base_power) e->dirty = true;
    return e->base_power;
}

/*
 * 直接设置基准功率。
 * 参数：e 为功率引擎对象；value 为目标基准功率。
 * 返回值：设置成功返回 true，否则返回 false。
 * 特殊行为：仅 steady 模式允许设置，越界值和与当前值相同的输入都会被拒绝。
 */
bool power_engine_set_power(power_engine_t *e, int value)
{
    if (e->mode != RIDE_MODE_STEADY) return false;
    if (value < POWER_MIN || value > POWER_MAX) return false;
    if (e->base_power == value) return false;
    e->base_power = value;
    e->last_power = value;
    e->dirty = true;
    return true;
}

/*
 * 按增量调整基准踏频。
 * 参数：e 为功率引擎对象；delta 为待增加的踏频偏移量。
 * 返回值：返回调整后的基准踏频。
 * 特殊行为：仅 steady 模式允许调整，结果会被钳制到合法踏频范围，并在数值变化时置 dirty。
 */
int power_engine_adjust_cadence(power_engine_t *e, int delta)
{
    if (e->mode != RIDE_MODE_STEADY) return e->cadence_base;
    int old = e->cadence_base;
    e->cadence_base = clamp_cadence(e->cadence_base + delta);
    if (old != e->cadence_base) e->dirty = true;
    return e->cadence_base;
}

/*
 * 直接设置基准踏频。
 * 参数：e 为功率引擎对象；value 为目标基准踏频。
 * 返回值：设置成功返回 true，否则返回 false。
 * 特殊行为：仅 steady 模式允许设置，越界值和与当前值相同的输入都会被拒绝。
 */
bool power_engine_set_cadence(power_engine_t *e, int value)
{
    if (e->mode != RIDE_MODE_STEADY) return false;
    if (value < CADENCE_MIN || value > CADENCE_MAX) return false;
    if (e->cadence_base == value) return false;
    e->cadence_base = value;
    e->last_cadence = value;
    e->dirty = true;
    return true;
}

/*
 * 设置 steady 模式下的固定用户心率。
 * 参数：e 为功率引擎对象；value 为目标心率。
 * 返回值：设置成功返回 true，否则返回 false。
 * 特殊行为：仅 steady 模式允许设置，会同步重置固定心率路径相关状态并置 dirty。
 */
bool power_engine_set_heartrate(power_engine_t *e, int value)
{
    bool direct_hr_only = (e->broadcast_mask & BROADCAST_FIELD_ALL) == BROADCAST_FIELD_HEARTRATE;
    if (e->mode != RIDE_MODE_STEADY && e->sport_profile != SPORT_PROFILE_RUNNING && !direct_hr_only) return false;
    if (value < HR_MIN || value > HR_MAX) return false;
    e->user_heartrate = value;
    e->has_user_hr = true;
    e->last_heartrate = value;
    hr_model_set_user_hr(&e->hr, value);
    e->dirty = true;
    return true;
}

/*
 * 清除固定用户心率，恢复心率随功率联动。
 * 参数：e 为功率引擎对象。
 * 返回值：无。
 * 特殊行为：清空 has_user_hr 标记并重置 HRV 累加器，使后续快照按 FTP 分区重新推导目标心率。
 */
void power_engine_clear_user_hr(power_engine_t *e)
{
    if (!e->has_user_hr) return;
    e->has_user_hr = false;
    e->user_heartrate = 0;
    hr_model_reset_variation(&e->hr);
    e->dirty = true;
}

/*
 * 设置功率浮动幅度（±W）。
 * 参数：e 为功率引擎对象；amp 为目标浮动幅度。
 * 返回值：数值实际变化返回 true，否则返回 false。
 * 特殊行为：越界值夹紧到 [POWER_FLOAT_MIN, POWER_FLOAT_MAX]；变化后重置游走状态并置 dirty；对所有模式接受设置（仅固定模式生效）。
 */
bool power_engine_set_power_float(power_engine_t *e, int amp)
{
    amp = clampi(amp, POWER_FLOAT_MIN, POWER_FLOAT_MAX);
    if (e->power_float == amp) return false;
    e->power_float = amp;
    fluct_walk_reset(&e->power_walk);
    e->dirty = true;
    return true;
}

/*
 * 设置踏频浮动幅度（±RPM）。
 * 参数：e 为功率引擎对象；amp 为目标浮动幅度。
 * 返回值：数值实际变化返回 true，否则返回 false。
 * 特殊行为：越界值夹紧到 [CADENCE_FLOAT_MIN, CADENCE_FLOAT_MAX]；变化后重置游走状态并置 dirty。
 */
bool power_engine_set_cadence_float(power_engine_t *e, int amp)
{
    amp = clampi(amp, CADENCE_FLOAT_MIN, CADENCE_FLOAT_MAX);
    if (e->cadence_float == amp) return false;
    e->cadence_float = amp;
    fluct_walk_reset(&e->cadence_walk);
    e->dirty = true;
    return true;
}

/*
 * 设置固定心率浮动幅度（±BPM）。
 * 参数：e 为功率引擎对象；amp 为目标浮动幅度。
 * 返回值：数值实际变化返回 true，否则返回 false。
 * 特殊行为：越界值夹紧到 [HR_FLOAT_MIN, HR_FLOAT_MAX]；变化后置 dirty；仅固定模式锁定心率时生效。
 */
bool power_engine_set_hr_float(power_engine_t *e, int amp)
{
    amp = clampi(amp, HR_FLOAT_MIN, HR_FLOAT_MAX);
    if (e->hr_float == amp) return false;
    e->hr_float = amp;
    e->dirty = true;
    return true;
}

/*
 * 设置物理按钮调功步进（W）。
 * 参数：e 为功率引擎对象；step 为目标步进值。
 * 返回值：数值实际变化返回 true，否则返回 false。
 * 特殊行为：越界值夹紧到 [POWER_STEP_MIN, POWER_STEP_MAX]；变化后置 dirty。影响物理按钮短按/长按调功幅度。
 */
bool power_engine_set_power_step(power_engine_t *e, int step)
{
    step = clampi(step, POWER_STEP_MIN, POWER_STEP_MAX);
    if (e->power_step == step) return false;
    e->power_step = step;
    e->dirty = true;
    return true;
}

/*
 * 切换骑行模式。
 * 参数：e 为功率引擎对象；mode 为目标模式。
 * 返回值：模式实际发生变化时返回 true，否则返回 false。
 * 特殊行为：非法模式值会被拒绝；成功切换后会立即重置该模式的内部状态并置 dirty。
 */
bool power_engine_set_mode(power_engine_t *e, ride_mode_t mode)
{
    if (mode < 0 || mode >= RIDE_MODE_COUNT) return false;
    if (e->mode == mode) return false;
    e->mode = mode;
    if (mode != RIDE_MODE_ERG) {
        e->has_erg_target = false;
        e->erg_active_target = 0;
    }
    power_engine_reset_mode_state(e);
    e->dirty = true;
    return true;
}

int power_engine_get_pacer_index(power_engine_t *e)
{
    int idx = e->profile.ride_type;
    if (!PACER_INDEX_IS_VALID(idx)) return PACER_INDEX_DEFAULT;
    return idx;
}

bool power_engine_set_pacer_index(power_engine_t *e, int pacer_index)
{
    if (!profile_set_ride_type(&e->profile, pacer_index)) return false;
    if (e->mode == RIDE_MODE_ROAD) power_engine_reset_mode_state(e);
    e->dirty = true;
    return true;
}

const char *power_engine_get_current_pacer(power_engine_t *e)
{
    int idx = power_engine_get_pacer_index(e);
    if (PACER_INDEX_IS_CUSTOM(idx)) return e->profile.custom_pacer_name;
    return ROAD_PACERS[idx].name;
}

/*
 * 设置 BLE 设备名称。
 * 参数：e 为功率引擎对象；name 为新的设备名称字符串。
 * 返回值：名称实际发生变化时返回 true，否则返回 false。
 * 特殊行为：空指针和空字符串会被拒绝；超长名称会被截断到 DEVICE_NAME_MAX-1 并补终止符。
 */
bool power_engine_set_device_name(power_engine_t *e, const char *name)
{
    if (name == NULL || name[0] == '\0') return false;
    if (strncmp(e->device_name, name, DEVICE_NAME_MAX) == 0) return false;
    strncpy(e->device_name, name, DEVICE_NAME_MAX - 1);
    e->device_name[DEVICE_NAME_MAX - 1] = '\0';
    e->dirty = true;
    return true;
}

bool power_engine_set_sport_profile(power_engine_t *e, sport_profile_t profile)
{
    if (profile < 0 || profile >= SPORT_PROFILE_COUNT) return false;
    if (e->sport_profile == profile) return false;
    e->sport_profile = profile;
    e->dirty = true;
    return true;
}

bool power_engine_set_run_speed(power_engine_t *e, int speed_x100_kmh)
{
    bool changed = running_engine_set_speed(&e->running, speed_x100_kmh);
    if (changed) e->dirty = true;
    return changed;
}

bool power_engine_set_run_pace_float(power_engine_t *e, int seconds)
{
    bool changed = running_engine_set_pace_float(&e->running, seconds);
    if (changed) e->dirty = true;
    return changed;
}

bool power_engine_set_run_cadence(power_engine_t *e, int cadence_spm)
{
    bool changed = running_engine_set_cadence(&e->running, cadence_spm);
    if (changed) e->dirty = true;
    return changed;
}

bool power_engine_set_run_cadence_float(power_engine_t *e, int amp_spm)
{
    bool changed = running_engine_set_cadence_float(&e->running, amp_spm);
    if (changed) e->dirty = true;
    return changed;
}

bool power_engine_set_run_incline(power_engine_t *e, int incline_x10)
{
    bool changed = running_engine_set_incline(&e->running, incline_x10);
    if (changed) e->dirty = true;
    return changed;
}

int power_engine_running_heartrate(power_engine_t *e, int speed_x100_kmh)
{
    if (!e) return DEFAULT_HEARTRATE;
    int hr;
    if (e->has_user_hr) {
        hr = hr_model_steady_user(&e->hr, e->user_heartrate, e->hr_float);
    } else {
        int eq_power = running_equivalent_power(e, speed_x100_kmh);
        int target_hr = hr_model_ftp_zone(&e->hr, eq_power, e->profile.ftp, e->profile.thr_hr);
        hr = hr_model_smooth(&e->hr, target_hr);
        hr = hr_model_emit(&e->hr, hr_clamp((int)(hr + hr_model_variation(&e->hr))));
    }
    e->last_heartrate = hr;
    e->snapshot.heartrate = hr;
    return hr;
}

bool power_engine_set_broadcast_mask(power_engine_t *e, int mask)
{
    int normalized = mask & BROADCAST_FIELD_ALL;
    if (normalized == 0) return false;
    if (e->broadcast_mask == normalized) return false;
    e->broadcast_mask = normalized;
    e->dirty = true;
    return true;
}

bool power_engine_broadcast_enabled(const power_engine_t *e, int field)
{
    if (!e) return false;
    return (e->broadcast_mask & field) != 0;
}

control_target_t power_engine_button_target(const power_engine_t *e)
{
    if (!e) return CONTROL_TARGET_NONE;
    if (e->sport_profile == SPORT_PROFILE_RUNNING) {
        if (e->broadcast_mask & BROADCAST_FIELD_POWER) return CONTROL_TARGET_RUN_SPEED;
        if (e->broadcast_mask & BROADCAST_FIELD_CADENCE) return CONTROL_TARGET_RUN_CADENCE;
        if (e->broadcast_mask & BROADCAST_FIELD_HEARTRATE) return CONTROL_TARGET_HEARTRATE;
        return CONTROL_TARGET_NONE;
    }
    if (e->broadcast_mask & BROADCAST_FIELD_POWER) return CONTROL_TARGET_POWER;
    if (e->broadcast_mask & BROADCAST_FIELD_CADENCE) return CONTROL_TARGET_CADENCE;
    if (e->broadcast_mask & BROADCAST_FIELD_HEARTRATE) return CONTROL_TARGET_HEARTRATE;
    return CONTROL_TARGET_NONE;
}

int power_engine_control_value(const power_engine_t *e, control_target_t target)
{
    if (!e) return 0;
    switch (target) {
    case CONTROL_TARGET_POWER:
        return e->base_power;
    case CONTROL_TARGET_CADENCE:
        return e->cadence_base;
    case CONTROL_TARGET_HEARTRATE:
        return e->has_user_hr ? e->user_heartrate : e->last_heartrate;
    case CONTROL_TARGET_RUN_SPEED:
        return e->running.speed_x100_kmh;
    case CONTROL_TARGET_RUN_CADENCE:
        return e->running.cadence_spm;
    default:
        return 0;
    }
}

bool power_engine_set_control_value(power_engine_t *e, control_target_t target, int value)
{
    if (!e) return false;
    switch (target) {
    case CONTROL_TARGET_POWER:
        return power_engine_set_power(e, value);
    case CONTROL_TARGET_CADENCE:
        return power_engine_set_cadence(e, value);
    case CONTROL_TARGET_HEARTRATE:
        return power_engine_set_heartrate(e, value);
    case CONTROL_TARGET_RUN_SPEED:
        return power_engine_set_run_speed(e, value);
    case CONTROL_TARGET_RUN_CADENCE:
        return power_engine_set_run_cadence(e, value);
    default:
        return false;
    }
}

int power_engine_adjust_control_target(power_engine_t *e, control_target_t target, int direction)
{
    if (!e) return 0;
    int sign = direction < 0 ? -1 : 1;
    switch (target) {
    case CONTROL_TARGET_POWER:
        return power_engine_adjust_power(e, sign * e->power_step);
    case CONTROL_TARGET_CADENCE:
        return power_engine_adjust_cadence(e, sign);
    case CONTROL_TARGET_HEARTRATE: {
        int base = e->has_user_hr ? e->user_heartrate : e->last_heartrate;
        int next = clampi(base + sign, HR_MIN, HR_MAX);
        power_engine_set_heartrate(e, next);
        return e->has_user_hr ? e->user_heartrate : e->last_heartrate;
    }
    case CONTROL_TARGET_RUN_SPEED: {
        int pace_sec = e->running.speed_x100_kmh > 0
                     ? (360000 + e->running.speed_x100_kmh / 2) / e->running.speed_x100_kmh
                     : 360;
        pace_sec += direction < 0 ? 5 : -5;
        int fastest = (360000 + RUN_SPEED_X100_MAX / 2) / RUN_SPEED_X100_MAX;
        int slowest = (360000 + RUN_SPEED_X100_MIN / 2) / RUN_SPEED_X100_MIN;
        pace_sec = clampi(pace_sec, fastest, slowest);
        int next_speed = clampi((360000 + pace_sec / 2) / pace_sec, RUN_SPEED_X100_MIN, RUN_SPEED_X100_MAX);
        power_engine_set_run_speed(e, next_speed);
        return e->running.speed_x100_kmh;
    }
    case CONTROL_TARGET_RUN_CADENCE: {
        int next = clampi(e->running.cadence_spm + sign, RUN_CADENCE_MIN, RUN_CADENCE_MAX);
        power_engine_set_run_cadence(e, next);
        return e->running.cadence_spm;
    }
    default:
        return 0;
    }
}

/*
 * 设置 FTMS 下发的 ERG 目标功率。
 * 参数：e 为功率引擎对象；target_power 为目标功率。
 * 返回值：成功设置或同值幂等返回 true，功率越界返回 false。
 * 特殊行为：非法功率范围会直接忽略；成功设置后会同步更新 base_power，并在非 ERG 模式下自动切换到 ERG 模式；
 *           已处于 ERG 且目标未变时幂等返回不置 dirty，避免高频同值下发反复落盘。
 */
bool power_engine_set_erg_target(power_engine_t *e, int target_power)
{
    if (target_power < POWER_MIN || target_power > POWER_MAX) return false;
    // 同值幂等：训练 App 在 ERG 中会高频(1~2Hz)反复下发相同/缓变目标，
    // 若每次都置 dirty 会让心跳每 10s 把无意义的 ERG 目标反复写入 NVS，损耗 flash。
    // 已处于 ERG 且目标未变时直接返回，不置 dirty。
    if (e->mode == RIDE_MODE_ERG && e->has_erg_target && e->base_power == target_power) {
        return true;
    }
    ride_mode_t old_mode = e->mode;
    e->erg_active_target = target_power;
    e->has_erg_target = true;
    e->base_power = target_power;
    e->last_power = target_power;
    if (old_mode != RIDE_MODE_ERG) {
        e->mode = RIDE_MODE_ERG;
        power_engine_reset_mode_state(e);
    }
    e->dirty = true;
    return true;
}

/*
 * 清除 ERG 目标并恢复到 steady 固定功率模式。
 * 参数：e 为功率引擎对象。
 * 返回值：发生状态变化返回 true，当前未处于 ERG 且无目标时返回 false。
 * 特殊行为：保留当前 base_power（通常为最后一个 ERG 目标），让用户退出训练后从可见功率继续手动调整。
 */
bool power_engine_clear_erg_target(power_engine_t *e)
{
    bool changed = e->has_erg_target || e->erg_active_target != 0 || e->mode == RIDE_MODE_ERG;
    if (!changed) return false;
    e->has_erg_target = false;
    e->erg_active_target = 0;
    if (e->mode == RIDE_MODE_ERG) {
        e->mode = RIDE_MODE_STEADY;
        power_engine_reset_mode_state(e);
    }
    e->last_power = e->base_power;
    e->dirty = true;
    return true;
}

bool power_engine_is_dirty(power_engine_t *e)  { return e->dirty; }
void power_engine_clear_dirty(power_engine_t *e) { e->dirty = false; }
