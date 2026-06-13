/**
 * 个人信息（体重/FTP/阈值心率/配速机器人）+ 范围校验
 * 由 MicroPython profile_manager.py 移植。
 */
#pragma once

#include <stdbool.h>

/*
 * 用户档案数据。
 * 用途：保存体重、FTP、阈值心率和道路模式配速机器人选择。
 */
typedef struct {
    int weight;     // kg
    int ftp;        // W
    int thr_hr;     // BPM
    int ride_type;  // 配速机器人索引 0-4
} profile_t;

/** 配速机器人：名称 + 功体比下限/上限（W/kg*10） */
typedef struct {
    const char *name;
    int wkg_lo;
    int wkg_hi;
} road_pacer_t;

/* 配速机器人 ID 字符串表，用于配置存储和 Web 参数映射。 */
extern const char *const PROFILE_RIDE_TYPE_OPTIONS[];
/* 配速机器人参数表，给 road 模式生成功体比范围。 */
extern const road_pacer_t ROAD_PACERS[];

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
