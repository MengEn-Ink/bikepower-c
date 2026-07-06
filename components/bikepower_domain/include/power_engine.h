/**
 * 功率数据引擎（纯 C，可 host 单测）
 * 由 MicroPython power_data.py + ride_modes.py 移植。
 * 职责：按骑行模式生成功率/踏频/心率快照，支持调功/ERG/模式切换。
 * 不含持久化（由 config_store 负责），不依赖 ESP-IDF。
 */
#pragma once

#include <stdbool.h>
#include "domain_config.h"
#include "profile.h"
#include "hr_model.h"
#include "led_pattern.h"
#include "running_engine.h"

/*
 * 单次领域层输出快照。
 * 用途：向 BLE 层或上层逻辑提供一次完整的功率、踏频、心率和模式结果。
 */
typedef struct {
    // 单次快照输出的功率值（W）
    int power;
    // 单次快照输出的踏频值（RPM）
    int cadence;
    // 单次快照输出的心率值（BPM）
    int heartrate;
    // 生成该快照时使用的骑行模式
    ride_mode_t mode;
} snapshot_t;

/*
 * 有界平滑随机游走状态。
 * 用途：为固定模式下的功率/踏频提供在 [-amp, +amp] 内平稳变化的偏移量，
 *       周期性在带内重采样目标后一阶指数逼近，输出始终限制在带内（非逐拍随机）。
 */
typedef struct {
    float value;   // 当前连续偏移量（相对基准值）
    int target;    // 当前目标偏移量
    int hold;      // 目标剩余保持拍数
    bool init;     // 是否已初始化
} fluct_walk_t;

/*
 * 功率引擎运行时状态。
 * 用途：保存当前基准配置、模式状态、用户档案、心率模型和最近一次输出快照。
 */
typedef struct {
    int base_power;
    int cadence_base;
    int last_heartrate;
    int user_heartrate;       // 有效仅当 has_user_hr
    bool has_user_hr;
    int power_float;          // 功率浮动幅度（±W，仅固定模式生效）
    int cadence_float;        // 踏频浮动幅度（±RPM，仅固定模式生效）
    int hr_float;             // 固定心率浮动幅度（±BPM，仅固定模式+锁定心率生效）
    int power_step;           // 物理按钮调功步进（W），可配网修改并持久化
    fluct_walk_t power_walk;  // 功率平滑游走状态
    fluct_walk_t cadence_walk;// 踏频平滑游走状态
    ride_mode_t mode;
    int erg_active_target;    // 有效仅当 has_erg_target
    bool has_erg_target;
    int last_power;
    int last_cadence;
    profile_t profile;
    led_config_t led;
    hr_state_t hr;
    sport_profile_t sport_profile;
    running_engine_t running;
    snapshot_t snapshot;
    long snapshot_count;
    bool dirty;
    char device_name[DEVICE_NAME_MAX];
} power_engine_t;

/* 初始化功率引擎到领域层默认状态。 */
void power_engine_init(power_engine_t *e);

/* 生成当前快照（每个主循环 tick 调用一次，dt=50ms）。 */
snapshot_t power_engine_snapshot(power_engine_t *e);

/* 调整基准功率，仅 steady 模式生效，返回调整后的功率值。 */
int power_engine_adjust_power(power_engine_t *e, int delta);
/* 直接设置基准功率，仅 steady 模式生效。 */
bool power_engine_set_power(power_engine_t *e, int value);
/* 调整基准踏频，仅 steady 模式生效，返回调整后的踏频值。 */
int power_engine_adjust_cadence(power_engine_t *e, int delta);
/* 直接设置基准踏频，仅 steady 模式生效。 */
bool power_engine_set_cadence(power_engine_t *e, int value);
/* 设置 steady 模式下固定用户心率。 */
bool power_engine_set_heartrate(power_engine_t *e, int value);
/* 清除固定用户心率，恢复心率随功率联动（按 FTP 分区映射）。 */
void power_engine_clear_user_hr(power_engine_t *e);
/* 设置功率浮动幅度（±W），越界夹紧，仅影响固定模式输出。 */
bool power_engine_set_power_float(power_engine_t *e, int amp);
/* 设置踏频浮动幅度（±RPM），越界夹紧，仅影响固定模式输出。 */
bool power_engine_set_cadence_float(power_engine_t *e, int amp);
/* 设置固定心率浮动幅度（±BPM），越界夹紧，仅影响固定模式锁定心率输出。 */
bool power_engine_set_hr_float(power_engine_t *e, int amp);
/* 设置物理按钮调功步进（W），越界夹紧到 [POWER_STEP_MIN, POWER_STEP_MAX]。 */
bool power_engine_set_power_step(power_engine_t *e, int step);
/* 切换骑行模式，并在模式变化后重置内部状态。 */
bool power_engine_set_mode(power_engine_t *e, ride_mode_t mode);
/* 读取当前 profile 选择的配速机器人索引。 */
int power_engine_get_pacer_index(power_engine_t *e);
/* 设置当前 profile 的配速机器人索引。 */
bool power_engine_set_pacer_index(power_engine_t *e, int pacer_index);
/* 返回当前 profile 选择的配速机器人名称。 */
const char *power_engine_get_current_pacer(power_engine_t *e);

/* 设置 BLE 设备名，超长时截断到 DEVICE_NAME_MAX-1。 */
bool power_engine_set_device_name(power_engine_t *e, const char *name);

/* 设置当前运动 profile。 */
bool power_engine_set_sport_profile(power_engine_t *e, sport_profile_t profile);
bool power_engine_set_run_speed(power_engine_t *e, int speed_x100_kmh);
bool power_engine_set_run_pace_float(power_engine_t *e, int seconds);
bool power_engine_set_run_cadence(power_engine_t *e, int cadence_spm);
bool power_engine_set_run_cadence_float(power_engine_t *e, int amp_spm);
bool power_engine_set_run_incline(power_engine_t *e, int incline_x10);

/* 处理 FTMS 下发的 ERG 目标功率，并在需要时自动切到 ERG 模式。
 * 返回 true 表示设置成功（范围有效并已应用），false 表示超出范围被拒绝。 */
bool power_engine_set_erg_target(power_engine_t *e, int target_power);
/* 清除 ERG 目标并回到 steady 固定功率模式，供 FTMS 停止/复位/断开后恢复手动调功。 */
bool power_engine_clear_erg_target(power_engine_t *e);

/* 按当前模式重置内部功率、踏频和心率相关状态。 */
void power_engine_reset_mode_state(power_engine_t *e);

/* 返回当前配置是否存在待持久化变更。 */
bool power_engine_is_dirty(power_engine_t *e);
/* 清除待持久化脏标记。 */
void power_engine_clear_dirty(power_engine_t *e);
