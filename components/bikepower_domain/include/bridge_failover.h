#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "power_bridge.h"

typedef enum {
    BRIDGE_FAILOVER_DISABLED,
    BRIDGE_FAILOVER_FAST_RECOVERY,
    BRIDGE_FAILOVER_LIVE,
    BRIDGE_FAILOVER_FALLBACK_SIMULATION,
    BRIDGE_FAILOVER_BACKGROUND_PROBE,
    BRIDGE_FAILOVER_PAUSED,
} bridge_failover_state_t;

typedef enum {
    BRIDGE_FAILOVER_START,
    BRIDGE_FAILOVER_VALID,
    BRIDGE_FAILOVER_FAILED,
    BRIDGE_FAILOVER_DISCONNECTED,
    BRIDGE_FAILOVER_TICK,
    BRIDGE_FAILOVER_PAUSE,
    BRIDGE_FAILOVER_RESUME,
    BRIDGE_FAILOVER_RETRY_NOW,
} bridge_failover_event_t;

typedef struct {
    bool connect;
    bool stop;
    bool persist_recent;
    int slot;
} bridge_failover_action_t;

typedef struct {
    bridge_failover_state_t state;
    int order[BRIDGE_DEVICE_MAX];
    int order_count;
    int order_index;
    int round;
    int active_slot;
    int probe_cursor;
    uint32_t fast_deadline_ms;
    uint32_t next_probe_ms;
    bool persist_emitted;
} bridge_failover_t;

/* 初始化为禁用状态，不发起连接或修改持久配置。 */
void bridge_failover_init(bridge_failover_t *runtime);
/*
 * 纯 C 状态转换；调用方负责执行返回动作及确认两份合法 CPS 样本。
 * session_slot 为本次会话槽位，无关联槽位时传 -1；时间使用可回绕毫秒值。
 * config 必须先经 power_bridge_config_validate 校验；recent 可为 NULL。
 */
bridge_failover_action_t bridge_failover_handle(
    bridge_failover_t *runtime, const bridge_config_t *config,
    const bridge_device_t *recent, bridge_failover_event_t event,
    int session_slot, uint32_t now_ms);
/* 禁用、暂停及降级探测阶段使用模拟输出；快速恢复阶段保持真实来源保护。 */
bool bridge_failover_uses_simulation(const bridge_failover_t *runtime);
