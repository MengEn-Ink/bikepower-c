/**
 * 按钮按压时长分类纯逻辑（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/app_state.c 抽取按压时长判定逻辑，便于 host 单元测试覆盖
 * 短按/长按调功/长按确认的阈值边界，防止阈值错误导致误触发配网或调功反向。
 *
 * 设计：阈值作为参数传入，领域层不依赖任何 config 头文件，
 *       调用方（app_state.c）传入 bikepower_config.h 的常量。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * 按钮释放后的动作分类。
 * 表示根据按压时长判定的释放语义。
 */
typedef enum {
    BTN_RELEASE_SHORT = 0,   // 短按释放（held < short_ms）→ 减功
    BTN_RELEASE_MID,         // 中间区间释放（short_ms <= held < hold_ms）→ 无动作（长按调功已在 REPEAT 应用）
    BTN_RELEASE_LONG,        // 长按释放（held >= hold_ms）→ 进入 WiFi 二次确认窗口
} btn_release_action_t;

/**
 * 根据按压时长分类释放动作。
 * held_ms：本次按住的总时长（毫秒）。
 * short_ms：短按阈值（SHORT_PRESS_MS）。
 * hold_ms：长按确认阈值（WIFI_BTN_HOLD_MS）。
 * 返回值：释放动作分类。
 * 特殊行为：边界值 held==short_ms 归为 MID（不触发减功），held==hold_ms 归为 LONG（触发确认窗口）。
 */
btn_release_action_t button_classify_release(uint32_t held_ms, uint32_t short_ms, uint32_t hold_ms);

/**
 * 判断 REPEAT 事件是否应执行调功。
 * 仅在未达到长按确认阈值时调功，达到阈值后停止调功（进入待确认状态）。
 * held_ms：当前按住时长（毫秒）。
 * hold_ms：长按确认阈值（WIFI_BTN_HOLD_MS）。
 * 返回值：true 表示应执行 +POWER_STEP_W 调功，false 表示已达确认阈值停止调功。
 * 特殊行为：held_ms == hold_ms 时返回 false（刚进入确认区间，停止调功）。
 */
bool button_repeat_should_adjust(uint32_t held_ms, uint32_t hold_ms);

/**
 * 判断 WiFi 二次确认窗口是否超时。
 * elapsed_ms：确认窗口已流逝时间（毫秒）。
 * window_ms：确认窗口时长（CONFIRM_WINDOW_MS）。
 * 返回值：true 表示已超时应取消回到 BLE 活动态，false 表示仍在窗口内。
 * 特殊行为：elapsed_ms == window_ms 时返回 true（达到超时）。
 */
bool button_confirm_window_expired(uint32_t elapsed_ms, uint32_t window_ms);
