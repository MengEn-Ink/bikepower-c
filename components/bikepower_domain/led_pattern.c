/**
 * LED 模式映射纯逻辑实现（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/led.c 抽取，便于 host 单元测试覆盖颜色映射和闪烁节拍。
 */
#include "led_pattern.h"

void led_pattern_to_rgb(led_pattern_logic_t pattern, uint8_t brightness, uint8_t out_rgb[3])
{
    switch (pattern) {
    case LED_PATTERN_OFF:
        out_rgb[0] = 0; out_rgb[1] = 0; out_rgb[2] = 0;           // 灭
        break;
    case LED_PATTERN_SLOW:
        out_rgb[0] = 0; out_rgb[1] = 0; out_rgb[2] = brightness;  // 蓝色（BLE 广播中）
        break;
    case LED_PATTERN_SOLID:
        out_rgb[0] = brightness; out_rgb[1] = 0; out_rgb[2] = 0;  // 绿色（BLE 已连接）
        break;
    case LED_PATTERN_FAST:
        out_rgb[0] = brightness; out_rgb[1] = brightness; out_rgb[2] = 0;  // 黄色（等待确认）
        break;
    default:
        out_rgb[0] = 0; out_rgb[1] = 0; out_rgb[2] = 0;
        break;
    }
}

bool led_blink_should_toggle(led_pattern_logic_t pattern, uint32_t elapsed_ms, uint32_t slow_ms, uint32_t fast_ms)
{
    switch (pattern) {
    case LED_PATTERN_SLOW:
        return elapsed_ms >= slow_ms;
    case LED_PATTERN_FAST:
        return elapsed_ms >= fast_ms;
    default:
        return false;
    }
}

int led_pattern_initial_on(led_pattern_logic_t pattern)
{
    return pattern == LED_PATTERN_SOLID ? 1 : 0;
}
