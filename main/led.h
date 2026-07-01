/**
 * LED 状态指示任务
 * 通用板：GPIO12 高电平有效普通 LED。
 * M5Stamp C3：GPIO2 SK6812 RGB LED（RMT 驱动），按状态映射颜色。
 * 运行环境：ESP-IDF v5.5.4，独立 FreeRTOS 任务驱动闪烁
 */
#pragma once

#include "esp_err.h"
#include "led_pattern.h"

/**
 * 初始化 LED 模块。
 * 配置输出 GPIO、拉低默认电平并启动独立 LED 任务。
 * 无参数。
 * 返回值：ESP_OK 表示初始化成功，否则为错误码。
 * 特殊行为：LED 实际闪烁节拍由后台任务持续驱动，不在调用线程内阻塞执行。
 */
esp_err_t led_init(void);

/**
 * 按场景和用户配置切换 LED 灯效。
 * scene：设备当前场景。
 * config：LED 场景配置。
 * 返回值：无。
 * 特殊行为：仅修改共享状态，真正的 GPIO 输出会在 led_task 下一个周期生效。
 */
void led_apply_scene(led_scene_t scene, const led_config_t *config);
