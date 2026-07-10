/**
 * NVS 配置桥接实现 — 见 nvs_store.h
 */
#include "nvs_store.h"
#include "config_store.h"
#include "bikepower_config.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "NVS";

// NVS 写入互斥锁：app_main 主循环（按钮调功 dirty 落盘）与 httpd 任务（网页保存）
// 可能并发调用 nvs_store_save，加锁避免两个 nvs_open 同 namespace 并发写造成数据竞争。
static SemaphoreHandle_t s_save_mutex = NULL;
static StaticSemaphore_t s_save_mutex_buf;

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
    c->heartrate = e->has_user_hr ? e->user_heartrate : DEFAULT_HEARTRATE;
    c->has_user_hr = e->has_user_hr ? 1 : 0;
    c->power_float = e->power_float;
    c->cadence_float = e->cadence_float;
    c->hr_float = e->hr_float;
    c->power_step = e->power_step;
    c->weight = e->profile.weight;
    c->ftp = e->profile.ftp;
    c->thr_hr = e->profile.thr_hr;
    c->ride_type = e->profile.ride_type;
    c->custom_wkg_low = e->profile.custom_wkg_low;
    c->custom_wkg_high = e->profile.custom_wkg_high;
    strncpy(c->custom_pacer_name, e->profile.custom_pacer_name, CUSTOM_PACER_NAME_MAX - 1);
    c->custom_pacer_name[CUSTOM_PACER_NAME_MAX - 1] = '\0';
    c->led = e->led;
    c->sport_profile = (int)e->sport_profile;
    c->broadcast_mask = e->broadcast_mask;
    c->run_speed_x100 = e->running.speed_x100_kmh;
    c->run_pace_float_sec = e->running.pace_float_sec;
    c->run_cadence = e->running.cadence_spm;
    c->run_cadence_float = e->running.cadence_float_spm;
    c->run_mode = e->running.mode;
    c->run_pacer_type = e->running.pacer_type;
    c->run_pace_low_sec = e->running.pace_low_sec;
    c->run_pace_high_sec = e->running.pace_high_sec;
    strncpy(c->run_custom_name, e->running.custom_name, RUN_CUSTOM_NAME_MAX - 1);
    c->run_custom_name[RUN_CUSTOM_NAME_MAX - 1] = '\0';
    c->run_incline_x10 = e->running.incline_x10;
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
    power_engine_set_power_float(e, c->power_float);
    power_engine_set_cadence_float(e, c->cadence_float);
    power_engine_set_hr_float(e, c->hr_float);
    power_engine_set_power_step(e, c->power_step);
    // 先恢复固定心率：初始化时engine默认mode为STEADY，set_heartrate可正常生效；
    // 后续set_mode切换到ROAD/ERG不会清除has_user_hr/user_heartrate字段，切回STEADY时可自动恢复使用
    if (c->has_user_hr) power_engine_set_heartrate(e, c->heartrate);
    power_engine_set_mode(e, (ride_mode_t)c->mode);
    profile_set_weight(&e->profile, c->weight);
    profile_set_ftp(&e->profile, c->ftp);
    profile_set_thr_hr(&e->profile, c->thr_hr);
    profile_set_custom_wkg(&e->profile, c->custom_wkg_low, c->custom_wkg_high);
    profile_set_custom_name(&e->profile, c->custom_pacer_name);
    profile_set_ride_type(&e->profile, c->ride_type);
    e->led = c->led;
    power_engine_set_sport_profile(e, (sport_profile_t)c->sport_profile);
    power_engine_set_broadcast_mask(e, c->broadcast_mask);
    power_engine_set_run_speed(e, c->run_speed_x100);
    power_engine_set_run_pace_float(e, c->run_pace_float_sec);
    power_engine_set_run_cadence(e, c->run_cadence);
    power_engine_set_run_cadence_float(e, c->run_cadence_float);
    if (c->run_pacer_type == RUN_PACER_CUSTOM) {
        power_engine_set_run_custom_pacer(e, c->run_custom_name, c->run_pace_low_sec, c->run_pace_high_sec, c->run_cadence);
    } else {
        power_engine_set_run_pacer(e, (run_pacer_type_t)c->run_pacer_type);
        power_engine_set_run_pace_range(e, c->run_pace_low_sec, c->run_pace_high_sec);
    }
    power_engine_set_run_mode(e, (run_mode_t)c->run_mode);
    power_engine_set_run_incline(e, c->run_incline_x10);
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
    // 启动时单线程初始化互斥锁（load 先于任何 save 调用，保证 mutex 在 save 之前就绪）
    if (s_save_mutex == NULL) {
        s_save_mutex = xSemaphoreCreateMutexStatic(&s_save_mutex_buf);
    }

    config_data_t c;
    config_store_defaults(&c);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "无配置，使用默认值并回写");
        config_to_engine(&c, e);
        if (!nvs_store_save(e)) ESP_LOGW(TAG, "默认值回写 NVS 失败，下次启动将重试");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "打开 NVS 失败(%s)，使用默认值", esp_err_to_name(err));
        config_to_engine(&c, e);
        return;
    }

    get_i32(h, NVS_KEY_SCHEMA_VER, &c.schema_ver);
    get_i32(h, NVS_KEY_POWER, &c.power);
    get_i32(h, NVS_KEY_CADENCE, &c.cadence);
    get_i32(h, NVS_KEY_MODE, &c.mode);
    get_i32(h, NVS_KEY_HEARTRATE, &c.heartrate);
    get_i32(h, NVS_KEY_HAS_USER_HR, &c.has_user_hr);
    get_i32(h, NVS_KEY_POWER_FLOAT, &c.power_float);
    get_i32(h, NVS_KEY_CADENCE_FLOAT, &c.cadence_float);
    get_i32(h, NVS_KEY_HR_FLOAT, &c.hr_float);
    get_i32(h, NVS_KEY_POWER_STEP, &c.power_step);
    get_i32(h, NVS_KEY_WEIGHT, &c.weight);
    get_i32(h, NVS_KEY_FTP, &c.ftp);
    get_i32(h, NVS_KEY_THR_HR, &c.thr_hr);
    get_i32(h, NVS_KEY_RIDE_TYPE, &c.ride_type);
    get_i32(h, NVS_KEY_CUSTOM_WKG_LO, &c.custom_wkg_low);
    get_i32(h, NVS_KEY_CUSTOM_WKG_HI, &c.custom_wkg_high);
    get_i32(h, NVS_KEY_SPORT_PROFILE, &c.sport_profile);
    get_i32(h, NVS_KEY_BROADCAST_MASK, &c.broadcast_mask);
    get_i32(h, NVS_KEY_RUN_SPEED, &c.run_speed_x100);
    get_i32(h, NVS_KEY_RUN_PACE_FLOAT, &c.run_pace_float_sec);
    get_i32(h, NVS_KEY_RUN_CADENCE, &c.run_cadence);
    get_i32(h, NVS_KEY_RUN_CAD_FLOAT, &c.run_cadence_float);
    get_i32(h, NVS_KEY_RUN_MODE, &c.run_mode);
    get_i32(h, NVS_KEY_RUN_PACER, &c.run_pacer_type);
    get_i32(h, NVS_KEY_RUN_PACE_LO, &c.run_pace_low_sec);
    get_i32(h, NVS_KEY_RUN_PACE_HI, &c.run_pace_high_sec);
    get_i32(h, NVS_KEY_RUN_INCLINE, &c.run_incline_x10);
    size_t cn_len = sizeof(c.custom_pacer_name);
    esp_err_t cn_err = nvs_get_str(h, NVS_KEY_CUSTOM_NAME, c.custom_pacer_name, &cn_len);
    if (cn_err != ESP_OK && cn_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "读取自定义配速昵称失败(%s)，保留默认昵称", esp_err_to_name(cn_err));
    }
    size_t rcn_len = sizeof(c.run_custom_name);
    esp_err_t rcn_err = nvs_get_str(h, NVS_KEY_RUN_CUSTOM_NAME, c.run_custom_name, &rcn_len);
    if (rcn_err != ESP_OK && rcn_err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "读取自定义跑步档名称失败(%s)，保留默认名称", esp_err_to_name(rcn_err));
    }
    int led_tmp;
    led_tmp = c.led.silent; get_i32(h, NVS_KEY_LED_SILENT, &led_tmp); c.led.silent = (uint8_t)led_tmp;
    led_tmp = c.led.scenes[LED_SCENE_WAIT_CONN].mode; get_i32(h, NVS_KEY_LED_WAIT_MODE, &led_tmp); c.led.scenes[LED_SCENE_WAIT_CONN].mode = (uint8_t)led_tmp;
    led_tmp = c.led.scenes[LED_SCENE_WAIT_CONN].color; get_i32(h, NVS_KEY_LED_WAIT_COLOR, &led_tmp); c.led.scenes[LED_SCENE_WAIT_CONN].color = (uint8_t)led_tmp;
    led_tmp = c.led.scenes[LED_SCENE_CONNECTED].mode; get_i32(h, NVS_KEY_LED_CONN_MODE, &led_tmp); c.led.scenes[LED_SCENE_CONNECTED].mode = (uint8_t)led_tmp;
    led_tmp = c.led.scenes[LED_SCENE_CONNECTED].color; get_i32(h, NVS_KEY_LED_CONN_COLOR, &led_tmp); c.led.scenes[LED_SCENE_CONNECTED].color = (uint8_t)led_tmp;
    led_tmp = c.led.scenes[LED_SCENE_WIFI_CONFIG].mode; get_i32(h, NVS_KEY_LED_WIFI_MODE, &led_tmp); c.led.scenes[LED_SCENE_WIFI_CONFIG].mode = (uint8_t)led_tmp;
    led_tmp = c.led.scenes[LED_SCENE_WIFI_CONFIG].color; get_i32(h, NVS_KEY_LED_WIFI_COLOR, &led_tmp); c.led.scenes[LED_SCENE_WIFI_CONFIG].color = (uint8_t)led_tmp;
    led_tmp = c.led.scenes[LED_SCENE_WIFI_CONFIRM].mode; get_i32(h, NVS_KEY_LED_CONFIRM_MODE, &led_tmp); c.led.scenes[LED_SCENE_WIFI_CONFIRM].mode = (uint8_t)led_tmp;
    led_tmp = c.led.scenes[LED_SCENE_WIFI_CONFIRM].color; get_i32(h, NVS_KEY_LED_CONFIRM_COLOR, &led_tmp); c.led.scenes[LED_SCENE_WIFI_CONFIRM].color = (uint8_t)led_tmp;
    led_tmp = c.led.scenes[LED_SCENE_ERROR].mode; get_i32(h, NVS_KEY_LED_ERROR_MODE, &led_tmp); c.led.scenes[LED_SCENE_ERROR].mode = (uint8_t)led_tmp;
    led_tmp = c.led.scenes[LED_SCENE_ERROR].color; get_i32(h, NVS_KEY_LED_ERROR_COLOR, &led_tmp); c.led.scenes[LED_SCENE_ERROR].color = (uint8_t)led_tmp;
    size_t dn_len = sizeof(c.device_name);
    esp_err_t dn_err = nvs_get_str(h, NVS_KEY_DEVICE_NAME, c.device_name, &dn_len);
    if (dn_err != ESP_OK && dn_err != ESP_ERR_NVS_NOT_FOUND) {
        // 读取失败（如存储项损坏/超长）时保留 defaults 的设备名，并记录原因便于排查。
        ESP_LOGW(TAG, "读取设备名失败(%s)，保留默认设备名", esp_err_to_name(dn_err));
    }
    nvs_close(h);

    bool migrated = config_store_migrate(&c);
    bool fixed = config_store_validate(&c);
    config_to_engine(&c, e);

    ESP_LOGI(TAG, "配置加载完成 p=%dW c=%dRPM mode=%d hr=%d(user=%d) w=%d kg ftp=%dW thr=%dBPM ride=%d custom=%s pf=%d cf=%d hf=%d step=%d sport=%d bmask=%d run=%d/%d/%d/%d rmode=%d rpacer=%d rpace=%d-%d rincl=%d dn=%s%s%s",
             c.power, c.cadence, c.mode, c.heartrate, c.has_user_hr,
             c.weight, c.ftp, c.thr_hr, c.ride_type,
             c.custom_pacer_name,
             c.power_float, c.cadence_float, c.hr_float, c.power_step,
             c.sport_profile, c.broadcast_mask, c.run_speed_x100, c.run_pace_float_sec, c.run_cadence, c.run_cadence_float,
             c.run_mode, c.run_pacer_type, c.run_pace_low_sec, c.run_pace_high_sec, c.run_incline_x10,
             c.device_name,
             migrated ? " [migrated]" : "", fixed ? " [clamped]" : "");

    if (migrated || fixed) {
        if (!nvs_store_save(e)) ESP_LOGW(TAG, "迁移/夹紧后回写 NVS 失败，下次启动将重试");
    }
}

