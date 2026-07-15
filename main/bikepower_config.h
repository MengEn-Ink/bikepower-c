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

// ===== 普通 GPIO LED 极性 =====
typedef enum {
    LED_ACTIVE_LOW = 0,      // GPIO 输出低电平点亮
    LED_ACTIVE_HIGH = 1,     // GPIO 输出高电平点亮
} led_active_level_t;

// ===== 板型名称宏（用于固件包命名） =====
#define BOARD_NAME_GENERIC    "generic-c3"
#define BOARD_NAME_M5STAMP    "m5stamp-c3"

// ===== 通用 ESP32-C3 开发板引脚 =====
#define BTN_PIN_GENERIC       9      // BOOT 按钮，内置上拉，按下为低电平
#define LED_PIN_GENERIC       12     // 板载 D4 LED，低电平有效

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
#define SK6812_BRIGHTNESS    0x30    // LED全局亮度（0-0xFF）
// RMT 驱动配置
#define RMT_CLK_HZ           (10 * 1000 * 1000)  // RMT时钟频率：10MHz → 100ns分辨率
#define RMT_MEM_BLOCK_SYMBOLS 64    // RMT通道内存块大小
#define RMT_TX_QUEUE_DEPTH   1       // RMT发送队列深度

// ===== BLE GATT 服务 UUID（16-bit） =====
#define POWER_SERVICE_UUID         0x1818
#define HR_SERVICE_UUID            0x180D
#define CSC_SERVICE_UUID           0x1816
#define RSC_SERVICE_UUID           0x1814
#define FTMS_SERVICE_UUID          0x1826
#define DIS_SERVICE_UUID           0x180A

// ===== 特征值 UUID =====
#define POWER_CHAR_UUID            0x2A63
#define HR_CHAR_UUID               0x2A37
#define CSC_CHAR_UUID              0x2A5B
#define RSC_MEASUREMENT_CHAR_UUID  0x2A53
#define RSC_FEATURE_CHAR_UUID      0x2A54
#define SENSOR_LOCATION_CHAR_UUID  0x2A5D
#define FTMS_FEATURE_CHAR_UUID     0x2ACC
#define FTMS_CONTROL_POINT_CHAR_UUID 0x2AD9
#define FTMS_TREADMILL_CHAR_UUID   0x2ACD
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
#define NVS_KEY_HEARTRATE      "hr"
#define NVS_KEY_HAS_USER_HR    "hashr"
#define NVS_KEY_POWER_FLOAT    "pfloat"
#define NVS_KEY_CADENCE_FLOAT  "cfloat"
#define NVS_KEY_HR_FLOAT       "hrfloat"
#define NVS_KEY_POWER_STEP     "pstep"
#define NVS_KEY_WEIGHT         "weight"
#define NVS_KEY_FTP            "ftp"
#define NVS_KEY_THR_HR         "thr_hr"
#define NVS_KEY_RIDE_TYPE      "ride"
#define NVS_KEY_CUSTOM_WKG_LO  "cwkglo"
#define NVS_KEY_CUSTOM_WKG_HI  "cwkghi"
#define NVS_KEY_CUSTOM_NAME    "cname"
#define NVS_KEY_DEVICE_NAME    "devname"
#define NVS_KEY_SPORT_PROFILE  "sport"
#define NVS_KEY_BROADCAST_MASK "bmask"
#define NVS_KEY_RUN_SPEED      "rspeed"
#define NVS_KEY_RUN_PACE_FLOAT "rpacef"
#define NVS_KEY_RUN_CADENCE    "rcad"
#define NVS_KEY_RUN_CAD_FLOAT  "rcadf"
#define NVS_KEY_RUN_MODE       "rmode"
#define NVS_KEY_RUN_PACER      "rpacer"
#define NVS_KEY_RUN_PACE_LO    "rplo"
#define NVS_KEY_RUN_PACE_HI    "rphi"
#define NVS_KEY_RUN_CUSTOM_NAME "rcname"
#define NVS_KEY_LED_SILENT     "ledsil"
#define NVS_KEY_LED_WAIT_MODE  "ledwm"
#define NVS_KEY_LED_WAIT_COLOR "ledwc"
#define NVS_KEY_LED_CONN_MODE  "ledconm"
#define NVS_KEY_LED_CONN_COLOR "ledconc"
#define NVS_KEY_LED_WIFI_MODE  "ledwifim"
#define NVS_KEY_LED_WIFI_COLOR "ledwific"
#define NVS_KEY_LED_CONFIRM_MODE  "ledcfm"
#define NVS_KEY_LED_CONFIRM_COLOR "ledcfc"
#define NVS_KEY_LED_ERROR_MODE    "lederrm"
#define NVS_KEY_LED_ERROR_COLOR   "lederrc"
#define NVS_TEMPLATE_NAMESPACE    "ble_tpl"
#define NVS_TEMPLATE_KEY_PREFIX   "tpl"

