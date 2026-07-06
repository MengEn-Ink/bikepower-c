/**
 * 配置 schema + 校验 + 版本迁移（纯 C，可 host 单测，不依赖 ESP-IDF）
 * 由 main/nvs_store.c 负责实际 NVS 读写，本模块只定义数据结构与校验/迁移逻辑。
 * 字段与 power_engine / profile 对应，持久化键名见 nvs_store.c。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "domain_config.h"
#include "led_pattern.h"

/*
 * 配置持久化数据结构。
 * 用途：承载 NVS 中保存的领域层参数，并作为校验与迁移的输入输出对象。
 */
typedef struct {
    int schema_ver;     // 配置格式版本
    int power;          // base_power
    int cadence;        // cadence_base
    int mode;           // ride_mode_t
    int heartrate;      // 固定用户心率（仅 has_user_hr 时有效）
    int has_user_hr;    // 是否启用固定用户心率（0/1）
    int power_float;    // 功率浮动幅度（±W）
    int cadence_float;  // 踏频浮动幅度（±RPM）
    int hr_float;       // 固定心率浮动幅度（±BPM）
    int power_step;     // 物理按钮调功步进（W）
    int weight;         // profile.weight
    int ftp;            // profile.ftp
    int thr_hr;         // profile.thr_hr
    int ride_type;      // profile.ride_type 0-5（5=自定义）
    int custom_wkg_low; // 自定义档 W/kg 下限（×10）
    int custom_wkg_high;// 自定义档 W/kg 上限（×10）
    char custom_pacer_name[CUSTOM_PACER_NAME_MAX]; // 自定义配速机器人昵称
    led_config_t led;   // LED 场景灯效配置
    int sport_profile;  // sport_profile_t
    int run_speed_x100; // 跑步速度，km/h * 100
    int run_pace_float_sec; // 跑步配速浮动，±秒/km
    int run_cadence;    // 跑步步频，steps/min
    int run_incline_x10;// 跑步坡度，% * 10
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

/**
 * 将配置对象序列化为 JSON 文本（用于导出/备份）。
 * 输出形如 {"schema_ver":N,"power":N,...,"device_name":"xxx"}。
 * c：待序列化的配置对象。
 * out：输出缓冲区。
 * out_sz：缓冲区大小。
 * 返回值：成功返回写入的字符数（不含结尾 '\0'，>0）；
 *         参数非法或缓冲区不足返回 0（不会写入截断/溢出内容）。
 * 特殊行为：device_name 按 JSON 规则转义双引号、反斜杠与控制字符（\b\f\n\r\t 及 \u00XX）。
 */
size_t config_store_serialize(const config_data_t *c, char *out, size_t out_sz);

/**
 * 从 JSON 文本反序列化为配置对象（用于导入/恢复）。
 * 先 config_store_defaults 填充，再仅覆盖 JSON 中出现的键，最后复用 validate 夹紧。
 * device_name 会做 JSON 反转义（与 serialize 配对，保证往返一致）。
 * json：输入 JSON 字符串。
 * c：输出配置对象。
 * 返回值：true 表示解析成功（即使有字段被 validate 修正）；
 *         json 或 c 为 NULL，或 JSON 缺少必备结构（无 schema_ver 键）返回 false。
 * 特殊行为：失败时 c 保持调用前内容不变（先解析到临时对象，成功才写出），不留半初始化配置。
 */
bool config_store_deserialize(const char *json, config_data_t *c);
