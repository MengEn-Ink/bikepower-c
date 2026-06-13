/**
 * LED 状态指示任务（GPIO12，高电平有效）
 * 运行环境：ESP-IDF v5.5.1，独立 FreeRTOS 任务驱动闪烁
 */
#pragma once

/**
 * LED 显示模式。
 * 表示状态机希望 LED 呈现的四种基础灯效。
 * 无参数。
 * 无返回值。
 */
typedef enum {
    LED_OFF,        // 熄灭（WiFi 配网模式）
    LED_SLOW,       // 慢闪 1s（BLE 广播中，等待连接）
    LED_SOLID,      // 常亮（BLE 已连接）
    LED_FAST,       // 快闪 200ms（等待二次确认）
} led_pattern_t;

/**
 * 初始化 LED 模块。
 * 配置输出 GPIO、拉低默认电平并启动独立 LED 任务。
 * 无参数。
 * 返回值：无。
 * 特殊行为：LED 实际闪烁节拍由后台任务持续驱动，不在调用线程内阻塞执行。
 */
void led_init(void);

/**
 * 切换 LED 显示模式。
 * 更新后台 LED 任务要呈现的目标灯效。
 * pattern：目标灯效模式。
 * 返回值：无。
 * 特殊行为：仅修改共享状态，真正的 GPIO 输出会在 led_task 下一个周期生效。
 */
void led_set_pattern(led_pattern_t pattern);
