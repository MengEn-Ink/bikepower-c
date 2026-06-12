/**
 * BLE 蓝牙功率计模块（NimBLE）
 * 五服务：Cycling Power + Heart Rate + CSC + FTMS + Device Information
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/** ERG 目标功率回调：FTMS 控制点收到 Set Target Power 时调用 */
typedef void (*ble_erg_cb_t)(int target_power);

/** 初始化并启动 NimBLE（注册五服务 + 广播 + 1s 通知定时器） */
void ble_bikepower_start(void);

/** 设置当前要通知的功率/心率/踏频（由领域层周期调用） */
void ble_bikepower_set_data(uint16_t power_w, uint8_t heartrate_bpm, uint16_t cadence_rpm);

/** 注册 ERG 目标功率回调 */
void ble_bikepower_set_erg_callback(ble_erg_cb_t cb);

/** 设置广播设备名（启动前调用生效） */
void ble_bikepower_set_device_name(const char *name);

/** 当前连接数（0 或 1） */
int ble_bikepower_conn_count(void);
