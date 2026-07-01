/**
 * 个人信息实现 — 见 profile.h
 */
#include <string.h>
#include "domain_config.h"
#include "profile.h"

const char *const PROFILE_RIDE_TYPE_OPTIONS[] = {
    "coco", "yumi", "jacques", "genie", "constance", "custom",
};

const road_pacer_t ROAD_PACERS[] = {
    { "Coco", 24, 28 },
    { "Yumi", 27, 31 },
    { "Jacques", 30, 35 },
    { "Genie", 35, 39 },
    { "Constance", 40, 44 },
};

static int clamp_wkg(int v)
{
    if (v < CUSTOM_WKG_MIN) return CUSTOM_WKG_MIN;
    if (v > CUSTOM_WKG_MAX) return CUSTOM_WKG_MAX;
    return v;
}

/*
 * 解析当前档位的有效 W/kg 范围。
 * 预设档（0-4）读 ROAD_PACERS；自定义档（5）读 profile.custom_wkg_*。
 */
void profile_effective_wkg(const profile_t *p, int *out_low, int *out_high)
{
    int low, high;
    if (PACER_INDEX_IS_CUSTOM(p->ride_type)) {
        low = p->custom_wkg_low;
        high = p->custom_wkg_high;
    } else {
        int idx = (p->ride_type >= 0 && p->ride_type < PACER_COUNT) ? p->ride_type : PACER_INDEX_DEFAULT;
        low = ROAD_PACERS[idx].wkg_low;
        high = ROAD_PACERS[idx].wkg_high;
    }
    if (out_low) *out_low = low;
    if (out_high) *out_high = high;
}

/*
 * 设置自定义档 W/kg 范围（×10）。
 * 越界夹紧到 [CUSTOM_WKG_MIN, CUSTOM_WKG_MAX]，并保证 low<=high。
 * 返回值：范围实际变化返回 true，否则 false。
 */
bool profile_set_custom_wkg(profile_t *p, int low, int high)
{
    low = clamp_wkg(low);
    high = clamp_wkg(high);
    if (low > high) high = low;
    if (p->custom_wkg_low == low && p->custom_wkg_high == high) return false;
    p->custom_wkg_low = low;
    p->custom_wkg_high = high;
    return true;
}

/*
 * 初始化用户档案默认值。
 * 参数：p 为待初始化的用户档案对象。
 * 返回值：无。
 * 特殊行为：会统一写入体重、FTP、阈值心率和默认配速机器人索引。
 */
void profile_init(profile_t *p)
{
    p->weight = PROFILE_WEIGHT_DEFAULT;
    p->ftp = PROFILE_FTP_DEFAULT;
    p->thr_hr = PROFILE_THR_HR_DEFAULT;
    p->ride_type = PROFILE_RIDE_TYPE_DEFAULT;
    p->custom_wkg_low = CUSTOM_WKG_LOW_DEFAULT;
    p->custom_wkg_high = CUSTOM_WKG_HIGH_DEFAULT;
}

/*
 * 设置用户体重。
 * 参数：p 为用户档案对象；weight 为目标体重。
 * 返回值：实际更新成功返回 true，否则返回 false。
 * 特殊行为：越界值和与当前值相同的输入都会被拒绝。
 */
bool profile_set_weight(profile_t *p, int weight)
{
    if (weight < PROFILE_WEIGHT_MIN || weight > PROFILE_WEIGHT_MAX) return false;
    if (weight == p->weight) return false;
    p->weight = weight;
    return true;
}

/*
 * 设置用户 FTP。
 * 参数：p 为用户档案对象；ftp 为目标 FTP。
 * 返回值：实际更新成功返回 true，否则返回 false。
 * 特殊行为：越界值和与当前值相同的输入都会被拒绝。
 */
bool profile_set_ftp(profile_t *p, int ftp)
{
    if (ftp < PROFILE_FTP_MIN || ftp > PROFILE_FTP_MAX) return false;
    if (ftp == p->ftp) return false;
    p->ftp = ftp;
    return true;
}

/*
 * 设置用户阈值心率。
 * 参数：p 为用户档案对象；thr_hr 为目标阈值心率。
 * 返回值：实际更新成功返回 true，否则返回 false。
 * 特殊行为：越界值和与当前值相同的输入都会被拒绝。
 */
bool profile_set_thr_hr(profile_t *p, int thr_hr)
{
    if (thr_hr < PROFILE_THR_HR_MIN || thr_hr > PROFILE_THR_HR_MAX) return false;
    if (thr_hr == p->thr_hr) return false;
    p->thr_hr = thr_hr;
    return true;
}

/*
 * 设置配速机器人索引。
 * 参数：p 为用户档案对象；ride_type 为目标配速机器人索引。
 * 返回值：实际更新成功返回 true，否则返回 false。
 * 特殊行为：非法索引和与当前值相同的输入都会被拒绝。
 */
bool profile_set_ride_type(profile_t *p, int ride_type)
{
    if (!PACER_INDEX_IS_VALID(ride_type)) return false;
    if (ride_type == p->ride_type) return false;
    p->ride_type = ride_type;
    return true;
}

/*
 * 把配速机器人字符串 ID 解析为索引。
 * 参数：id 为配速机器人字符串标识。
 * 返回值：匹配成功返回对应索引，未找到时返回 0。
 * 特殊行为：空指针输入会直接回退到默认索引。
 */
int profile_ride_type_index(const char *id)
{
    if (!id) return PACER_INDEX_DEFAULT;
    for (int i = 0; i < RIDE_TYPE_COUNT; i++) {
        if (strcmp(PROFILE_RIDE_TYPE_OPTIONS[i], id) == 0) return i;
    }
    return PACER_INDEX_DEFAULT;
}

/*
 * 按字符串 ID 设置配速机器人。
 * 参数：p 为用户档案对象；id 为配速机器人字符串标识。
 * 返回值：设置成功返回 true，否则返回 false。
 * 特殊行为：空字符串或未知 ID 会被拒绝；匹配成功后仍复用索引版 setter 的范围和去重逻辑。
 */
bool profile_set_ride_type_str(profile_t *p, const char *id)
{
    if (!id || !id[0]) return false;
    int idx = -1;
    for (int i = 0; i < RIDE_TYPE_COUNT; i++) {
        if (strcmp(PROFILE_RIDE_TYPE_OPTIONS[i], id) == 0) { idx = i; break; }
    }
    if (idx < 0) return false;
    return profile_set_ride_type(p, idx);
}
