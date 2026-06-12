/**
 * BikePower 全局配置常量
 * 由 MicroPython config.py 移植，保持 BLE UUID / 数据格式 / 阈值位级兼容。
 */
#pragma once

// ===== 硬件引脚 =====
#define BTN_PIN              9      // BOOT 按钮，内置上拉，按下为低电平
#define LED_PIN              12     // 板载 D4 LED，高电平有效

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

// ===== BLE 连接与广播 =====
#define BLE_ADVERTISING_INTERVAL_MS  500    // 广播间隔
#define BLE_NOTIFY_INTERVAL_MS       1000   // 通知间隔

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

// ===== 功率/踏频/心率范围 =====
#define POWER_MIN             0
#define POWER_MAX             2000
#define CADENCE_MIN          20
#define CADENCE_MAX         120
#define HR_MIN               60
#define HR_MAX              200

// ===== 调功步进 =====
#define POWER_STEP_W         20     // 短按 -20W / 长按连续 +20W

// ===== 默认值 =====
#define DEFAULT_POWER       260
#define DEFAULT_CADENCE      90
#define DEFAULT_HEARTRATE   150

// ===== 设备信息 =====
#define DEVICE_NAME          "BikePower"
#define DEVICE_MANUFACTURER  "BikePower"
#define DEVICE_MODEL         "ESP32-C3"
#define FIRMWARE_VERSION     "3.0.0"

// ===== 看门狗 =====
#define WDT_TIMEOUT_MS       5000
