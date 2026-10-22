/**
 * WiFi 配网门户：HTTP 配置面板（esp_http_server）
 * 运行环境：ESP-IDF v5.5.4，目标芯片 ESP32-C3
 *
 * SoftAP 启动由 radio_ctrl 统一管理；本模块只负责 HTTP 路由和页面。
 * 调用前必须已由 radio_ctrl 启动 SoftAP（v3.1.3 起为共存模式，BLE 仍在线）。
 * 路由：GET /（配置页）、POST /config（保存并实时生效）、GET /time（剩余秒数）。
 * 180s 无续期自动关闭 WiFi 回到 BLE（不重启）；配置通过 power_engine setter 实时生效到 BLE 通知。
 */
#pragma once

#include <stdbool.h>

#include "power_engine.h"
#include "esp_err.h"

typedef bool (*wifi_portal_done_callback_t)(void);

/**
 * 启动 WiFi 配网门户（HTTP 部分）。
 * 启动 HTTP 服务和超时定时器，提供浏览器配置页面。
 * engine：当前功率引擎实例，供网页读取与写入配置。
 * 返回值：ESP_OK 表示门户已就绪，否则为错误码。
 * 特殊行为：调用前必须已由 radio_ctrl 启动 SoftAP；HTTP 启动失败时返回错误码，由调用方关闭 SoftAP 回到 BLE。
 */
esp_err_t wifi_portal_start(power_engine_t *engine);

/**
 * 停止 WiFi 配网门户。
 * 停止超时检查定时器并关闭 HTTP 服务，释放 esp_http_server 资源。
 * 无参数。
 * 返回值：无。
 * 特殊行为：通常由 done 回调间接触发；SoftAP 由 radio_ctrl_exit_wifi 单独关闭；可重复调用（幂等）。
 */
void wifi_portal_stop(void);

/**
 * 注册配网结束回调。
 * 保存成功或超时后会调用 cb，由调用方把完成通知可靠交给 app_main 任务。
 * cb：配网结束回调，返回 true 表示通知已被可靠接受；返回 false 时门户保留重试能力；可为 NULL。
 * 返回值：无。
 * 特殊行为：回调在 esp_timer 任务上下文执行，应仅做非阻塞轻量操作（如投递事件或设置 pending latch）。
 */
void wifi_portal_set_done_callback(wifi_portal_done_callback_t cb);

/**
 * 消费一次 BLE 重启请求。
 * 当配网页保存了需要重建 GATT 表的配置（例如运动 profile）时返回 true。
 * 无参数。
 * 返回值：true 表示调用方应在 WiFi 关闭后重启 BLE；false 表示无需处理。
 * 特殊行为：读取后会清除内部标志，确保同一次配置只触发一次 BLE 重启。
 */
bool wifi_portal_consume_ble_restart_requested(void);

/**
 * 查询是否已有待处理 BLE 重启请求但不清除标志。
 * 无参数。
 * 返回值：true 表示运动 profile 已变化，需要主任务重启 BLE 重建 GATT 表；false 表示无需处理。
 * 特殊行为：供配网仍打开时的主循环实时处理；关闭 WiFi 路径继续使用 consume 接口保证幂等。
 */
bool wifi_portal_ble_restart_requested(void);
