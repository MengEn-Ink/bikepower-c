/**
 * 配置 schema 实现 — 见 config_store.h
 */
#include "config_store.h"
#include <string.h>

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void config_store_defaults(config_data_t *c)
{
    c->schema_ver = CONFIG_VERSION;
    c->power = DEFAULT_POWER;
    c->cadence = DEFAULT_CADENCE;
    c->mode = DEFAULT_RIDE_MODE;
    c->tune_road = MODE_TUNE_DEFAULT;
    c->weight = PROFILE_WEIGHT_DEFAULT;
    c->ftp = PROFILE_FTP_DEFAULT;
    c->thr_hr = PROFILE_THR_HR_DEFAULT;
    c->ride_type = PROFILE_RIDE_TYPE_DEFAULT;
    strncpy(c->device_name, DEVICE_NAME_DEFAULT, DEVICE_NAME_MAX - 1);
    c->device_name[DEVICE_NAME_MAX - 1] = '\0';
}

bool config_store_validate(config_data_t *c)
{
    config_data_t in = *c;

    c->power = clamp_int(c->power, POWER_MIN, POWER_MAX);
    c->cadence = clamp_int(c->cadence, CADENCE_MIN, CADENCE_MAX);
    if (c->mode < 0 || c->mode >= RIDE_MODE_COUNT) c->mode = DEFAULT_RIDE_MODE;
    c->tune_road = clamp_int(c->tune_road, MODE_TUNE_MIN, MODE_TUNE_MAX);
    c->weight = clamp_int(c->weight, PROFILE_WEIGHT_MIN, PROFILE_WEIGHT_MAX);
    c->ftp = clamp_int(c->ftp, PROFILE_FTP_MIN, PROFILE_FTP_MAX);
    c->thr_hr = clamp_int(c->thr_hr, PROFILE_THR_HR_MIN, PROFILE_THR_HR_MAX);
    if (c->ride_type < 0 || c->ride_type >= PACER_COUNT) c->ride_type = PROFILE_RIDE_TYPE_DEFAULT;

    c->device_name[DEVICE_NAME_MAX - 1] = '\0';
    if (c->device_name[0] == '\0') {
        strncpy(c->device_name, DEVICE_NAME_DEFAULT, DEVICE_NAME_MAX - 1);
    }

    return in.power != c->power
        || in.cadence != c->cadence
        || in.mode != c->mode
        || in.tune_road != c->tune_road
        || in.weight != c->weight
        || in.ftp != c->ftp
        || in.thr_hr != c->thr_hr
        || in.ride_type != c->ride_type
        || strncmp(in.device_name, c->device_name, DEVICE_NAME_MAX) != 0;
}

bool config_store_migrate(config_data_t *c)
{
    if (c->schema_ver == CONFIG_VERSION) {
        return false;
    }
    config_store_defaults(c);
    return true;
}
