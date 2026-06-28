/**
 * 射频状态机纯逻辑实现（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/radio_ctrl.c 抽取，便于 host 单元测试覆盖 STA 模式切换决策。
 */
#include "radio_state.h"

bool radio_state_sta_connected(uint32_t bits)
{
    return (bits & RADIO_STA_GOT_IP_BIT) != 0;
}

bool radio_state_sta_timeout(uint32_t bits)
{
    return (bits & (RADIO_STA_GOT_IP_BIT | RADIO_STA_DISCONNECTED_BIT)) == 0;
}

bool radio_state_can_switch_to_apsta(radio_mode_t current_mode)
{
    return current_mode == RADIO_MODE_AP;
}

bool radio_state_can_switch_to_ap(radio_mode_t current_mode, bool sta_disconnected)
{
    if (current_mode != RADIO_MODE_APSTA) return false;
    return sta_disconnected;
}
