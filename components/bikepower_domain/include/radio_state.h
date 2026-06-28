/**
 * 射频状态机纯逻辑（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/radio_ctrl.c 抽取 STA/AP 模式切换决策和连接结果位判定，
 * 便于 host 单元测试覆盖 STA 连接超时/成功/断开的判定逻辑。
 *
 * 设计：纯函数，输入当前 RF 模式和事件位，输出决策结果。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * RF 模式（值与 esp_wifi_mode_t 一致）。
 */
typedef enum {
    RADIO_MODE_NONE = 0,
    RADIO_MODE_AP = 2,
    RADIO_MODE_STA = 1,
    RADIO_MODE_APSTA = 3,
} radio_mode_t;

/**
 * STA 连接事件位。
 */
#define RADIO_STA_GOT_IP_BIT       (1u << 0)
#define RADIO_STA_DISCONNECTED_BIT (1u << 1)

/**
 * 判断 STA 连接等待结果是否成功（获 IP）。
 * bits：事件组等待返回的位。
 * 返回值：true 表示已获 IP，可访问互联网。
 */
bool radio_state_sta_connected(uint32_t bits);

/**
 * 判断 STA 连接是否超时（既未获 IP 也未断连）。
 * bits：事件组等待返回的位。
 * 返回值：true 表示超时（无任何事件位）。
 */
bool radio_state_sta_timeout(uint32_t bits);

/**
 * 从 AP-only 模式切换到 APSTA 模式是否合法。
 * current_mode：当前 WiFi 模式。
 * 返回值：true 表示当前是 AP-only，可切到 APSTA。
 */
bool radio_state_can_switch_to_apsta(radio_mode_t current_mode);

/**
 * 从 APSTA 切回 AP-only 是否合法（STA 已断开）。
 * current_mode：当前 WiFi 模式。
 * sta_disconnected：STA 是否已断开。
 * 返回值：true 表示可切回 AP-only。
 */
bool radio_state_can_switch_to_ap(radio_mode_t current_mode, bool sta_disconnected);

#ifdef __cplusplus
}
#endif
