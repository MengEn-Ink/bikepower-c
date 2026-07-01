/**
 * LED 灯效模型 + 决策纯逻辑实现 — 见 led_pattern.h
 */
#include "led_pattern.h"

/* 双闪节拍：亮 short、灭 short、亮 short、灭 long，一个周期。 */
#define DOUBLE_ON_MS    120u
#define DOUBLE_GAP_MS   180u   /* 两下之间的灭 */
#define DOUBLE_REST_MS  700u   /* 双闪后停顿 */

void led_config_defaults(led_config_t *c)
{
    c->scenes[LED_SCENE_WAIT_CONN]    = (led_scene_cfg_t){ LED_MODE_SLOW,  LED_COLOR_BLUE };
    c->scenes[LED_SCENE_CONNECTED]    = (led_scene_cfg_t){ LED_MODE_SOLID, LED_COLOR_GREEN };
    c->scenes[LED_SCENE_WIFI_CONFIG]  = (led_scene_cfg_t){ LED_MODE_SLOW,  LED_COLOR_CYAN };
    c->scenes[LED_SCENE_WIFI_CONFIRM] = (led_scene_cfg_t){ LED_MODE_FAST,  LED_COLOR_YELLOW };
    c->scenes[LED_SCENE_ERROR]        = (led_scene_cfg_t){ LED_MODE_FAST,  LED_COLOR_RED };
    c->silent = 0;
}

static uint8_t clamp_u8(uint8_t v, uint8_t hi_exclusive, uint8_t fallback)
{
    return (v < hi_exclusive) ? v : fallback;
}

bool led_config_validate(led_config_t *c)
{
    bool fixed = false;
    for (int i = 0; i < LED_SCENE_COUNT; i++) {
        uint8_t m = clamp_u8(c->scenes[i].mode, LED_MODE_COUNT, LED_MODE_SLOW);
        uint8_t col = clamp_u8(c->scenes[i].color, LED_COLOR_COUNT, LED_COLOR_BLUE);
        if (m != c->scenes[i].mode) { c->scenes[i].mode = m; fixed = true; }
        if (col != c->scenes[i].color) { c->scenes[i].color = col; fixed = true; }
    }
    if (c->silent > 1) { c->silent = 1; fixed = true; }
    return fixed;
}

led_effect_t led_resolve(led_scene_t scene, const led_config_t *c, bool is_rgb)
{
    int idx = (scene >= 0 && scene < LED_SCENE_COUNT) ? scene : LED_SCENE_WAIT_CONN;
    led_effect_t e = { c->scenes[idx].mode, c->scenes[idx].color };

    if (scene == LED_SCENE_ERROR) {
        /* 异常强制可见：忽略 silent；灭则回退快闪 */
        if (e.mode == LED_MODE_OFF) e.mode = LED_MODE_FAST;
    } else if (c->silent) {
        /* 真静默：非异常一律灭 */
        e.mode = LED_MODE_OFF;
    }

    /* 硬件降级：非 RGB 板不支持呼吸，降级为慢闪 */
    if (!is_rgb && e.mode == LED_MODE_BREATH) {
        e.mode = LED_MODE_SLOW;
    }
    return e;
}

uint32_t led_mode_period_ms(led_mode_t mode, uint32_t slow_ms, uint32_t fast_ms)
{
    switch (mode) {
    case LED_MODE_SLOW:   return slow_ms * 2u;   /* 亮 slow + 灭 slow */
    case LED_MODE_FAST:   return fast_ms * 2u;
    case LED_MODE_DOUBLE: return DOUBLE_ON_MS * 2u + DOUBLE_GAP_MS + DOUBLE_REST_MS;
    case LED_MODE_BREATH: return slow_ms * 2u;   /* 一次渐亮渐灭 */
    default:              return 1000u;          /* OFF/SOLID 无关紧要 */
    }
}

uint8_t led_mode_scale(led_mode_t mode, uint32_t phase_ms, uint32_t slow_ms, uint32_t fast_ms)
{
    switch (mode) {
    case LED_MODE_OFF:
        return 0;
    case LED_MODE_SOLID:
        return 255;
    case LED_MODE_SLOW:
        return (phase_ms < slow_ms) ? 255 : 0;      /* 前半亮后半灭 */
    case LED_MODE_FAST:
        return (phase_ms < fast_ms) ? 255 : 0;
    case LED_MODE_DOUBLE: {
        uint32_t t = phase_ms;
        if (t < DOUBLE_ON_MS) return 255;                          /* 第1下亮 */
        t -= DOUBLE_ON_MS;
        if (t < DOUBLE_GAP_MS) return 0;                           /* 间隔灭 */
        t -= DOUBLE_GAP_MS;
        if (t < DOUBLE_ON_MS) return 255;                          /* 第2下亮 */
        return 0;                                                  /* 停顿灭 */
    }
    case LED_MODE_BREATH: {
        /* 三角包络：0→255→0，半周期 slow_ms */
        uint32_t half = slow_ms ? slow_ms : 1;
        uint32_t up;
        if (phase_ms < half) {
            up = phase_ms * 255u / half;             /* 渐亮 */
        } else {
            uint32_t d = phase_ms - half;
            if (d > half) d = half;
            up = 255u - d * 255u / half;             /* 渐灭 */
        }
        return (uint8_t)up;
    }
    default:
        return 0;
    }
}

void led_color_to_rgb(led_color_t color, uint8_t brightness, uint8_t out_rgb[3])
{
    /* out_rgb 为 SK6812 GRB 顺序：[G, R, B] */
    uint8_t r = 0, g = 0, b = 0;
    switch (color) {
    case LED_COLOR_RED:     r = brightness; break;
    case LED_COLOR_GREEN:   g = brightness; break;
    case LED_COLOR_BLUE:    b = brightness; break;
    case LED_COLOR_YELLOW:  r = brightness; g = brightness; break;
    case LED_COLOR_CYAN:    g = brightness; b = brightness; break;
    case LED_COLOR_MAGENTA: r = brightness; b = brightness; break;
    case LED_COLOR_WHITE:   r = brightness; g = brightness; b = brightness; break;
    default: break;
    }
    out_rgb[0] = g;
    out_rgb[1] = r;
    out_rgb[2] = b;
}
