/**
 * 中断驱动按钮（GPIO9，内部上拉，按下为低电平）
 * 运行环境：ESP-IDF v5.5.1
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/** 初始化 GPIO 中断并启动 button_task，事件投递到 app_queue */
void button_init(QueueHandle_t app_queue);
