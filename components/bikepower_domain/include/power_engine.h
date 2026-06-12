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

typedef struct {
    int power;
    int cadence;
    int heartrate;
    ride_mode_t mode;
} snapshot_t;

typedef struct {
    int base_power;
    int cadence_base;
    int last_heartrate;
    int user_heartrate;       // 有效仅当 has_user_hr
    bool has_user_hr;
    ride_mode_t mode;
    int mode_tuning_road;     // road 模式范围调节 0-100
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

void power_engine_init(power_engine_t *e);

/** 生成当前快照（每个主循环 tick 调用一次，dt=50ms） */
snapshot_t power_engine_snapshot(power_engine_t *e);

/** 调功（仅 steady 模式生效），返回调整后基准功率 */
int power_engine_adjust_power(power_engine_t *e, int delta);
bool power_engine_set_power(power_engine_t *e, int value);
int power_engine_adjust_cadence(power_engine_t *e, int delta);
bool power_engine_set_cadence(power_engine_t *e, int value);
bool power_engine_set_heartrate(power_engine_t *e, int value);
bool power_engine_set_mode(power_engine_t *e, ride_mode_t mode);
bool power_engine_set_mode_tuning(power_engine_t *e, ride_mode_t mode, int value);
int power_engine_get_mode_tuning(power_engine_t *e, ride_mode_t mode);

/** 设置 BLE 设备名（截断到 DEVICE_NAME_MAX-1），返回是否变化 */
bool power_engine_set_device_name(power_engine_t *e, const char *name);

/** FTMS 控制点下发 ERG 目标功率，自动切到 ERG 模式 */
void power_engine_set_erg_target(power_engine_t *e, int target_power);

/** 模式切换后重置内部状态 */
void power_engine_reset_mode_state(power_engine_t *e);

bool power_engine_is_dirty(power_engine_t *e);
void power_engine_clear_dirty(power_engine_t *e);
