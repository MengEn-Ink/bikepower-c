#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "ble_power_bridge.h"
#include "bridge_failover.h"

typedef enum {
    BRIDGE_RUNTIME_SCAN_START,
    BRIDGE_RUNTIME_SCAN_STOP,
    BRIDGE_RUNTIME_TEST,
    BRIDGE_RUNTIME_STOP,
    BRIDGE_RUNTIME_RESUME,
    BRIDGE_RUNTIME_RETRY_NOW,
} bridge_runtime_command_t;

typedef struct {
    bridge_failover_state_t state;
    int active_slot;
    int candidate_slot;
    uint32_t next_attempt_ms;
    bool configured_real;
    bool actual_simulation;
    ble_bridge_status_t session;
} bridge_runtime_snapshot_t;

typedef void (*bridge_runtime_recent_callback_t)(
    const bridge_device_t *device);

/* 启动前注册；回调在应用主任务中执行，用于保存最近成功身份提示。 */
void bridge_runtime_set_recent_callback(
    bridge_runtime_recent_callback_t callback);
/* BLE 启动成功后由主任务初始化；真实骑行来源仅按已保存地址定向连接。 */
void bridge_runtime_init(const bridge_config_t *config, int sport_profile,
                         const bridge_device_t *recent, uint32_t now_ms);
/* 主任务在配置提交后调用；共享校正变更不重新启动相同设备会话。 */
void bridge_runtime_apply_config(const bridge_config_t *config,
                                 int sport_profile, uint32_t now_ms);
/*
 * 跨任务有界邮箱入口，仅复制命令；TEST 必须提供已校验的设备身份。
 * 参数无效返回 ESP_ERR_INVALID_ARG；邮箱忙返回 ESP_ERR_INVALID_STATE。
 */
esp_err_t bridge_runtime_request(bridge_runtime_command_t command,
                                 const bridge_device_t *device);
/* 以下接口仅由应用主任务调用；HTTP 读取 wifi_portal 发布的快照。 */
void bridge_runtime_tick(const bridge_config_t *config, int sport_profile,
                         uint32_t now_ms);
bridge_runtime_snapshot_t bridge_runtime_snapshot(
    const bridge_config_t *calibration, uint32_t now_ms);
bool bridge_runtime_uses_simulation(void);
