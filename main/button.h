/**
 * 中断驱动按钮
 * 通用板：GPIO9（BOOT 按钮），M5Stamp C3：GPIO3（用户按钮 SW）。
 * 均为内部上拉，按下为低电平。
 * 运行环境：ESP-IDF v5.5.4
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * 初始化按钮模块。
 * 配置 GPIO 中断、创建唤醒信号量并启动 button_task，把语义化按钮事件投递到应用队列。
 * app_queue：应用层事件队列句柄。
 * 返回值：无。
 * 特殊行为：ISR 只做唤醒，去抖和按压时长计算都在任务上下文完成。
 */
void button_init(QueueHandle_t app_queue);
