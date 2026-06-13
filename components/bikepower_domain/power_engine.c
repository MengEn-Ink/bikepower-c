/**
 * 功率数据引擎实现 — 见 power_engine.h
 */
#include "domain_config.h"
#include "domain_rng.h"
#include "power_engine.h"
#include <string.h>

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int clamp_power(int v)   { return clampi(v, POWER_MIN, POWER_MAX); }
static int clamp_cadence(int v) { return clampi(v, CADENCE_MIN, CADENCE_MAX); }

static int clamp_tune(int v)
{
    if (v < MODE_TUNE_MIN) return MODE_TUNE_MIN;
    if (v > MODE_TUNE_MAX) return MODE_TUNE_MAX;
    return v;
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
    e->mode = DEFAULT_RIDE_MODE;
    e->mode_tuning_road = MODE_TUNE_DEFAULT;
    e->erg_active_target = 0;
    e->has_erg_target = false;
    e->last_power = DEFAULT_POWER;
    e->last_cadence = DEFAULT_CADENCE;
    profile_init(&e->profile);
    hr_model_init(&e->hr, DEFAULT_HEARTRATE);
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

static const road_pacer_t *road_pacer(power_engine_t *e)
{
    int idx = e->profile.ride_type;
    if (idx < 0 || idx >= PACER_COUNT) idx = 0;
    return &ROAD_PACERS[idx];
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
        const road_pacer_t *p = road_pacer(e);
        e->last_power = e->profile.weight * p->wkg_lo / 10;
        e->last_cadence = e->cadence_base;
    } else if (e->mode == RIDE_MODE_ERG) {
        e->last_power = e->has_erg_target ? e->erg_active_target : e->base_power;
        e->last_cadence = e->cadence_base;
    } else {
        e->last_power = e->base_power;
        e->last_cadence = e->cadence_base;
    }
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
 * 特殊行为：若已设置固定用户心率，则直接走固定心率平滑路径，不再按 FTP 分区推导目标心率。
 */
static void build_steady(power_engine_t *e, int *po, int *co, int *ho)
{
    int power = clamp_power(e->base_power + domain_rng_range(STEADY_POWER_FLUCT_LO, STEADY_POWER_FLUCT_HI));
    int cadence = clamp_cadence(e->cadence_base + domain_rng_range(STEADY_CADENCE_FLUCT_LO, STEADY_CADENCE_FLUCT_HI));
    if (e->has_user_hr) {
        *po = power; *co = cadence;
        *ho = hr_model_steady_user(&e->hr, e->user_heartrate);
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
    const road_pacer_t *p = road_pacer(e);
    int wkg = domain_rng_range(p->wkg_lo, p->wkg_hi);
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
    if (e->mode != RIDE_MODE_STEADY) return false;
    if (value < HR_MIN || value > HR_MAX) return false;
    e->user_heartrate = value;
    e->has_user_hr = true;
    e->last_heartrate = value;
    hr_model_set_user_hr(&e->hr, value);
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
    power_engine_reset_mode_state(e);
    e->dirty = true;
    return true;
}

/*
 * 设置模式调节参数。
 * 参数：e 为功率引擎对象；mode 为目标模式；value 为调节值。
 * 返回值：参数实际发生变化时返回 true，否则返回 false。
 * 特殊行为：当前仅 road 模式支持调节；输入值会先钳制到合法区间，且若当前正处于该模式会立即重置模式状态。
 */
bool power_engine_set_mode_tuning(power_engine_t *e, ride_mode_t mode, int value)
{
    if (mode != RIDE_MODE_ROAD) return false;
    value = clamp_tune(value);
    if (e->mode_tuning_road == value) return false;
    e->mode_tuning_road = value;
    if (e->mode == mode) power_engine_reset_mode_state(e);
    e->dirty = true;
    return true;
}

int power_engine_get_mode_tuning(power_engine_t *e, ride_mode_t mode)
{
    if (mode != RIDE_MODE_ROAD) return MODE_TUNE_DEFAULT;
    return clamp_tune(e->mode_tuning_road);
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

/*
 * 设置 FTMS 下发的 ERG 目标功率。
 * 参数：e 为功率引擎对象；target_power 为目标功率。
 * 返回值：无。
 * 特殊行为：非法功率范围会直接忽略；成功设置后会同步更新 base_power，并在非 ERG 模式下自动切换到 ERG 模式。
 */
void power_engine_set_erg_target(power_engine_t *e, int target_power)
{
    if (target_power < POWER_MIN || target_power > POWER_MAX) return;
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
}

bool power_engine_is_dirty(power_engine_t *e)  { return e->dirty; }
void power_engine_clear_dirty(power_engine_t *e) { e->dirty = false; }
