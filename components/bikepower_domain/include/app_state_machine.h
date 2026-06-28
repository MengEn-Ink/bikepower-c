/**
 * 应用状态机纯逻辑（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/app_state.c 抽取状态枚举和事件路由决策表，便于 host 单元测试覆盖
 * 哪些 (state, event) 组合应该被忽略、哪些应该触发动作回调。
 *
 * 设计：状态/事件以 int 传入，决策表返回 action 枚举，调用方据此执行副作用。
 *       本模块不持有任何运行时状态，纯函数。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 应用层状态（值与 main/app_state.h 的 app_state_t 一致）。
 */
typedef enum {
    ASM_ST_BOOTING = 0,
    ASM_ST_BLE_ACTIVE = 1,
    ASM_ST_WIFI_CONFIRM = 2,
    ASM_ST_WIFI_CONFIG = 3,
    ASM_ST_ERROR_RECOVERY = 4,
} asm_state_t;

/**
 * 应用层事件类型（值与 main/app_events.h 的 app_evt_type_t 一致）。
 */
typedef enum {
    ASM_EVT_CONN_CHANGED = 0,
    ASM_EVT_ERG_TARGET = 1,
    ASM_EVT_WIFI_DONE = 2,
    ASM_EVT_BTN_DOWN = 3,
    ASM_EVT_BTN_REPEAT = 4,
    ASM_EVT_BTN_UP = 5,
} asm_evt_t;

/**
 * 状态机决策结果：决定 BTN_* 事件在给定状态下应执行的动作。
 */
typedef enum {
    ASM_ACT_IGNORE = 0,        // 忽略（不属于当前状态或已在另一分支处理）
    ASM_ACT_BTN_DOWN_RECORD,   // 记录按下前基准功率（BTN_DOWN in BLE_ACTIVE）
    ASM_ACT_BTN_TRIGGER_WIFI,  // 触发 WiFi 配网（BTN_DOWN in WIFI_CONFIRM）
    ASM_ACT_BTN_REPEAT_ADJUST, // REPEAT 用于调功（BLE_ACTIVE 且未到 hold 阈值，由调用方再判阈值）
    ASM_ACT_BTN_UP_SHORT,      // 短按释放：减功
    ASM_ACT_BTN_UP_LONG,       // 长按释放：进入确认窗口
    ASM_ACT_WIFI_DONE_EXIT,    // 配网结束：退出 WiFi
} asm_action_t;

/**
 * 根据 (state, event) 决策应执行的动作。
 * state：当前状态机状态。
 * event：当前事件类型。
 * 返回值：动作枚举；ASM_ACT_IGNORE 表示该组合无动作。
 * 特殊行为：BTN_UP 的具体分类（短/长）需调用方再结合 held_ms 判定，
 *           本函数返回 ASM_ACT_IGNORE 时调用方应直接跳过；
 *           BTN_DOWN 在 WIFI_CONFIRM 下返回 ASM_ACT_BTN_TRIGGER_WIFI；
 *           BTN_DOWN 在 BLE_ACTIVE 下返回 ASM_ACT_BTN_DOWN_RECORD；
 *           BTN_DOWN 在其他状态下返回 ASM_ACT_IGNORE。
 */
asm_action_t app_state_machine_dispatch(asm_state_t state, asm_evt_t event);

/**
 * 判断给定状态是否允许调功（REPEAT/UP-SHORT）。
 * state：当前状态。
 * 返回值：true 表示 BLE_ACTIVE 状态可调功。
 */
bool app_state_machine_can_adjust_power(asm_state_t state);

/**
 * 判断 BTN_UP 是否应被忽略（已在 DOWN 处理或处于配网态）。
 * state：当前状态。
 * 返回值：true 表示应忽略释放事件。
 */
bool app_state_machine_should_ignore_btn_up(asm_state_t state);

#ifdef __cplusplus
}
#endif