// ===== BLE 连接与广播 =====
#define BLE_ADVERTISING_INTERVAL_MS  500    // 广播间隔
#define BLE_NOTIFY_INTERVAL_MS       1000   // 通知间隔
#define BLE_RETRY_TICKS              100    // BLE 启动失败重试节拍数（每拍50ms → 5s）
#define BLE_NOTIFY_LOCK_WAIT_MS      10     // 通知互斥锁短等待上限：锁持有为微秒级，避免阻塞 NimBLE 任务
#define BLE_SCAN_DEFAULT_MS          8000   // 模板库扫描附近 BLE 设备的默认时长
#define BLE_SCAN_MAX_RESULTS         12     // 扫描结果缓存上限，避免配网态占用过多内存
#define BLE_SCAN_READ_TIMEOUT_MS     10000  // 连接外部设备读取 DIS 的最长等待时间
#define DEVICE_ANALYSIS_SAMPLE_MS    9000   // 外部运动设备短时连接采样窗口
// 广播包名字 AD 字段最大字符数。31 字节预算 - flags(3) - 完整16位UUID列表(8) = 20 字节给名字 AD，
// 减去 AD 头(2) 得 18 个字符；超出部分完整名走 scan response。
#define BLE_ADV_NAME_MAX_LEN         18

// ===== FTMS 控制点 OpCode（协议码，0x2AD9 写入/响应） =====
#define FTMS_OP_REQUEST_CONTROL   0x00
#define FTMS_OP_RESET             0x01
#define FTMS_OP_SET_TARGET_SPEED  0x02
#define FTMS_OP_SET_TARGET_POWER  0x05
#define FTMS_OP_START_RESUME      0x07
#define FTMS_OP_STOP_PAUSE        0x08
#define FTMS_OP_RESPONSE_CODE     0x80
// ===== FTMS 控制点 Result Code（响应结果） =====
#define FTMS_RESULT_SUCCESS       0x01
#define FTMS_RESULT_NOT_SUPPORTED 0x02
#define FTMS_RESULT_INVALID_PARAM 0x03
#define FTMS_CP_BUF_SIZE          20     // FTMS控制点接收缓冲区大小（字节）

// ===== 应用事件队列 =====
#define APP_EVENT_QUEUE_LEN          16     // app_main 事件队列深度
#define MAIN_HEARTBEAT_INTERVAL_TICKS 200   // 主循环心跳日志间隔：200*50ms=10s

// ===== FreeRTOS 任务配置（栈大小/优先级） =====
#define TASK_STACK_BTN          2560   // 按钮任务栈大小（字节）
#define TASK_PRIO_BTN           5      // 按钮任务调度优先级
#define TASK_STACK_LED          2048   // LED任务栈大小（字节）
#define TASK_PRIO_LED           3      // LED任务调度优先级
#define TASK_STACK_HTTPD        12288  // HTTP服务器任务栈大小（字节），BLE扫描/分析handler需大栈空间
#define TASK_PRIO_NET           5      // 网络辅助任务调度优先级

