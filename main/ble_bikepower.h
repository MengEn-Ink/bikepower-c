/**
 * BLE 蓝牙功率计模块（NimBLE）
 * 五服务：Cycling Power + Heart Rate + CSC + FTMS + Device Information
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "domain_config.h"
#include "running_engine.h"

/**
 * ERG 目标功率回调类型。
 * 当 FTMS 控制点收到 Set Target Power 指令时，用于把目标功率回传给应用层。
 * target_power：App 下发的目标功率，单位瓦特（signed 16-bit）。
 * 返回值：true 表示目标功率已被接受并生效，false 表示被拒绝（范围无效或资源不足）。
 * 特殊行为：回调运行在 NimBLE host 任务上下文，不应执行阻塞操作。
 */
typedef bool (*ble_erg_cb_t)(int target_power);

/**
 * ERG 清除回调类型。
 * 当 FTMS 控制点收到 Stop/Pause、Reset 等退出训练指令时，用于通知应用层清除 ERG 状态。
 * reason：退出原因码，通常为 FTMS 操作码。
 * 返回值：无。
 * 特殊行为：回调运行在 NimBLE host 任务上下文，不应执行阻塞操作。
 */
typedef void (*ble_erg_clear_cb_t)(int reason);

/**
 * 启动 BLE 蓝牙功率计服务。
 * 初始化 NimBLE、注册五个 GATT 服务、配置设备名并开始广播和周期通知。
 * 无参数。
 * 返回值：`ESP_OK` 表示启动成功，否则表示初始化某一步失败。
 * 特殊行为：内部会创建周期通知定时器和 NimBLE host 任务；启动失败时会自动清理已分配资源。
 */
esp_err_t ble_bikepower_start(void);

/**
 * 查询 BLE 模块是否已成功启动并就绪。
 * 无参数。
 * 返回值：`true` 表示 BLE 已启动且可正常使用，`false` 表示未启动或启动失败。
 * 特殊行为：可用于在主循环中判断是否安全调用 BLE 数据更新和连接查询接口。
 */
bool ble_bikepower_is_ready(void);

/**
 * 查询 BLE 启动是否失败。
 * 无参数。
 * 返回值：`true` 表示启动过程中发生错误需要清理重试，`false` 表示正常。
 * 特殊行为：启动失败时需要由主任务调用 ble_bikepower_stop() 清理资源后再重试。
 */
bool ble_bikepower_start_failed(void);

/**
 * 停止 BLE 蓝牙功率计服务。
 * 停止通知定时器、终止连接、停止广播并释放 NimBLE 控制器资源。
 * 无参数。
 * 返回值：`true` 表示射频已成功释放，`false` 表示停用失败、射频可能仍被占用。
 * 特殊行为：进入 WiFi 配网前必须先调用，满足 WB-MUTEX-1 射频互斥约束。
 */
bool ble_bikepower_stop(void);

/**
 * 更新下一轮 BLE 通知要发送的数据。
 * 写入当前功率、心率和踏频快照，供周期通知定时器读取打包。
 * power_w：功率，单位瓦特。
 * heartrate_bpm：心率，单位 BPM。
 * cadence_rpm：踏频，单位 RPM。
 * 返回值：无。
 * 特殊行为：这里只更新共享快照，不会立即主动发送通知。
 */
void ble_bikepower_set_data(uint16_t power_w, uint8_t heartrate_bpm, uint16_t cadence_rpm);

/**
 * 设置 BLE 运动 profile。
 * profile：cycling 时注册现有 CPS/HRS/CSC/FTMS/DIS；running 时注册 RSC/HRS/DIS。
 * 返回值：无。
 * 特殊行为：建议在 ble_bikepower_start() 前调用；运行中修改需重启 BLE 才能重建 GATT 表。
 */
void ble_bikepower_set_profile(sport_profile_t profile);

/**
 * 更新跑步 profile 下一轮 BLE 通知数据。
 * snapshot：跑步引擎输出的速度、步频、步幅和距离快照。
 * heartrate_bpm：心率，单位 BPM。
 * 返回值：无。
 * 特殊行为：只更新共享变量，不立即触发通知。
 */
void ble_bikepower_set_running_data(running_snapshot_t snapshot, uint8_t heartrate_bpm);

/**
 * 注册 ERG 目标功率回调。
 * 保存上层提供的回调函数，供 FTMS 控制点写入目标功率时触发。
 * cb：回调函数指针，可为 NULL。
 * 返回值：无。
 * 特殊行为：新回调会覆盖旧回调。
 */
void ble_bikepower_set_erg_callback(ble_erg_cb_t cb);

/**
 * 注册 ERG 清除回调。
 * 保存上层提供的回调函数，供 FTMS 控制点收到 Stop/Pause、Reset 时触发。
 * cb：回调函数指针，可为 NULL。
 * 返回值：无。
 * 特殊行为：新回调会覆盖旧回调。
 */
void ble_bikepower_set_erg_clear_callback(ble_erg_clear_cb_t cb);

/**
 * 设置 BLE 广播设备名。
 * 更新内部缓存的设备名称，供 GAP 广播字段和 GAP 设备名服务使用。
 * name：新的设备名字符串。
 * 返回值：无。
 * 特殊行为：建议在启动前调用；空指针或空串会被忽略。
 */
void ble_bikepower_set_device_name(const char *name);

/**
 * 把当前缓存的设备名同步到 GAP 服务并重启广播。
 * 调用后扫描端可见新设备名，已建立的连接通过 GAP Device Name 服务读取也能拿到新名。
 * 无参数。
 * 返回值：无。
 * 特殊行为：仅 BLE 已启动时有效；已连接时只更新 GAP 服务不重启广播（避免影响连接），
 *           断连后会自动用新名重新广播。
 */
void ble_bikepower_refresh_device_name(void);

/**
 * 查询当前 BLE 连接数。
 * 返回当前是否存在活动连接。
 * 无参数。
 * 返回值：`0` 表示未连接，`1` 表示已连接。
 * 特殊行为：当前实现只支持单连接，因此返回值只会是 0 或 1。
 */
int ble_bikepower_conn_count(void);
