/**
 * BLE 蓝牙功率计模块（NimBLE）
 * 骑行 profile：CPS + HRS + CSC + FTMS + DIS
 * 跑步 profile：RSC + HRS + FTMS + DIS
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "domain_config.h"
#include "running_engine.h"

typedef bool (*ble_erg_cb_t)(int target_power);

typedef void (*ble_erg_clear_cb_t)(int reason);

typedef enum {
    BLE_RUN_CTRL_SPEED,
    BLE_RUN_CTRL_START,
    BLE_RUN_CTRL_STOP,
    BLE_RUN_CTRL_RESET,
} ble_run_control_type_t;

typedef bool (*ble_run_control_cb_t)(ble_run_control_type_t type, int value);

esp_err_t ble_bikepower_start(void);

bool ble_bikepower_is_ready(void);

bool ble_bikepower_start_failed(void);

bool ble_bikepower_stop(void);

void ble_bikepower_set_data(uint16_t power_w, uint8_t heartrate_bpm, uint16_t cadence_rpm);
void ble_bikepower_set_broadcast_mask(int mask);

void ble_bikepower_set_profile(sport_profile_t profile);

void ble_bikepower_set_running_data(running_snapshot_t snapshot, uint8_t heartrate_bpm);

void ble_bikepower_set_erg_callback(ble_erg_cb_t cb);

void ble_bikepower_set_erg_clear_callback(ble_erg_clear_cb_t cb);

void ble_bikepower_set_run_control_callback(ble_run_control_cb_t cb);

void ble_bikepower_set_device_name(const char *name);

void ble_bikepower_refresh_device_name(void);

int ble_bikepower_conn_count(void);
