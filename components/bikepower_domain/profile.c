/**
 * 个人信息实现 — 见 profile.h
 */
#include <string.h>
#include "domain_config.h"
#include "profile.h"

const char *const PROFILE_RIDE_TYPE_OPTIONS[] = {
    "coco", "yumi", "jacques", "genie", "constance",
};

const road_pacer_t ROAD_PACERS[] = {
    { "Coco", 24, 28 },
    { "Yumi", 27, 31 },
    { "Jacques", 30, 35 },
    { "Genie", 35, 39 },
    { "Constance", 40, 44 },
};

void profile_init(profile_t *p)
{
    p->weight = PROFILE_WEIGHT_DEFAULT;
    p->ftp = PROFILE_FTP_DEFAULT;
    p->thr_hr = PROFILE_THR_HR_DEFAULT;
    p->ride_type = PROFILE_RIDE_TYPE_DEFAULT;
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
    if (ride_type < 0 || ride_type >= PACER_COUNT) return false;
    if (ride_type == p->ride_type) return false;
    p->ride_type = ride_type;
    return true;
}

int profile_ride_type_index(const char *id)
{
    if (!id) return 0;
    for (int i = 0; i < PACER_COUNT; i++) {
        if (strcmp(PROFILE_RIDE_TYPE_OPTIONS[i], id) == 0) return i;
    }
    return 0;
}

bool profile_set_ride_type_str(profile_t *p, const char *id)
{
    if (!id || !id[0]) return false;
    int idx = -1;
    for (int i = 0; i < PACER_COUNT; i++) {
        if (strcmp(PROFILE_RIDE_TYPE_OPTIONS[i], id) == 0) { idx = i; break; }
    }
    if (idx < 0) return false;
    return profile_set_ride_type(p, idx);
}
