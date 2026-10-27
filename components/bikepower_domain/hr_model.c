/**
 * 心率动力学模型实现 — 见 hr_model.h
 */
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "domain_config.h"
#include "domain_rng.h"
#include "hr_model.h"

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int hr_clamp(int v)
{
    return clampi(v, HR_MIN, HR_MAX);
}

/*
 * 初始化心率模型全部内部状态。
 * 参数：st 为待初始化的心率状态对象；initial_hr 为初始心率。
 * 返回值：无。
 * 特殊行为：会同时初始化平滑器、HRV 缓存、发射节流状态和固定用户心率路径的初值。
 */
void hr_model_init(hr_state_t *st, int initial_hr)
{
    st->load_power = 0.0f;
    st->load_power_init = false;
    st->hr_float = (float)initial_hr;
    st->hr_float_init = true;
    st->hrv_tick = 0;
    st->hrv_sec_counter = 0;
    st->drift_bpm = 0.0f;
    st->hrv_cached = 0.0f;
    st->hrv_init = true;
    st->emit_hr = 0;
    st->emit_init = false;
    st->emit_tick = 0;
    st->user_hr_float = (float)initial_hr;
    st->user_hr_float_init = false;
    st->user_hr_target = initial_hr;
    st->user_hr_hold = 0;
    st->user_hr_target_init = false;
    st->last_clamped_hr = 0;
}

void hr_model_reset_load(hr_state_t *st, int power)
{
    st->load_power = (float)power;
    st->load_power_init = true;
}

int hr_model_smooth_load(hr_state_t *st, int power)
{
    if (!st->load_power_init) {
        hr_model_reset_load(st, power);
        return power;
    }
    float dt = (float)HR_LOOP_INTERVAL_MS / 1000.0f;
    float tau = ((float)power > st->load_power)
              ? (float)HR_LOAD_TAU_RISE : (float)HR_LOAD_TAU_FALL;
    float alpha = dt / tau;
    st->load_power += alpha * ((float)power - st->load_power);
    return (int)(st->load_power + 0.5f);
}

/*
 * 重置心率变异相关状态。
 * 参数：st 为心率状态对象。
 * 返回值：无。
 * 特殊行为：仅清空 HRV tick 和漂移累加，不重置主心率平滑器。
 */
void hr_model_reset_variation(hr_state_t *st)
{
    st->hrv_tick = 0;
    st->drift_bpm = 0.0f;
}

/*
 * 设置固定用户心率并同步更新相关缓存。
 * 参数：st 为心率状态对象；value 为新的用户心率。
 * 返回值：无。
 * 特殊行为：会立即刷新主平滑值、固定心率路径、发射输出和 HRV 累加器，避免模式切换时出现旧值拖尾。
 */
void hr_model_set_user_hr(hr_state_t *st, int value)
{
    st->hr_float = (float)value;
    st->user_hr_float = (float)value;
    st->user_hr_target = value;
    st->user_hr_hold = 0;
    st->emit_hr = value;
    st->emit_init = true;
    st->hrv_tick = 0;
    st->drift_bpm = 0.0f;
}

/*
 * 根据 FTP 分区把功率映射成目标心率。
 * 参数：st 为心率状态对象；power 为当前功率；ftp 为功能阈值功率；thr_hr 为阈值心率。
 * 返回值：返回已钳制到合法范围内的目标心率。
 * 特殊行为：当 ftp 非法时回退到经验线性公式；超过上限时会记录最近一次被钳制的原始心率值。
 */
int hr_model_ftp_zone(hr_state_t *st, int power, int ftp, int thr_hr)
{
    if (ftp <= 0) {
        return hr_clamp((int)(0.45f * power + 45.0f));
    }
    int pct = power * 100 / ftp;
    int hr_pct;
    if (pct <= 55) {
        hr_pct = HR_ZONE_RECOVERY_BASE + pct * HR_ZONE_RECOVERY_SLOPE / 55;
    } else if (pct <= 75) {
        hr_pct = 68 + (pct - 55) * 15 / 20;
    } else if (pct <= 90) {
        hr_pct = 83 + (pct - 75) * 11 / 15;
    } else if (pct <= 105) {
        hr_pct = 94 + (pct - 90) * 6 / 15;
    } else if (pct <= 120) {
        hr_pct = 100 + (pct - 105) * 6 / 15;
    } else {
        hr_pct = 106;
    }
    int base_hr = thr_hr * hr_pct / 100;
    int clamped = hr_clamp(base_hr);
    if (clamped >= HR_MAX && base_hr != st->last_clamped_hr) {
        st->last_clamped_hr = base_hr;
    }
    return clamped;
}

/*
 * 用一阶指数模型把当前心率逼近目标心率。
 * 参数：st 为心率状态对象；target 为目标心率。
 * 返回值：返回平滑后的整数心率值。
 * 特殊行为：上升和下降阶段使用不同时间常数；首次调用若未初始化会直接以目标值作为起点。
 */
