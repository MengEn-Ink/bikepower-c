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
 * 注册进入 WiFi 配网的触发回调。
 * 在用户完成二次确认后由状态机调用该回调，交给射频控制模块执行 BLE->WiFi 切换。
 * cb：配网触发回调，可为 NULL。
 * 返回值：无。
 * 特殊行为：未注册时二次确认不会真正进配网，只会记录日志并回到 BLE 态。
 */
void app_state_set_wifi_trigger(void (*cb)(void));

/**
 * 获取当前应用状态。
 * 返回状态机当前所处的高层状态。
 * 无参数。
 * 返回值：当前 app_state_t 状态值。
 * 特殊行为：仅返回快照，不做同步等待。
 */
app_state_t app_state_get(void);
