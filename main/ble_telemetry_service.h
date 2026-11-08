#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "host/ble_hs.h"
#include "device_telemetry.h"

/* 启动后注册服务；host 停止前清理订阅和 RSSI callout。 */
int ble_telemetry_service_register(void);
void ble_telemetry_service_sync(void);
void ble_telemetry_service_disconnect(uint16_t connection);
void ble_telemetry_service_stop(void);
/* host 已退出后、NPL 内存池释放前调用。 */
void ble_telemetry_service_deinit(void);
void ble_telemetry_service_subscribe(uint16_t connection, uint16_t handle, bool notify);
void ble_telemetry_service_connect(uint16_t connection);
/* 应用主任务发布四组一致序号，GATT 仅复制缓存。 */
void ble_telemetry_service_publish(telemetry_snapshot_t *snapshot);
/* 调用方持有 BLE 生命周期通知锁，发送函数也必须为已持锁版本。 */
void ble_telemetry_service_notify(void (*send)(uint16_t, uint16_t, const uint8_t *, size_t));
/* 发送尝试统计：result 为 NimBLE 提交结果，不等于对端接收确认。 */
void ble_telemetry_service_tx(int result, bool allocation_failed);
void ble_telemetry_service_lock_skipped(void);
