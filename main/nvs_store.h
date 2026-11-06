/**
 * NVS 配置桥接（ESP-IDF 专用）
 * 把领域层 config_data_t 与 power_engine/profile 状态在 NVS（namespace "bikepower"）间读写。
 * 配置与 DIS 身份经 persistent_store 双槽快照联合保存；旧逐键布局只用于迁移读取。
 * schema 校验/迁移/默认值由领域层 config_store 负责，本模块处理旧键兼容和引擎搬运。
 */
#pragma once

#include "power_engine.h"
#include "device_identity.h"
#include <stddef.h>
#include <stdbool.h>

/**
 * 从 NVS 读取配置并注入 power_engine。
 * 负责把持久化配置加载到引擎和 profile 中。
 * e：待写入配置的功率引擎实例。
 * 返回值：无。
 * 特殊行为：NVS 缺失或版本不匹配时会使用默认值并回写，越界字段会先被校验夹紧。
 */
void nvs_store_load(power_engine_t *e);

/**
 * 把当前引擎配置写回 NVS。
 * 负责把 power_engine 和 profile 的当前状态落盘。
 * e：待保存的功率引擎实例。
 * 返回值：`true` 表示保存成功，`false` 表示保存失败。
 * 特殊行为：只负责当前快照写入，不主动清除领域层 dirty 标记。
 */
bool nvs_store_save(power_engine_t *e);
bool nvs_store_save_with_identity(power_engine_t *e, const device_identity_t *identity);

/* 最近成功设备仅用于下次启动排序，不属于配置 schema。 */
bool nvs_store_load_bridge_recent(bridge_device_t *device);
bool nvs_store_save_bridge_recent(const bridge_device_t *device);
