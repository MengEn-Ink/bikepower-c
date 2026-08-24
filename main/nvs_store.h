/**
 * NVS 配置桥接（ESP-IDF 专用）
 * 把领域层 config_data_t 与 power_engine/profile 状态在 NVS（namespace "bikepower"）间读写。
 * schema 校验/迁移/默认值由领域层 config_store 负责，本模块只做 NVS I/O 与字段搬运。
 */
#pragma once

#include "power_engine.h"
#include <stddef.h>
#include <stdbool.h>

void nvs_store_load(power_engine_t *e);

bool nvs_store_save(power_engine_t *e);
