#pragma once

#include "host/ble_gatt.h"
#include "ble_bikepower.h"

/** 通知由 BLE 生命周期模块负责；配置服务只提供待发送字节。 */
typedef void (*ble_config_indicate_cb_t)(uint16_t handle, const uint8_t *data, size_t length);

/** 初始化事务和句柄；回调在 NimBLE 启动前注册，运行期间不更换。 */
void ble_config_service_reset(ble_config_indicate_cb_t indicate,
                              ble_gatt_access_fn *identity_access);

/** 返回静态配置 GATT 表，所有运动 profile 共用。 */
const struct ble_gatt_svc_def *ble_config_service_definition(void);

/** 断连取消未提交事务；已提交的应用层落盘仍可完成。 */
void ble_config_service_disconnect(void);

/** 启动前注册 host 提交回调；只投递应用 mailbox，运行期间不更换。 */
void ble_config_service_set_commit_callback(ble_config_commit_cb_t cb);
/** 主任务发布持久配置；实际字段变化递增 revision，不并发调用。 */
void ble_config_service_set_snapshot(const power_engine_t *engine);
/** 锁内检查事务基准是否仍匹配已发布配置。 */
bool ble_config_service_revision_matches(uint32_t revision);
/** 主任务确认落盘结果；忽略不匹配事务，成功须提供已应用配置。 */
void ble_config_service_commit_result(uint32_t transaction_id, uint8_t result,
                                      uint8_t restart_flags, const config_data_t *applied);
