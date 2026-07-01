/**
 * 应用状态机（对齐 doc 11 §8.1 状态枚举）
 * 运行环境：ESP-IDF v5.5.4
 */
#pragma once

#include "app_events.h"
#include "power_engine.h"

/**
 * 应用状态机状态枚举。
 * 表示设备从启动、BLE 工作、WiFi 确认到异常恢复的高层运行阶段。
 * 无参数。
 * 无返回值。
 */
typedef enum {
    ST_BOOTING,
    ST_BLE_ACTIVE,      // BLE 广播/连接中（LED 由连接状态决定）
    ST_WIFI_CONFIRM,    // 长按后等待二次确认（LED 快闪）
    ST_WIFI_CONFIG,     // WiFi 配网模式（阶段5接入）
    ST_SAVING,
    ST_REBOOTING,
    ST_ERROR_RECOVERY,
} app_state_t;

/**
 * 初始化应用状态机。
 * 绑定领域引擎并把状态切到 BLE 活动态，同时根据连接状态刷新 LED。
 * engine：全局唯一的功率引擎实例。
 * 返回值：无。
 * 特殊行为：会立即把内部状态设置为 ST_BLE_ACTIVE，而不是保留 ST_BOOTING。
 */
void app_state_init(power_engine_t *engine);

/**
 * 处理一个应用事件。
 * 根据按钮或连接事件驱动调功、确认窗口和配网切换。
 * e：待处理的事件对象，不能为空。
 * 返回值：无。
 * 特殊行为：必须在主任务上下文调用，不适合 ISR 或 NimBLE 回调直接调用。
 */
void app_state_handle_event(const app_evt_t *e);

/**
 * 执行状态机周期性检查。
 * 处理确认窗口超时等依赖时间推进的逻辑。
 * 无参数。
 * 返回值：无。
 * 特殊行为：需要由主循环持续按节拍调用，否则确认窗口不会超时退出。
 */
void app_state_tick(void);

/**
 * 按当前状态和连接状态重新应用 LED 灯效。
 * 用于网页保存 LED 配置后立即刷新当前场景。
 * 无参数。
 * 返回值：无。
 */
void app_state_refresh_led(void);

/**
 * 注册进入 WiFi 配网的触发回调。
 * 在用户完成二次确认后由状态机调用该回调，交给射频控制模块执行 BLE->WiFi 切换。
 * cb：配网触发回调，可为 NULL。
 * 返回值：无。
 * 特殊行为：未注册时二次确认不会真正进配网，只会记录日志并回到 BLE 态。
 */
void app_state_set_wifi_trigger(void (*cb)(void));

/**
 * 注册退出 WiFi 配网的回调。
 * 在配网结束（保存成功或超时）后由状态机调用该回调，交给射频控制模块关闭 WiFi 资源。
 * cb：配网结束回调，可为 NULL。
 * 返回值：无。
 * 特殊行为：回调在主任务上下文执行；未注册时仅切换状态，不释放 WiFi 资源（需调用方自行清理）。
 */
void app_state_set_wifi_exit(void (*cb)(void));

/**
 * 获取当前应用状态。
 * 返回状态机当前所处的高层状态。
 * 无参数。
 * 返回值：当前 app_state_t 状态值。
 * 特殊行为：仅返回快照，不做同步等待。
 */
app_state_t app_state_get(void);

/**
 * 进入错误恢复态。
 * BLE 启动失败或其他致命但可恢复错误时调用，LED 切到快闪提示。
 * 无参数。
 * 返回值：无。
 * 特殊行为：只改变状态和 LED，不复位或重启，留给主循环做周期重试。
 */
void app_state_set_error(void);

/**
 * 从错误恢复态回到 BLE 活动态。
 * BLE 重试成功后调用，恢复正常运行的 LED 和状态。
 * 无参数。
 * 返回值：无。
 * 特殊行为：仅当当前处于 ST_ERROR_RECOVERY 时才会迁移。
 */
void app_state_clear_error(void);

#ifdef PORTAL_TEST_AUTO_CONFIG
/**
 * 测试用：自动进入 WiFi 配网（PORTAL_TEST_AUTO_CONFIG）。
 * 仅测试固件编译，正式发版不编译。等价于按钮二次确认后的触发，跳过确认窗口直接进配网。
 * 无参数。返回值：无。
 */
void app_state_enter_wifi_config_for_test(void);
#endif