/**
 * 把当前配置保存到 NVS。
 * 将引擎状态转换为 config_data_t 后逐键写入 namespace 并提交。
 * 任一键写入失败或 commit 失败都返回 false，保留 dirty 由调用方重试。
 * e：待保存的功率引擎实例。
 * 返回值：`true` 表示保存成功，`false` 表示失败。
 * 特殊行为：失败时记录具体出错的键名，便于诊断。
 */
bool nvs_store_save(power_engine_t *e)
{
    // 防御性 lazy init：load 异常时 mutex 仍可能在 save 首次调用时为 NULL
    if (s_save_mutex == NULL) {
        s_save_mutex = xSemaphoreCreateMutexStatic(&s_save_mutex_buf);
    }
    // 互斥保护整段 nvs_open→set→commit 流程，避免并发写造成 commit 丢失或脏写。
    // 等待 100ms 让另一侧先写完；超时则放弃本次保存（dirty 保留，下次心跳重试）。
    if (xSemaphoreTake(s_save_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "NVS 保存互斥锁等待超时，跳过本次保存");
        return false;
    }

    config_data_t c;
    engine_to_config(e, &c);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "保存打开 NVS 失败(%s)", esp_err_to_name(err));
        xSemaphoreGive(s_save_mutex);
        return false;
    }

    bool ok = true;
