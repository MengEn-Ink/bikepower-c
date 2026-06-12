/**
 * 个人信息（体重/FTP/阈值心率/配速机器人）+ 范围校验
 * 由 MicroPython profile_manager.py 移植。
 */
#pragma once

#include <stdbool.h>

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

/** 配速机器人 ID 字符串（用于配置/Web） */
extern const char *const PROFILE_RIDE_TYPE_OPTIONS[];
extern const road_pacer_t ROAD_PACERS[];

void profile_init(profile_t *p);

bool profile_set_weight(profile_t *p, int weight);
bool profile_set_ftp(profile_t *p, int ftp);
bool profile_set_thr_hr(profile_t *p, int thr_hr);
bool profile_set_ride_type(profile_t *p, int ride_type);

/** 按字符串 ID 设置配速机器人，返回是否变化 */
bool profile_set_ride_type_str(profile_t *p, const char *id);

/** 配速机器人 ID 字符串 → 索引，未找到返回 0 */
int profile_ride_type_index(const char *id);
