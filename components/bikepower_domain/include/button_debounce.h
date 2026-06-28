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

/**
 * 判断去抖后是否仍为按下（消除抖动/误触）。
 * pressed_now：去抖延时后读到的当前 GPIO 电平（非 0 表示按下）。
 * 返回值：true 表示有效按下，false 表示抖动或误触应丢弃。
 * 特殊行为：纯判定，不持有时间状态。
 */
bool button_debounce_is_valid_press(bool pressed_now);

/**
 * 判断是否应触发首次或后续 REPEAT 事件。
 * held_ms：本次按住总时长。
 * last_repeat_ms：上次 REPEAT 触发时的 held_ms（首次传 0）。
 * short_ms：短按阈值（首次 REPEAT 在此之后才开始）。
 * repeat_ms：自动重复间隔。
 * 返回值：true 表示应投递 REPEAT 事件。
 * 特殊行为：边界 held==short_ms 触发首次；后续按 repeat_ms 周期触发。
 */
bool button_repeat_should_fire(uint32_t held_ms, uint32_t last_repeat_ms, uint32_t short_ms, uint32_t repeat_ms);

/**
 * 判断释放沿是否真的释放（再次去抖，避免长按中抖动被误判为释放）。
 * pressed_after_debounce：去抖延时后再次读到的电平。
 * 返回值：true 表示已释放（应发 BTN_UP），false 表示抖动应继续轮询。
 */
bool button_release_is_real(bool pressed_after_debounce);

#ifdef __cplusplus
}
#endif