#define SET_I32(k, v) do { \
        esp_err_t _e = nvs_set_i32(h, (k), (v)); \
        if (_e != ESP_OK) { \
            ESP_LOGW(TAG, "nvs_set_i32 %s 失败(%s)", (k), esp_err_to_name(_e)); \
            ok = false; \
        } \
    } while (0)

    SET_I32(NVS_KEY_SCHEMA_VER, c.schema_ver);
    SET_I32(NVS_KEY_POWER, c.power);
    SET_I32(NVS_KEY_CADENCE, c.cadence);
    SET_I32(NVS_KEY_MODE, c.mode);
    SET_I32(NVS_KEY_HEARTRATE, c.heartrate);
    SET_I32(NVS_KEY_HAS_USER_HR, c.has_user_hr);
    SET_I32(NVS_KEY_POWER_FLOAT, c.power_float);
    SET_I32(NVS_KEY_CADENCE_FLOAT, c.cadence_float);
    SET_I32(NVS_KEY_HR_FLOAT, c.hr_float);
    SET_I32(NVS_KEY_POWER_STEP, c.power_step);
    SET_I32(NVS_KEY_WEIGHT, c.weight);
    SET_I32(NVS_KEY_FTP, c.ftp);
    SET_I32(NVS_KEY_THR_HR, c.thr_hr);
    SET_I32(NVS_KEY_RIDE_TYPE, c.ride_type);
    SET_I32(NVS_KEY_CUSTOM_WKG_LO, c.custom_wkg_low);
    SET_I32(NVS_KEY_CUSTOM_WKG_HI, c.custom_wkg_high);
    SET_I32(NVS_KEY_SPORT_PROFILE, c.sport_profile);
    SET_I32(NVS_KEY_BROADCAST_MASK, c.broadcast_mask);
    SET_I32(NVS_KEY_RUN_SPEED, c.run_speed_x100);
    SET_I32(NVS_KEY_RUN_PACE_FLOAT, c.run_pace_float_sec);
    SET_I32(NVS_KEY_RUN_CADENCE, c.run_cadence);
    SET_I32(NVS_KEY_RUN_CAD_FLOAT, c.run_cadence_float);
    SET_I32(NVS_KEY_RUN_MODE, c.run_mode);
    SET_I32(NVS_KEY_RUN_PACER, c.run_pacer_type);
    SET_I32(NVS_KEY_RUN_PACE_LO, c.run_pace_low_sec);
    SET_I32(NVS_KEY_RUN_PACE_HI, c.run_pace_high_sec);
    SET_I32(NVS_KEY_RUN_INCLINE, c.run_incline_x10);
    SET_I32(NVS_KEY_LED_SILENT, c.led.silent);
    SET_I32(NVS_KEY_LED_WAIT_MODE, c.led.scenes[LED_SCENE_WAIT_CONN].mode);
    SET_I32(NVS_KEY_LED_WAIT_COLOR, c.led.scenes[LED_SCENE_WAIT_CONN].color);
    SET_I32(NVS_KEY_LED_CONN_MODE, c.led.scenes[LED_SCENE_CONNECTED].mode);
    SET_I32(NVS_KEY_LED_CONN_COLOR, c.led.scenes[LED_SCENE_CONNECTED].color);
    SET_I32(NVS_KEY_LED_WIFI_MODE, c.led.scenes[LED_SCENE_WIFI_CONFIG].mode);
    SET_I32(NVS_KEY_LED_WIFI_COLOR, c.led.scenes[LED_SCENE_WIFI_CONFIG].color);
    SET_I32(NVS_KEY_LED_CONFIRM_MODE, c.led.scenes[LED_SCENE_WIFI_CONFIRM].mode);
    SET_I32(NVS_KEY_LED_CONFIRM_COLOR, c.led.scenes[LED_SCENE_WIFI_CONFIRM].color);
    SET_I32(NVS_KEY_LED_ERROR_MODE, c.led.scenes[LED_SCENE_ERROR].mode);
    SET_I32(NVS_KEY_LED_ERROR_COLOR, c.led.scenes[LED_SCENE_ERROR].color);