int hr_model_smooth(hr_state_t *st, int target)
{
    float dt = (float)HR_LOOP_INTERVAL_MS / 1000.0f;
    if (!st->hr_float_init) {
        st->hr_float = (float)target;
        st->hr_float_init = true;
    }
    float tau = (target > st->hr_float) ? (float)HR_TAU_RISE : (float)HR_TAU_FALL;
    float alpha = 1.0f - expf(-dt / tau);
    st->hr_float = st->hr_float + alpha * ((float)target - st->hr_float);
    return (int)st->hr_float;
}

/*
 * 合成一次心率变异量。
 * 参数：st 为心率状态对象。
 * 返回值：返回当前缓存的 HRV 叠加量。
 * 特殊行为：内部按 20 个循环约 1 秒更新一次缓存，组合 RSA、Mayer 波和慢漂移，并对漂移上限做钳制。
 */
float hr_model_variation(hr_state_t *st)
{
    if (!st->hrv_init) {
        st->hrv_tick = 0;
        st->drift_bpm = 0.0f;
        st->hrv_cached = 0.0f;
        st->hrv_sec_counter = 0;
        st->hrv_init = true;
    }
    st->hrv_tick++;
    st->hrv_sec_counter++;
    if (st->hrv_sec_counter >= 20) {
        st->hrv_sec_counter = 0;
        float t = (float)st->hrv_tick * (float)HR_LOOP_INTERVAL_MS / 1000.0f;
        float rsa = HRV_RSA_AMPLITUDE * sinf(2.0f * (float)M_PI * HRV_RSA_FREQ_HZ * t);
        float mayer = HRV_MAYER_AMPLITUDE * sinf(2.0f * (float)M_PI * t / HRV_MAYER_PERIOD_S);
        float drift_per_cycle = HRV_DRIFT_RATE_BPM_PER_MIN /
            (60.0f / ((float)HR_LOOP_INTERVAL_MS / 1000.0f));
        st->drift_bpm += drift_per_cycle * 20.0f;
        if (st->drift_bpm > HRV_DRIFT_CAP_BPM) st->drift_bpm = HRV_DRIFT_CAP_BPM;
        st->hrv_cached = rsa + mayer + st->drift_bpm;
    }
    return st->hrv_cached;
}

/*
 * 对外输出心率时做台阶保持和限速。
 * 参数：st 为心率状态对象；raw 为原始心率值。
 * 返回值：返回限速后的对外心率值。
 * 特殊行为：当原始值超出死区时每次最多变化 1 BPM，并以低概率注入一次 2 BPM 的随机跳变。
 */
int hr_model_emit(hr_state_t *st, int raw)
{
    if (!st->emit_init) {
        st->emit_hr = raw;
        st->emit_init = true;
        st->emit_tick = 0;
        return raw;
    }
    int delta = raw - st->emit_hr;
    if (delta > HR_EMIT_DEADBAND) {
        st->emit_hr += 1;
    } else if (delta < -HR_EMIT_DEADBAND) {
        st->emit_hr -= 1;
    }
    int hr = st->emit_hr;
    st->emit_tick++;
    if (st->emit_tick >= 20) {
        st->emit_tick = 0;
        if (domain_rng_range(0, 9999) < HR_EMIT_JUMP_BPS) {
            int direction = domain_rng_range(0, 1) ? 1 : -1;
            hr = clampi(hr + direction * 2, HR_MIN, HR_MAX);
            st->emit_hr = hr;
        }
    }
    return hr;
}

/*
 * 在固定用户心率模式下生成带小幅波动的平滑心率。
 * 参数：st 为心率状态对象；user_hr 为用户设置的基准心率；amp 为 ± 浮动幅度（0 表示恒定）。
 * 返回值：返回钳制后的整数心率值。
 * 特殊行为：amp=0 时直接输出基准值；否则目标心率在 [base-amp, base+amp] 内周期性重采样并平滑逼近，
 *           结果始终限制在该允许波动区间内。
 */
int hr_model_steady_user(hr_state_t *st, int user_hr, int amp)
{
    int base = hr_clamp(user_hr);
    if (amp <= 0) {
        st->user_hr_float = (float)base;
        st->user_hr_float_init = true;
        st->hr_float = (float)base;
        return base;
    }
    int low = hr_clamp(base - amp);
    int high = hr_clamp(base + amp);
    if (!st->user_hr_float_init || st->user_hr_float < low || st->user_hr_float > high) {
        st->user_hr_float = (float)base;
        st->user_hr_float_init = true;
    }
    if (!st->user_hr_target_init || st->user_hr_hold <= 0) {
        st->user_hr_target = hr_clamp(base + domain_rng_range(-amp, amp));
        st->user_hr_hold = STEADY_USER_HR_TARGET_HOLD_LOOPS;
        st->user_hr_target_init = true;
    } else {
        st->user_hr_hold--;
    }
    float dt = (float)HR_LOOP_INTERVAL_MS / 1000.0f;
    float alpha = 1.0f - expf(-dt / (float)STEADY_USER_HR_TAU);
    st->user_hr_float = st->user_hr_float + alpha * ((float)st->user_hr_target - st->user_hr_float);
    if (st->user_hr_float < low) st->user_hr_float = (float)low;
    else if (st->user_hr_float > high) st->user_hr_float = (float)high;
    st->hr_float = st->user_hr_float;
    return hr_clamp((int)(st->user_hr_float + 0.5f));
}
