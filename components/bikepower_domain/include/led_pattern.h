/**
 * LED 灯效模型 + 决策纯逻辑（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/led.c 抽取，覆盖：灯效模式/颜色枚举、场景→灯效决策（四层优先级链）、
 * 各模式的亮度包络（供驱动逐帧渲染）、颜色→RGB 映射。
 *
 * 设计：阈值/亮度/周期作为参数传入，领域层不依赖任何 config 头文件。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* 灯效模式（可配置）。 */
typedef enum {
    LED_MODE_OFF = 0,     // 灭
    LED_MODE_SOLID = 1,   // 常亮
    LED_MODE_SLOW = 2,    // 慢闪
    LED_MODE_FAST = 3,    // 快闪
    LED_MODE_DOUBLE = 4,  // 双闪（闪两下后停顿循环）
    LED_MODE_BREATH = 5,  // 呼吸（渐亮渐灭，仅 RGB；通用板降级为慢闪）
    LED_MODE_COUNT = 6,
} led_mode_t;

/* 灯效颜色（仅 RGB 板生效，通用板忽略）。 */
typedef enum {
    LED_COLOR_RED = 0,
    LED_COLOR_GREEN = 1,
    LED_COLOR_BLUE = 2,
    LED_COLOR_YELLOW = 3,
    LED_COLOR_CYAN = 4,
    LED_COLOR_MAGENTA = 5,
    LED_COLOR_WHITE = 6,
    LED_COLOR_COUNT = 7,
} led_color_t;

/* 可配置的设备场景（对应状态机可区分的稳态/交互态）。 */
typedef enum {
    LED_SCENE_WAIT_CONN = 0,    // 待连接（BLE 广播中）
    LED_SCENE_CONNECTED = 1,    // 已连接
    LED_SCENE_WIFI_CONFIG = 2,  // 配网中
    LED_SCENE_WIFI_CONFIRM = 3, // 等待二次确认
    LED_SCENE_ERROR = 4,        // 异常/错误恢复
    LED_SCENE_COUNT = 5,
} led_scene_t;

/* 单场景灯效配置。 */
typedef struct {
    uint8_t mode;   // led_mode_t
    uint8_t color;  // led_color_t
} led_scene_cfg_t;

/* LED 全局配置。 */
typedef struct {
    led_scene_cfg_t scenes[LED_SCENE_COUNT];
    uint8_t silent;  // 1=真静默（除异常外全灭）
} led_config_t;

/* 决策输出的最终灯效。 */
typedef struct {
    uint8_t mode;   // led_mode_t
    uint8_t color;  // led_color_t
} led_effect_t;

/* 填充出厂默认灯效配置（待连接=蓝慢闪 / 已连接=绿常亮 / 配网=青慢闪 /
 * 确认=黄快闪 / 异常=红快闪；silent=0）。 */
void led_config_defaults(led_config_t *c);

/* 逐字段夹紧 led_config 的 mode/color/silent 到合法范围，返回是否发生修正。 */
bool led_config_validate(led_config_t *c);

/**
 * 场景→最终灯效决策（四层优先级链）：
 *   1) 异常场景强制可见：忽略 silent；mode==OFF 时回退为 FAST。
 *   2) 真静默：silent=1 时非异常场景一律 OFF。
 *   3) 用户配置：取 scenes[scene]。
 *   4) 硬件降级：is_rgb=false 时 BREATH 降级为 SLOW（颜色由驱动忽略）。
 * scene：当前场景；c：配置；is_rgb：是否 RGB 板。
 * 返回值：最终 {mode,color}。
 */
led_effect_t led_resolve(led_scene_t scene, const led_config_t *c, bool is_rgb);

/**
 * 计算某模式在给定相位的亮度包络（0-255，供驱动缩放颜色/判定亮灭）。
 * mode：灯效模式；phase_ms：模式周期内的相位（0..period）；
 * slow_ms/fast_ms：慢/快闪半周期。
 * 返回值：0-255 强度。OFF=0；SOLID=255；SLOW/FAST 方波；DOUBLE 双脉冲；BREATH 三角包络。
 */
uint8_t led_mode_scale(led_mode_t mode, uint32_t phase_ms, uint32_t slow_ms, uint32_t fast_ms);

/**
 * 返回某模式的完整循环周期（ms），驱动用 elapsed % period 得到相位。
 * OFF/SOLID 返回固定 1000（无视觉影响）。
 */
uint32_t led_mode_period_ms(led_mode_t mode, uint32_t slow_ms, uint32_t fast_ms);

/**
 * 颜色→SK6812 GRB 数组。
 * color：颜色枚举；brightness：全局亮度上限（0-0xFF）；out_rgb：输出 [G,R,B]。
 */
void led_color_to_rgb(led_color_t color, uint8_t brightness, uint8_t out_rgb[3]);
