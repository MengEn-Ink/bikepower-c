/**
 * LED 模式映射纯逻辑（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/led.c 抽取 pattern→RGB 颜色映射和闪烁节拍判定，
 * 便于 host 单元测试覆盖 4 种灯效的颜色正确性和闪烁频率。
 *
 * 设计：阈值/亮度作为参数传入，领域层不依赖任何 config 头文件。
 *       pattern 枚举值与 main/led.h 的 led_pattern_t 保持一致（0/1/2/3）。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * LED 灯效模式（值与 main/led.h 的 led_pattern_t 一致）。
 */
typedef enum {
    LED_PATTERN_OFF = 0,    // 熄灭
    LED_PATTERN_SLOW = 1,   // 慢闪（BLE 广播中）
    LED_PATTERN_SOLID = 2,  // 常亮（BLE 已连接）
    LED_PATTERN_FAST = 3,   // 快闪（等待二次确认）
} led_pattern_logic_t;

/**
 * 根据 pattern 生成 SK6812 GRB 颜色数组。
 * pattern：灯效模式。
 * brightness：全局亮度（0-0xFF）。
 * out_rgb：输出 3 字节数组 [G, R, B]（SK6812 协议顺序）。
 * 返回值：无。
 * 特殊行为：OFF 模式输出全 0；SLOW 输出蓝色；SOLID 输出绿色；FAST 输出黄色。
 */
void led_pattern_to_rgb(led_pattern_logic_t pattern, uint8_t brightness, uint8_t out_rgb[3]);

/**
 * 判断闪烁是否应在当前节拍翻转。
 * pattern：灯效模式。
 * elapsed_ms：当前模式已累计的毫秒数。
 * slow_ms：慢闪周期（LED_BLINK_SLOW_MS）。
 * fast_ms：快闪周期（LED_BLINK_FAST_MS）。
 * 返回值：true 表示应翻转 on/off 状态。
 * 特殊行为：OFF/SOLID 模式返回 false（不闪烁）；边界 elapsed==周期 返回 true。
 */
bool led_blink_should_toggle(led_pattern_logic_t pattern, uint32_t elapsed_ms, uint32_t slow_ms, uint32_t fast_ms);

/**
 * 切换到新 pattern 时的初始 on 状态。
 * pattern：新灯效模式。
 * 返回值：1 表示初始亮，0 表示初始灭。
 * 特殊行为：SOLID 初始亮，其余初始灭。
 */
int led_pattern_initial_on(led_pattern_logic_t pattern);