// ===== 按钮时间阈值（毫秒） =====
#define SHORT_PRESS_MS         500
#define WIFI_BTN_HOLD_MS       2000
#define CONFIRM_WINDOW_MS      3000
#define BTN_AUTO_REPEAT_MS     200
#define BTN_POLL_MS            20     // 按下保持期间的轮询间隔
#define BTN_DEBOUNCE_MS        30     // 边沿去抖时间

// ===== LED 闪烁间隔（毫秒） =====
#define LED_BLINK_SLOW_MS      1000
#define LED_BLINK_FAST_MS      200
#define LED_BLINK_WIFI_MS      3000
#define LED_TICK_MS            20     // LED 任务节拍，累加判断闪烁相位

// ===== WiFi AP 热点 =====
#define WIFI_SSID_PREFIX       "BikePower"
#define WIFI_PASSWORD          "12345678"
#define WIFI_AP_CHANNEL        1
#define WIFI_PORT              80
#define WIFI_SHUTDOWN_MS       180000
#define WIFI_RENEW_MS          180000
#define WIFI_PORTAL_CHECK_INTERVAL_US  1000000  // 配网门户超时检查周期（每秒）
#define WIFI_PORTAL_REBOOT_DELAY_SEC  5         // 保存并关闭 WiFi 前的收尾等待时间（历史命名保留）
#define WIFI_MAX_STA_CONN      2        // SoftAP最大连接客户端数
#define HTTP_MAX_BODY_LEN      4096     // HTTP请求体最大长度（字节）
#define HTTP_RECV_TIMEOUT_RETRIES 3     // httpd_req_recv 超时有限重试次数
#define HTTP_MAX_URI_HANDLERS  30       // HTTP服务器最大URI路由数（含 Captive Portal 探测端点 + 配置接口）

// ===== Captive Portal =====
#define CAPTIVE_PORTAL_HOST    "192.168.4.1"  // SoftAP 网关 IP，DNS 劫持目标 + 重定向 Location 主机
#define CAPTIVE_PORTAL_API_PATH "/.well-known/captive-portal"

// ===== 射频软切换（radio_ctrl，毫秒） =====
#define RADIO_BLE_RELEASE_WAIT_MS  200   // 停 BLE 后留给控制器释放射频的时间
#define RADIO_RESTART_DELAY_MS     1000  // 失败兜底 esp_restart 前的等待
#define BLE_STOP_LOCK_WAIT_MS      100   // 停 BLE 等待通知回调退出的最大时间
#define BLE_SCAN_LOCK_WAIT_MS      5     // BLE 扫描回调等待扫描结果锁的最大时间

// ===== 功率/踏频/心率范围、调功步进、默认值 =====
// 单一事实源：以下领域常量统一在 domain_config.h 定义，本头直接引入避免两处 drift。
// POWER_MIN/MAX、CADENCE_MIN/MAX、HR_MIN/MAX、POWER_STEP_W、
// DEFAULT_POWER/CADENCE/HEARTRATE 均来自 domain_config.h。
#include "domain_config.h"

// ===== 设备信息 =====
#define DEVICE_NAME          "BikePower"
#define DEVICE_MANUFACTURER  "BikePower"
#define DEVICE_MODEL         "ESP32-C3"
#define FIRMWARE_VERSION     "4.1.0"
#define FIRMWARE_STABLE_VERSION "4.0.0"
#define FIRMWARE_FIX_VERSION    "4.1.0"

#ifndef BIKEPOWER_GIT_SHA
#define BIKEPOWER_GIT_SHA "nogit"
#endif

#ifndef BIKEPOWER_BUILD_TIME
#define BIKEPOWER_BUILD_TIME "unknown"
#endif

// ===== 看门狗 =====
#define WDT_TIMEOUT_MS       5000

// ===== 低内存监控（无 GC，80KB 健康线 + 20KB 严重告警） =====
#define HEAP_WARN_BYTES      81920
#define HEAP_CRIT_BYTES      20480
