/**
 * LED 状态指示任务
 * 通用板：GPIO12 低电平有效普通 LED。
 * M5Stamp C3：GPIO2 SK6812 RGB LED（RMT 驱动），按状态映射颜色。
 * 运行环境：ESP-IDF v5.5.4，独立 FreeRTOS 任务驱动闪烁
 */
#pragma once

#include "esp_err.h"
#include "led_pattern.h"

esp_err_t led_init(void);

void led_apply_scene(led_scene_t scene, const led_config_t *config);
