#pragma once

#include "esp_err.h"
#include "device_identity.h"
#include "power_bridge.h"

typedef enum {
    BLE_BRIDGE_IDLE,
    BLE_BRIDGE_CONNECTING,
    BLE_BRIDGE_DISCOVERING,
    BLE_BRIDGE_SUBSCRIBING,
    BLE_BRIDGE_WAITING,
    BLE_BRIDGE_VALID,
    BLE_BRIDGE_STALE,
    /* 保留旧状态编号，恢复状态由 bridge_runtime 发布。 */
    BLE_BRIDGE_RETRYING,
    BLE_BRIDGE_STOPPING,
    BLE_BRIDGE_ERROR,
} ble_bridge_state_t;

typedef struct {
    ble_bridge_state_t state;
    bridge_device_t peer;
    bridge_snapshot_t sample;
    device_identity_t identity;
    char device_name[BRIDGE_NAME_MAX];
    bool temporary;
    bool tested;
    bool connected;
    int error;
} ble_bridge_status_t;

/* 单设备会话由 bridge_runtime 在主任务中驱动，HTTP 命令经运行时邮箱投递。 */
esp_err_t ble_power_bridge_request(const bridge_device_t *peer, bool temporary);
esp_err_t ble_power_bridge_request_stop(void);
void ble_power_bridge_tick(const bridge_config_t *calibration, uint32_t now_ms);
ble_bridge_status_t ble_power_bridge_status(const bridge_config_t *calibration, uint32_t now_ms);
/* radio 重建前取消上游；静态上下文保留到断开回调完成，不复用连接句柄。 */
void ble_power_bridge_radio_stop(void);
void ble_power_bridge_host_reset(void);
