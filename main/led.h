/**
 * LED 状态指示任务（GPIO12，高电平有效）
 * 运行环境：ESP-IDF v5.5.1，独立 FreeRTOS 任务驱动闪烁
 */
#pragma once

typedef enum {
    LED_OFF,        // 熄灭（WiFi 配网模式）
    LED_SLOW,       // 慢闪 1s（BLE 广播中，等待连接）
    LED_SOLID,      // 常亮（BLE 已连接）
    LED_FAST,       // 快闪 200ms（等待二次确认）
} led_pattern_t;

/** 初始化 GPIO 并启动 LED 任务 */
void led_init(void);

/** 切换 LED 显示模式（线程安全，任意上下文可调用） */
void led_set_pattern(led_pattern_t pattern);
