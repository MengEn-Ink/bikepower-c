/**
 * 配置 schema + 校验 + 版本迁移（纯 C，可 host 单测，不依赖 ESP-IDF）
 * 由 main/nvs_store.c 负责实际 NVS 读写，本模块只定义数据结构与校验/迁移逻辑。
 * 字段与 power_engine / profile 对应，持久化键名见 nvs_store.c。
 */
#pragma once

#include <stdbool.h>
#include "domain_config.h"

/*
 * 配置持久化数据结构。
 * 用途：承载 NVS 中保存的领域层参数，并作为校验与迁移的输入输出对象。
 */
typedef struct {
    int schema_ver;     // 配置格式版本
    int power;          // base_power
    int cadence;        // cadence_base
    int mode;           // ride_mode_t
    int tune_road;      // road 模式范围调节 0-100
    int weight;         // profile.weight
    int ftp;            // profile.ftp
    int thr_hr;         // profile.thr_hr
    int ride_type;      // profile.ride_type 0-4
    char device_name[DEVICE_NAME_MAX];  // BLE 广播名
} config_data_t;

/* 填充配置对象的出厂默认值。 */
void config_store_defaults(config_data_t *c);

/**
 * 逐字段范围校验并夹紧到合法区间。
 * 返回是否发生了任何修正（true 表示原始数据有越界字段）。
 */
bool config_store_validate(config_data_t *c);

/**
 * 版本迁移：把旧 schema_ver 的数据升级到当前 CONFIG_VERSION。
 * 当前无历史版本，仅在 schema_ver 不匹配时重置为默认。
 * 返回是否发生迁移。
 */
bool config_store_migrate(config_data_t *c);
