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
 * 功率引擎运行时状态。
 * 用途：保存当前基准配置、模式状态、用户档案、心率模型和最近一次输出快照。
 */
typedef struct {
    int base_power;
    int cadence_base;
    int last_heartrate;
    int user_heartrate;       // 有效仅当 has_user_hr
    bool has_user_hr;
    ride_mode_t mode;
    int erg_active_target;    // 有效仅当 has_erg_target
    bool has_erg_target;
    int last_power;
    int last_cadence;
    profile_t profile;
    hr_state_t hr;
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

/* 处理 FTMS 下发的 ERG 目标功率，并在需要时自动切到 ERG 模式。
 * 返回 true 表示设置成功（范围有效并已应用），false 表示超出范围被拒绝。 */
bool power_engine_set_erg_target(power_engine_t *e, int target_power);

/* 按当前模式重置内部功率、踏频和心率相关状态。 */
void power_engine_reset_mode_state(power_engine_t *e);

/* 返回当前配置是否存在待持久化变更。 */
bool power_engine_is_dirty(power_engine_t *e);
/* 清除待持久化脏标记。 */
void power_engine_clear_dirty(power_engine_t *e);
