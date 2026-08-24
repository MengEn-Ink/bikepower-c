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

btn_release_action_t button_classify_release(uint32_t held_ms, uint32_t short_ms, uint32_t hold_ms);

bool button_repeat_should_adjust(uint32_t held_ms, uint32_t hold_ms);

bool button_confirm_window_expired(uint32_t elapsed_ms, uint32_t window_ms);
