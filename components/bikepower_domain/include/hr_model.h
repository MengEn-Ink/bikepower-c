/**
 * 心率动力学模型（纯 C，可 host 单测）
 * 由 MicroPython ride_modes.py 的心率相关方法移植：
 *   _ftp_zone_heartrate / _smooth_heartrate / _hr_variation /
 *   _emit_heartrate / _steady_user_heartrate
 * 模型: HR(t) = HR_old + alpha*(HR_target - HR_old), alpha = 1 - exp(-dt/tau)
 */
#pragma once

#include <stdbool.h>

typedef struct {
    // 一阶平滑累加器
    float hr_float;
    bool  hr_float_init;
    // HRV 状态
    int   hrv_tick;
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

void hr_model_init(hr_state_t *st, int initial_hr);

/** 模式切换时重置变异/漂移累加器 */
void hr_model_reset_variation(hr_state_t *st);

/** 直接设置用户心率时重置内部累加器 */
void hr_model_set_user_hr(hr_state_t *st, int value);

/** FTP 区间映射：功率 → 目标心率（已钳制） */
int hr_model_ftp_zone(hr_state_t *st, int power, int ftp, int thr_hr);

/** 一阶指数平滑逼近目标心率 */
int hr_model_smooth(hr_state_t *st, int target);

/** 合成心率变异性（每秒缓存），返回叠加量 */
float hr_model_variation(hr_state_t *st);

/** 对外台阶保持限速（死区 + 低概率跳变），返回限速后心率 */
int hr_model_emit(hr_state_t *st, int raw);

/** 固定用户心率模式下的小幅波动平滑值 */
int hr_model_steady_user(hr_state_t *st, int user_hr);

int hr_clamp(int v);
