/**
 * 板型描述与检测
 * 各板型的引脚、LED 驱动类型、固件包名集中在 board_detect.c 的描述表里。
 * 新增板型只需在 bikepower_config.h 追加枚举、在描述表补一行，无需改动调用方。
 */
#pragma once

#include <stddef.h>
#include "bikepower_config.h"

/**
 * 板型描述。
 * 集中一个板型的硬件差异：按钮/LED 引脚、LED 驱动类型、固件包名。
 */
typedef struct {
    int btn_pin;            // 按钮 GPIO
    int led_pin;            // LED GPIO
    led_drv_t led_drv;      // LED 驱动类型
    led_active_level_t led_active_level; // 普通 GPIO LED 的点亮电平；RGB 驱动忽略
    const char *name;       // 固件包名（如 "generic-c3"）
} board_desc_t;

board_type_t board_detect(void);

board_type_t board_get_type(void);

const board_desc_t *board_get_desc(void);

/**
 * 生成含板型、版本号、稳定版、修复版和 Git ID 的固件显示名称。
 * out：输出缓冲区。
 * out_sz：输出缓冲区大小。
 */
void board_firmware_name(char *out, size_t out_sz);

int board_btn_pin(void);

int board_led_pin(void);

led_drv_t board_led_drv(void);

led_active_level_t board_led_active_level(void);
