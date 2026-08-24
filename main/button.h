/**
 * 中断驱动按钮
 * 通用板：GPIO9（BOOT 按钮），M5Stamp C3：GPIO3（用户按钮 SW）。
 * 均为内部上拉，按下为低电平。
 * 运行环境：ESP-IDF v5.5.4
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

esp_err_t button_init(QueueHandle_t app_queue);
