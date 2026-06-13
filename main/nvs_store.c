/**
 * NVS 配置桥接实现 — 见 nvs_store.h
 */
#include "nvs_store.h"
#include "config_store.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

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
#define K_DEVNAME     "devname"

/**
 * 把运行时引擎状态拷贝成可持久化配置结构。
 * 从 power_engine 与 profile 提取需要保存到 NVS 的字段。
 * e：源功率引擎实例。
 * c：目标配置结构。
 * 返回值：无。
 * 特殊行为：设备名会做长度截断并确保字符串结尾为 `\0`。
 */
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
    strncpy(c->device_name, e->device_name, DEVICE_NAME_MAX - 1);
    c->device_name[DEVICE_NAME_MAX - 1] = '\0';
}

/**
 * 把配置结构应用回运行时引擎。
 * 将 config_data_t 中的持久化字段逐项写回 power_engine 和 profile。
 * c：源配置结构。
 * e：目标功率引擎实例。
 * 返回值：无。
 * 特殊行为：字段写入统一走领域层 setter，保持校验和副作用一致。
 */
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
    power_engine_set_device_name(e, c->device_name);
}

// 读单个 i32，缺失时保留默认值
/**
 * 从 NVS 读取一个 32 位整数键。
 * 若键存在则写入输出变量，否则保持调用方当前默认值不变。
 * h：已打开的 NVS 句柄。
 * key：键名。
 * out：输出整数地址。
 * 返回值：无。
 * 特殊行为：键不存在不视为错误，方便以 defaults 结构为底逐项覆盖。
 */
static void get_i32(nvs_handle_t h, const char *key, int *out)
{
    int32_t v;
    if (nvs_get_i32(h, key, &v) == ESP_OK) {
        *out = (int)v;
    }
}

/**
 * 从 NVS 加载配置。
 * 读取 namespace `bikepower` 下的键值，对版本迁移和越界修正后写回引擎。
 * e：待接收配置的功率引擎实例。
 * 返回值：无。
 * 特殊行为：无配置时会回写默认值；发生迁移或夹紧时也会重新保存修正后的结果。
 */
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
    size_t dn_len = sizeof(c.device_name);
    nvs_get_str(h, K_DEVNAME, c.device_name, &dn_len);
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

/**
 * 把当前配置保存到 NVS。
 * 将引擎状态转换为 config_data_t 后逐键写入 namespace `bikepower` 并提交。
 * e：待保存的功率引擎实例。
 * 返回值：`true` 表示保存成功，`false` 表示失败。
 * 特殊行为：写入失败时只记录日志，不做回滚。
 */
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
    nvs_set_str(h, K_DEVNAME, c.device_name);

    err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS commit 失败(%s)", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "配置已保存 p=%dW mode=%d", c.power, c.mode);
    return true;
}
