/**
 * 按钮去抖纯逻辑（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/button.c 抽取去抖判定、长按自动重复节拍判定，
 * 便于 host 单元测试覆盖去抖窗口、首次 REPEAT 触发、后续 REPEAT 间隔。
 *
 * 设计：所有时间阈值作为参数传入，不依赖任何 config 头文件。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool button_debounce_is_valid_press(bool pressed_now);

bool button_repeat_should_fire(uint32_t held_ms, uint32_t last_repeat_ms, uint32_t short_ms, uint32_t repeat_ms);

bool button_release_is_real(bool pressed_after_debounce);

#ifdef __cplusplus
}
#endif
