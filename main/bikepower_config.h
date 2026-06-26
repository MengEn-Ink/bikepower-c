/**
 * BikePower 全局配置常量
 * 由 MicroPython config.py 移植，保持 BLE UUID / 数据格式 / 阈值位级兼容。
 */
#pragma once

// ===== 板型枚举 =====
// 新增板型：在此追加枚举值（保持 BOARD_TYPE_COUNT 末位），
// 并在 board_detect.c 的 s_board_desc[] 表里补一行即可，无需改动 led/button 等模块。
typedef enum {
    BOARD_GENERIC_C3,       // 通用 ESP32-C3 开发板（GPIO12 LED, GPIO9 按钮）
    BOARD_M5STAMP_C3,       // M5Stamp C3（GPIO2 SK6812 RGB LED, GPIO3 按钮）
    BOARD_TYPE_COUNT,       // 板型总数（哨兵，必须置于末尾）
} board_type_t;

// ===== LED 驱动类型 =====
typedef enum {
    LED_DRV_GPIO,           // 普通 GPIO 高低电平 LED
    LED_DRV_SK6812,         // SK6812 RGB LED（RMT 驱动）
} led_drv_t;

// ===== 板型名称宏（用于固件包命名） =====
#define BOARD_NAME_GENERIC    "generic-c3"
#define BOARD_NAME_M5STAMP    "m5stamp-c3"

// ===== 通用 ESP32-C3 开发板引脚 =====
#define BTN_PIN_GENERIC       9      // BOOT 按钮，内置上拉，按下为低电平
#define LED_PIN_GENERIC       12     // 板载 D4 LED，高电平有效

// ===== M5Stamp C3 引脚 =====
#define BTN_PIN_M5STAMP       3      // 用户按钮 SW，内置上拉，按下为低电平
#define LED_PIN_M5STAMP       2      // SK6812 RGB LED 数据引脚

// ===== SK6812 RGB LED 配置（M5Stamp C3） =====
#define SK6812_NUM_LEDS       1      // 板载 1 颗 RGB LED
#define SK6812_T0H_NS        300     // 0 码高电平时间 ns
#define SK6812_T0L_NS        900     // 0 码低电平时间 ns
#define SK6812_T1H_NS        600     // 1 码高电平时间 ns
#define SK6812_T1L_NS        600     // 1 码低电平时间 ns
#define SK6812_RESET_US      80      // 复位低电平时间 us

// ===== BLE GATT 服务 UUID（16-bit） =====
#define POWER_SERVICE_UUID         0x1818
#define HR_SERVICE_UUID            0x180D
#define CSC_SERVICE_UUID           0x1816
#define FTMS_SERVICE_UUID          0x1826
#define DIS_SERVICE_UUID           0x180A

// ===== 特征值 UUID =====
#define POWER_CHAR_UUID            0x2A63
#define HR_CHAR_UUID               0x2A37
#define CSC_CHAR_UUID              0x2A5B
#define FTMS_FEATURE_CHAR_UUID     0x2ACC
#define FTMS_CONTROL_POINT_CHAR_UUID 0x2AD9
#define FTMS_POWER_CHAR_UUID       0x2AD2
#define FTMS_STATUS_CHAR_UUID      0x2ADA
#define DIS_MANUFACTURER_CHAR_UUID 0x2A29
#define DIS_MODEL_CHAR_UUID        0x2A24
#define DIS_FIRMWARE_CHAR_UUID     0x2A26

// ===== NVS 持久化 =====
#define NVS_NAMESPACE          "bikepower"
#define NVS_KEY_SCHEMA_VER     "sv"
#define NVS_KEY_POWER          "power"
#define NVS_KEY_CADENCE        "cadence"
#define NVS_KEY_MODE           "mode"
#define NVS_KEY_TUNE_ROAD      "tune"
#define NVS_KEY_WEIGHT         "weight"
#define NVS_KEY_FTP            "ftp"
#define NVS_KEY_THR_HR         "thr_hr"
#define NVS_KEY_RIDE_TYPE      "ride"
#define NVS_KEY_DEVICE_NAME    "devname"

// ===== BLE 连接与广播 =====
#define BLE_ADVERTISING_INTERVAL_MS  500    // 广播间隔
#define BLE_NOTIFY_INTERVAL_MS       1000   // 通知间隔
#define BLE_RETRY_TICKS              100    // BLE 启动失败重试节拍数（每拍50ms → 5s）
// 广播包名字 AD 字段最大字符数。31 字节预算 - flags(3) - 完整16位UUID列表(6) = 22 字节给名字 AD，
// 减去 AD 头(2) 得 20 个字符；超出部分完整名走 scan response。
#define BLE_ADV_NAME_MAX_LEN         20

// ===== 按钮时间阈值（毫秒） =====
#define SHORT_PRESS_MS         500
#define WIFI_BTN_HOLD_MS       2000
#define CONFIRM_WINDOW_MS      3000
#define BTN_AUTO_REPEAT_MS     200

// ===== LED 闪烁间隔（毫秒） =====
#define LED_BLINK_SLOW_MS      1000
#define LED_BLINK_FAST_MS      200
#define LED_BLINK_WIFI_MS      3000

// ===== WiFi AP 热点 =====
#define WIFI_SSID              "BikePower"
#define WIFI_PASSWORD          "12345678"
#define WIFI_AP_CHANNEL        1
#define WIFI_PORT              80
#define WIFI_SHUTDOWN_MS       180000
#define WIFI_RENEW_MS          180000

// ===== 功率/踏频/心率范围、调功步进、默认值 =====
// 单一事实源：以下领域常量统一在 domain_config.h 定义，本头直接引入避免两处 drift。
// POWER_MIN/MAX、CADENCE_MIN/MAX、HR_MIN/MAX、POWER_STEP_W、
// DEFAULT_POWER/CADENCE/HEARTRATE 均来自 domain_config.h。
#include "domain_config.h"

// ===== 设备信息 =====
#define DEVICE_NAME          "BikePower"
#define DEVICE_MANUFACTURER  "BikePower"
#define DEVICE_MODEL         "ESP32-C3"
#define FIRMWARE_VERSION     "3.1.1"

// ===== 看门狗 =====
#define WDT_TIMEOUT_MS       5000

// ===== 低内存监控（无 GC，沿用 ESP-IDF 习惯阈值） =====
#define HEAP_WARN_BYTES      40960
#define HEAP_CRIT_BYTES      20480
