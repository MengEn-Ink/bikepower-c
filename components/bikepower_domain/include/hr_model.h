/**
 * 心率动力学模型（纯 C，可 host 单测）
 * 由 MicroPython ride_modes.py 的心率相关方法移植：
 *   _ftp_zone_heartrate / _smooth_heartrate / _hr_variation /
 *   _emit_heartrate / _steady_user_heartrate
 * 模型: HR(t) = HR_old + alpha*(HR_target - HR_old), alpha = 1 - exp(-dt/tau)
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * 心率模型内部状态。
 * 用途：保存心率平滑、HRV 变异、对外发射节流和固定用户心率模式所需的全部运行时数据。
 */
typedef struct {
    // 一阶平滑累加器
    float hr_float;
    bool  hr_float_init;
    // HRV 状态
    uint32_t hrv_tick;   // 50ms 步进计数；无符号回绕数学上等价 2^32 个 tick 后相位整周回到 0
    int   hrv_sec_counter;
    float drift_bpm;
    float hrv_cached;
    bool  hrv_init;
    // 对外台阶保持
    int   emit_hr;
    bool  emit_init;
    int   emit_tick;
    // 固定用户心率平滑路径
    float user_hr_float;
    bool  user_hr_float_init;
    int   user_hr_target;
    int   user_hr_hold;
    bool  user_hr_target_init;
    // 钳制告警去抖
    int   last_clamped_hr;
} hr_state_t;

/*
 * 初始化心率模型状态。
 * 用途：为平滑器、HRV 累加器、对外发射状态和固定心率模式缓存设置初始值。
 */
void hr_model_init(hr_state_t *st, int initial_hr);

/* 模式切换时重置 HRV 变异与漂移累加器。 */
void hr_model_reset_variation(hr_state_t *st);

/* 直接设置固定用户心率，并同步重置相关内部累加器。 */
void hr_model_set_user_hr(hr_state_t *st, int value);

/* 按 FTP 分区把当前功率映射为目标心率，并在返回前做边界钳制。 */
int hr_model_ftp_zone(hr_state_t *st, int power, int ftp, int thr_hr);

/* 用一阶指数模型把当前心率平滑逼近目标心率。 */
int hr_model_smooth(hr_state_t *st, int target);

/* 合成心率变异量，按秒更新缓存后返回当前叠加值。 */
float hr_model_variation(hr_state_t *st);

/* 对外输出心率时做台阶保持与限速，降低过快跳变。 */
int hr_model_emit(hr_state_t *st, int raw);

/* 在固定用户心率模式下生成带小幅波动的平滑心率值。 */
int hr_model_steady_user(hr_state_t *st, int user_hr);

/* 钳制心率到领域层允许范围内。 */
int hr_clamp(int v);