#undef SET_I32

    err = nvs_set_str(h, NVS_KEY_CUSTOM_NAME, c.custom_pacer_name);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_str %s 失败(%s)", NVS_KEY_CUSTOM_NAME, esp_err_to_name(err));
        ok = false;
    }

    err = nvs_set_str(h, NVS_KEY_RUN_CUSTOM_NAME, c.run_custom_name);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_str %s 失败(%s)", NVS_KEY_RUN_CUSTOM_NAME, esp_err_to_name(err));
        ok = false;
    }

    err = nvs_set_str(h, NVS_KEY_DEVICE_NAME, c.device_name);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_str %s 失败(%s)", NVS_KEY_DEVICE_NAME, esp_err_to_name(err));
        ok = false;
    }

    if (ok) {
        err = nvs_commit(h);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "NVS commit 失败(%s)", esp_err_to_name(err));
            ok = false;
        }
    }
    nvs_close(h);

    xSemaphoreGive(s_save_mutex);

    if (ok) {
        ESP_LOGI(TAG, "配置已保存 p=%dW c=%dRPM mode=%d hr=%d(user=%d) w=%d ftp=%d thr=%d ride=%d custom=%s pf=%d cf=%d hf=%d step=%d sport=%d bmask=%d run=%d/%d/%d/%d rmode=%d rpacer=%d rpace=%d-%d rincl=%d dn=%s",
                 c.power, c.cadence, c.mode, c.heartrate, c.has_user_hr,
                 c.weight, c.ftp, c.thr_hr, c.ride_type,
                 c.custom_pacer_name,
                 c.power_float, c.cadence_float, c.hr_float, c.power_step,
                 c.sport_profile, c.broadcast_mask, c.run_speed_x100, c.run_pace_float_sec, c.run_cadence, c.run_cadence_float,
                 c.run_mode, c.run_pacer_type, c.run_pace_low_sec, c.run_pace_high_sec, c.run_incline_x10,
                 c.device_name);
    }
    return ok;
}
