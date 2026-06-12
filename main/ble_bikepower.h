/**
 * BLE 蓝牙功率计模块（NimBLE）
 * 阶段1：CPS 单服务 + 广播 + 1s 固定功率通知
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/** 初始化并启动 NimBLE（注册服务 + 广播 + 1s 通知定时器） */
void ble_bikepower_start(void);

/** 设置当前要通知的功率/心率/踏频（由领域层周期调用） */
void ble_bikepower_set_data(uint16_t power_w, uint8_t heartrate_bpm, uint16_t cadence_rpm);

/** 当前连接数（0 或 1） */
int ble_bikepower_conn_count(void);
