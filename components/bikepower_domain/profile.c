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

bool profile_set_custom_name(profile_t *p, const char *name)
{
    if (name == NULL || name[0] == '\0') return false;
    if (strncmp(p->custom_pacer_name, name, CUSTOM_PACER_NAME_MAX) == 0) return false;
    strncpy(p->custom_pacer_name, name, CUSTOM_PACER_NAME_MAX - 1);
    p->custom_pacer_name[CUSTOM_PACER_NAME_MAX - 1] = '\0';
    return true;
}

void profile_init(profile_t *p)
{
    p->weight = PROFILE_WEIGHT_DEFAULT;
    p->ftp = PROFILE_FTP_DEFAULT;
    p->thr_hr = PROFILE_THR_HR_DEFAULT;
    p->ride_type = PROFILE_RIDE_TYPE_DEFAULT;
    p->custom_wkg_low = CUSTOM_WKG_LOW_DEFAULT;
    p->custom_wkg_high = CUSTOM_WKG_HIGH_DEFAULT;
    strncpy(p->custom_pacer_name, CUSTOM_PACER_NAME_DEFAULT, CUSTOM_PACER_NAME_MAX - 1);
    p->custom_pacer_name[CUSTOM_PACER_NAME_MAX - 1] = '\0';
}

bool profile_set_weight(profile_t *p, int weight)
{
    if (weight < PROFILE_WEIGHT_MIN || weight > PROFILE_WEIGHT_MAX) return false;
    if (weight == p->weight) return false;
    p->weight = weight;
    return true;
}

bool profile_set_ftp(profile_t *p, int ftp)
{
    if (ftp < PROFILE_FTP_MIN || ftp > PROFILE_FTP_MAX) return false;
    if (ftp == p->ftp) return false;
    p->ftp = ftp;
    return true;
}

bool profile_set_thr_hr(profile_t *p, int thr_hr)
{
    if (thr_hr < PROFILE_THR_HR_MIN || thr_hr > PROFILE_THR_HR_MAX) return false;
    if (thr_hr == p->thr_hr) return false;
    p->thr_hr = thr_hr;
    return true;
}

bool profile_set_ride_type(profile_t *p, int ride_type)
{
    if (!PACER_INDEX_IS_VALID(ride_type)) return false;
    if (ride_type == p->ride_type) return false;
    p->ride_type = ride_type;
    return true;
}

int profile_ride_type_index(const char *id)
{
    if (!id) return PACER_INDEX_DEFAULT;
    for (int i = 0; i < RIDE_TYPE_COUNT; i++) {
        if (strcmp(PROFILE_RIDE_TYPE_OPTIONS[i], id) == 0) return i;
    }
    return PACER_INDEX_DEFAULT;
}

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
