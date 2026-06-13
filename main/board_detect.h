/**
 * 板型运行时自动检测
 * 通过 GPIO 上拉/下拉特征区分通用 ESP32-C3 开发板与 M5Stamp C3。
 */
#pragma once

#include "bikepower_config.h"

/**
 * 检测当前板型。
 * 通过 GPIO12 下拉输入电平判断：通用板 GPIO12 接 LED 电路（上拉/高），
 * M5Stamp C3 的 GPIO12 悬空（下拉后读低）。
 * 无参数。
 * 返回值：检测到的板型。
 */
board_type_t board_detect(void);

/**
 * 获取当前板型（检测后的缓存值）。
 * 无参数。
 * 返回值：当前板型。
 */
board_type_t board_get_type(void);

/**
 * 获取当前板型的按钮引脚。
 * 无参数。
 * 返回值：按钮 GPIO 编号。
 */
int board_btn_pin(void);

/**
 * 获取当前板型的 LED 引脚。
 * 无参数。
 * 返回值：LED GPIO 编号。
 */
int board_led_pin(void);
