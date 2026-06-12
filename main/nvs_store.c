/**
 * NVS 配置桥接实现 — 见 nvs_store.h
 */
#include "nvs_store.h"
#include "config_store.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "NVS";

#define NVS_NS        "bikepower"
#define K_SCHEMA_VER  "sv"
#define K_POWER       "power"
#define K_CADENCE     "cadence"
#define K_MODE        "mode"
#define K_TUNE        "tune"
#define K_WEIGHT      "weight"
#define K_FTP         "ftp"
#define K_THR_HR      "thr_hr"
#define K_RIDE        "ride"

static void engine_to_config(power_engine_t *e, config_data_t *c)
{
    c->schema_ver = CONFIG_VERSION;
    c->power = e->base_power;
    c->cadence = e->cadence_base;
    c->mode = (int)e->mode;
    c->tune_road = e->mode_tuning_road;
    c->weight = e->profile.weight;
    c->ftp = e->profile.ftp;
    c->thr_hr = e->profile.thr_hr;
    c->ride_type = e->profile.ride_type;
}

static void config_to_engine(const config_data_t *c, power_engine_t *e)
{
    power_engine_set_power(e, c->power);
    power_engine_set_cadence(e, c->cadence);
    power_engine_set_mode(e, (ride_mode_t)c->mode);
    power_engine_set_mode_tuning(e, RIDE_MODE_ROAD, c->tune_road);
    profile_set_weight(&e->profile, c->weight);
    profile_set_ftp(&e->profile, c->ftp);
    profile_set_thr_hr(&e->profile, c->thr_hr);
    profile_set_ride_type(&e->profile, c->ride_type);
}

// 读单个 i32，缺失时保留默认值
static void get_i32(nvs_handle_t h, const char *key, int *out)
{
    int32_t v;
    if (nvs_get_i32(h, key, &v) == ESP_OK) {
        *out = (int)v;
    }
}

void nvs_store_load(power_engine_t *e)
{
    config_data_t c;
    config_store_defaults(&c);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "无配置，使用默认值并回写");
        config_to_engine(&c, e);
        nvs_store_save(e);
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "打开 NVS 失败(%s)，使用默认值", esp_err_to_name(err));
        config_to_engine(&c, e);
        return;
    }

    get_i32(h, K_SCHEMA_VER, &c.schema_ver);
    get_i32(h, K_POWER, &c.power);
    get_i32(h, K_CADENCE, &c.cadence);
    get_i32(h, K_MODE, &c.mode);
    get_i32(h, K_TUNE, &c.tune_road);
    get_i32(h, K_WEIGHT, &c.weight);
    get_i32(h, K_FTP, &c.ftp);
    get_i32(h, K_THR_HR, &c.thr_hr);
    get_i32(h, K_RIDE, &c.ride_type);
    nvs_close(h);

    bool migrated = config_store_migrate(&c);
    bool fixed = config_store_validate(&c);
    config_to_engine(&c, e);

    ESP_LOGI(TAG, "配置加载完成 p=%dW c=%dRPM mode=%d tune=%d w=%d ftp=%d thr=%d ride=%d%s%s",
             c.power, c.cadence, c.mode, c.tune_road, c.weight, c.ftp, c.thr_hr, c.ride_type,
             migrated ? " [migrated]" : "", fixed ? " [clamped]" : "");

    if (migrated || fixed) {
        nvs_store_save(e);
    }
}

bool nvs_store_save(power_engine_t *e)
{
    config_data_t c;
    engine_to_config(e, &c);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "保存打开 NVS 失败(%s)", esp_err_to_name(err));
        return false;
    }

    nvs_set_i32(h, K_SCHEMA_VER, c.schema_ver);
    nvs_set_i32(h, K_POWER, c.power);
    nvs_set_i32(h, K_CADENCE, c.cadence);
    nvs_set_i32(h, K_MODE, c.mode);
    nvs_set_i32(h, K_TUNE, c.tune_road);
    nvs_set_i32(h, K_WEIGHT, c.weight);
    nvs_set_i32(h, K_FTP, c.ftp);
    nvs_set_i32(h, K_THR_HR, c.thr_hr);
    nvs_set_i32(h, K_RIDE, c.ride_type);

    err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS commit 失败(%s)", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "配置已保存 p=%dW mode=%d", c.power, c.mode);
    return true;
}
