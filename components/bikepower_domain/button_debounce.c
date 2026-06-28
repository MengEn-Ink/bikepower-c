/**
 * 按钮去抖纯逻辑实现（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/button.c 抽取，便于 host 单元测试覆盖去抖和自动重复节拍。
 */
#include "button_debounce.h"

bool button_debounce_is_valid_press(bool pressed_now)
{
    return pressed_now;
}

bool button_repeat_should_fire(uint32_t held_ms, uint32_t last_repeat_ms, uint32_t short_ms, uint32_t repeat_ms)
{
    if (held_ms < short_ms) return false;
    return (held_ms - last_repeat_ms) >= repeat_ms;
}

bool button_release_is_real(bool pressed_after_debounce)
{
    return !pressed_after_debounce;
}
