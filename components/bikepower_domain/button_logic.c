/**
 * 按钮按压时长分类纯逻辑实现（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/app_state.c 抽取，便于 host 单元测试覆盖阈值边界。
 */
#include "button_logic.h"

btn_release_action_t button_classify_release(uint32_t held_ms, uint32_t short_ms, uint32_t hold_ms)
{
    if (held_ms < short_ms) {
        return BTN_RELEASE_SHORT;
    }
    if (held_ms >= hold_ms) {
        return BTN_RELEASE_LONG;
    }
    return BTN_RELEASE_MID;
}

bool button_repeat_should_adjust(uint32_t held_ms, uint32_t hold_ms)
{
    return held_ms < hold_ms;
}

bool button_confirm_window_expired(uint32_t elapsed_ms, uint32_t window_ms)
{
    return elapsed_ms >= window_ms;
}
