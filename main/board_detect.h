/**
 * 板型描述与检测
 * 各板型的引脚、LED 驱动类型、固件包名集中在 board_detect.c 的描述表里。
 * 新增板型只需在 bikepower_config.h 追加枚举、在描述表补一行，无需改动调用方。
 */
#pragma once

#include "bikepower_config.h"

/**
 * 板型描述。
 * 集中一个板型的硬件差异：按钮/LED 引脚、LED 驱动类型、固件包名。
 */
typedef struct {
    int btn_pin;            // 按钮 GPIO
    int led_pin;            // LED GPIO
    led_drv_t led_drv;      // LED 驱动类型
    const char *name;       // 固件包名（如 "generic-c3"）
} board_desc_t;

/**
 * 检测当前板型。
 * Kconfig 指定板型时直接采用；否则通过 GPIO12 下拉输入电平判断：
 * 通用板 GPIO12 接 LED 电路（上拉/高），M5Stamp C3 悬空（下拉后读低）。
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
 * 获取当前板型的描述。
 * 无参数。
 * 返回值：指向当前板型描述表项的只读指针。
 */
const board_desc_t *board_get_desc(void);

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

/**
 * 获取当前板型的 LED 驱动类型。
 * 无参数。
 * 返回值：LED 驱动类型枚举。
 */
led_drv_t board_led_drv(void);
