/**
 * 应用状态机纯逻辑实现（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 把 main/app_state.c 的事件路由 switch 抽成决策表，便于 host 单元测试。
 */
#include "app_state_machine.h"

asm_action_t app_state_machine_dispatch(asm_state_t state, asm_evt_t event)
{
    switch (event) {
    case ASM_EVT_BTN_DOWN:
        if (state == ASM_ST_WIFI_CONFIRM) return ASM_ACT_BTN_TRIGGER_WIFI;
        if (state == ASM_ST_BLE_ACTIVE) return ASM_ACT_BTN_DOWN_RECORD;
        return ASM_ACT_IGNORE;

    case ASM_EVT_BTN_REPEAT:
        // REPEAT 仅在 BLE_ACTIVE 处理，是否到 hold 阈值由调用方再判
        if (state == ASM_ST_BLE_ACTIVE) return ASM_ACT_BTN_REPEAT_ADJUST;
        return ASM_ACT_IGNORE;

    case ASM_EVT_BTN_UP:
        // 调用方需结合 state 和 held_ms 再细分（SHORT/LONG/MID）
        if (state == ASM_ST_BLE_ACTIVE) return ASM_ACT_BTN_UP_SHORT;  // 默认占位，调用方据 held_ms 决定
        return ASM_ACT_IGNORE;

    case ASM_EVT_WIFI_DONE:
        if (state == ASM_ST_WIFI_CONFIG) return ASM_ACT_WIFI_DONE_EXIT;
        return ASM_ACT_IGNORE;

    case ASM_EVT_CONN_CHANGED:
    case ASM_EVT_ERG_TARGET:
        // 这两个事件由调用方直接处理（不依赖状态分支），返回 IGNORE 表示无额外决策
        return ASM_ACT_IGNORE;

    default:
        return ASM_ACT_IGNORE;
    }
}

bool app_state_machine_can_adjust_power(asm_state_t state)
{
    return state == ASM_ST_BLE_ACTIVE;
}

bool app_state_machine_should_ignore_btn_up(asm_state_t state)
{
    return state == ASM_ST_WIFI_CONFIG || state == ASM_ST_WIFI_CONFIRM;
}
