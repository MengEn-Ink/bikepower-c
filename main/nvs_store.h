/**
 * NVS 配置桥接（ESP-IDF 专用）
 * 把领域层 config_data_t 与 power_engine/profile 状态在 NVS（namespace "bikepower"）间读写。
 * schema 校验/迁移/默认值由领域层 config_store 负责，本模块只做 NVS I/O 与字段搬运。
 */
#pragma once

#include "power_engine.h"

/**
 * 从 NVS 读取配置并注入 power_engine（含 profile）。
 * NVS 缺失或版本不匹配时使用默认值并回写。校验越界字段会被夹紧。
 */
void nvs_store_load(power_engine_t *e);

/**
 * 把 power_engine（含 profile）当前状态写入 NVS。
 * 返回是否写入成功。
 */
bool nvs_store_save(power_engine_t *e);
