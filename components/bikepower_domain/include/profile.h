/**
 * 个人信息（体重/FTP/阈值心率/配速机器人）+ 范围校验
 * 由 MicroPython profile_manager.py 移植。
 */
#pragma once

#include <stdbool.h>
#include "domain_config.h"

/*
 * 用户档案数据。
 * 用途：保存体重、FTP、阈值心率和道路模式配速机器人选择。
 */
typedef struct {
    int weight;     // kg
    int ftp;        // W
    int thr_hr;     // BPM
    int ride_type;  // 配速机器人索引 0-5（5=自定义）
    int custom_wkg_low;   // 自定义档 W/kg 下限（×10），仅 ride_type==PACER_INDEX_CUSTOM 生效
    int custom_wkg_high;  // 自定义档 W/kg 上限（×10）
    char custom_pacer_name[CUSTOM_PACER_NAME_MAX];  // 自定义档显示昵称
} profile_t;

/* 配速机器人 ID 字符串表（含自定义档 "custom"），用于配置存储和 Web 参数映射。 */
extern const char *const PROFILE_RIDE_TYPE_OPTIONS[];

/* 解析当前档位的有效 W/kg 范围（预设读 ROAD_PACERS，自定义档读 profile）。
 * 输出 out_low/out_high（×10），任一为 NULL 时忽略。 */
void profile_effective_wkg(const profile_t *p, int *out_low, int *out_high);
/* 设置自定义档 W/kg 范围（×10），越界夹紧并保证 low<=high。 */
bool profile_set_custom_wkg(profile_t *p, int low, int high);
/* 设置自定义配速机器人昵称，空字符串拒绝，超长截断。 */
bool profile_set_custom_name(profile_t *p, const char *name);

/* 初始化用户档案到默认值。 */
void profile_init(profile_t *p);

/* 设置体重，超出范围时拒绝更新。 */
bool profile_set_weight(profile_t *p, int weight);
/* 设置 FTP，超出范围时拒绝更新。 */
bool profile_set_ftp(profile_t *p, int ftp);
/* 设置阈值心率，超出范围时拒绝更新。 */
bool profile_set_thr_hr(profile_t *p, int thr_hr);
/* 设置配速机器人索引，非法索引时拒绝更新。 */
bool profile_set_ride_type(profile_t *p, int ride_type);

/* 按字符串 ID 设置配速机器人，找不到对应项时返回 false。 */
bool profile_set_ride_type_str(profile_t *p, const char *id);

/* 把配速机器人字符串 ID 转成索引，未找到时回退到默认项。 */
int profile_ride_type_index(const char *id);
