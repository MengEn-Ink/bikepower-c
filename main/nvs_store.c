#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "esp_log.h"
#include "bikepower_config.h"
#include "nvs_store.h"
#include "persistent_store.h"
#include "device_identity_store.h"

static const char *TAG = "NVS";
static StaticSemaphore_t s_recent_mutex_buffer;
static SemaphoreHandle_t s_recent_mutex;

#define FIELD(key, name) {key, offsetof(config_data_t, name)}
static const struct { const char *key; size_t offset; } s_legacy_fields[] = {
    FIELD(NVS_KEY_SCHEMA_VER, schema_ver), FIELD(NVS_KEY_POWER, power),
    FIELD(NVS_KEY_CADENCE, cadence), FIELD(NVS_KEY_MODE, mode),
    FIELD(NVS_KEY_HEARTRATE, heartrate), FIELD(NVS_KEY_HAS_USER_HR, has_user_hr),
    FIELD(NVS_KEY_POWER_FLOAT, power_float), FIELD(NVS_KEY_CADENCE_FLOAT, cadence_float),
    FIELD(NVS_KEY_HR_FLOAT, hr_float), FIELD(NVS_KEY_POWER_STEP, power_step),
    FIELD(NVS_KEY_WEIGHT, weight), FIELD(NVS_KEY_FTP, ftp), FIELD(NVS_KEY_THR_HR, thr_hr),
    FIELD(NVS_KEY_RIDE_TYPE, ride_type), FIELD(NVS_KEY_CUSTOM_WKG_LO, custom_wkg_low),
    FIELD(NVS_KEY_CUSTOM_WKG_HI, custom_wkg_high), FIELD(NVS_KEY_SPORT_PROFILE, sport_profile),
    FIELD(NVS_KEY_BROADCAST_MASK, broadcast_mask), FIELD(NVS_KEY_RUN_SPEED, run_speed_x100),
    FIELD(NVS_KEY_RUN_PACE_FLOAT, run_pace_float_sec), FIELD(NVS_KEY_RUN_CADENCE, run_cadence),
    FIELD(NVS_KEY_RUN_CAD_FLOAT, run_cadence_float), FIELD(NVS_KEY_RUN_MODE, run_mode),
    FIELD(NVS_KEY_RUN_PACER, run_pacer_type), FIELD(NVS_KEY_RUN_PACE_LO, run_pace_low_sec),
    FIELD(NVS_KEY_RUN_PACE_HI, run_pace_high_sec),
};
#undef FIELD

static const char *s_led_keys[] = {
    NVS_KEY_LED_SILENT, NVS_KEY_LED_WAIT_MODE, NVS_KEY_LED_WAIT_COLOR,
    NVS_KEY_LED_CONN_MODE, NVS_KEY_LED_CONN_COLOR, NVS_KEY_LED_WIFI_MODE,
    NVS_KEY_LED_WIFI_COLOR, NVS_KEY_LED_CONFIRM_MODE, NVS_KEY_LED_CONFIRM_COLOR,
    NVS_KEY_LED_ERROR_MODE, NVS_KEY_LED_ERROR_COLOR,
};

static bool read_legacy(config_data_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK) return false;
    bool ok = true;
    for (size_t i = 0; i < sizeof(s_legacy_fields) / sizeof(s_legacy_fields[0]); i++) {
        int32_t value;
        err = nvs_get_i32(handle, s_legacy_fields[i].key, &value);
        if (err == ESP_OK) *(int *)((uint8_t *)config + s_legacy_fields[i].offset) = (int)value;
        else if (err != ESP_ERR_NVS_NOT_FOUND) ok = false;
    }
    uint8_t *led_fields[1 + LED_SCENE_COUNT * 2];
    led_fields[0] = &config->led.silent;
    for (unsigned i = 0; i < LED_SCENE_COUNT; i++) {
        led_fields[1 + i * 2] = &config->led.scenes[i].mode;
        led_fields[2 + i * 2] = &config->led.scenes[i].color;
    }
    for (size_t i = 0; i < sizeof(s_led_keys) / sizeof(s_led_keys[0]); i++) {
        int32_t value;
        err = nvs_get_i32(handle, s_led_keys[i], &value);
        if (err == ESP_OK) *led_fields[i] = value < 0 || value > UINT8_MAX ? UINT8_MAX : (uint8_t)value;
        else if (err != ESP_ERR_NVS_NOT_FOUND) ok = false;
    }
    const char *keys[] = {NVS_KEY_CUSTOM_NAME, NVS_KEY_RUN_CUSTOM_NAME, NVS_KEY_DEVICE_NAME};
    char *strings[] = {config->custom_pacer_name, config->run_custom_name, config->device_name};
    size_t capacities[] = {sizeof(config->custom_pacer_name), sizeof(config->run_custom_name), sizeof(config->device_name)};
    for (unsigned i = 0; i < 3; i++) {
        size_t length = capacities[i];
        err = nvs_get_str(handle, keys[i], strings[i], &length);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) ok = false;
    }
    uint8_t bridge[BRIDGE_WIRE_MAX_LEN];
    size_t length = sizeof(bridge);
    err = nvs_get_blob(handle, NVS_KEY_BRIDGE, bridge, &length);
    if (err == ESP_OK) {
        if (!power_bridge_config_decode(bridge, length, &config->bridge)) ok = false;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) ok = false;
    nvs_close(handle);
    config_store_migrate(config);
    config_store_validate(config);
    return ok;
}

void nvs_store_load(power_engine_t *engine)
{
    if (!engine) return;
    static config_data_t config;
    static device_identity_t identity;
    config_store_defaults(&config);
    device_identity_defaults(&identity);
    persistent_store_result_t result = persistent_store_load(&config, &identity);
    if (result == PERSISTENT_STORE_OK) {
        config_store_apply_to_engine(&config, engine);
        return;
    }
    if (result != PERSISTENT_STORE_ABSENT && result != PERSISTENT_STORE_MIGRATION_INCOMPLETE) {
        persistent_store_readonly_defaults();
        ESP_LOGE(TAG, "快照不可读取，使用安全默认值并禁止覆盖存储");
        config_store_apply_to_engine(&config, engine);
        return;
    }
    bool ok = read_legacy(&config);
    if (!device_identity_store_load_legacy(&identity)) ok = false;
    config_store_apply_to_engine(&config, engine);
    if (!ok) {
        persistent_store_readonly_defaults();
        config_store_defaults(&config);
        config_store_apply_to_engine(&config, engine);
        ESP_LOGE(TAG, "旧配置读取失败，禁止将不完整旧值迁移写回");
        return;
    }
    if (result == PERSISTENT_STORE_MIGRATION_INCOMPLETE) {
        persistent_store_readonly(&config, &identity);
        ESP_LOGE(TAG, "首次提交标记不完整，使用原旧配置并禁止继续写入");
        return;
    }
    persistent_store_seed(&config, &identity);
    if (!persistent_store_save(&config, &identity)) {
        ESP_LOGW(TAG, "旧配置快照迁移未完成，保留旧键并在下次启动重试");
    }
}

bool nvs_store_save_with_identity(power_engine_t *engine, const device_identity_t *identity)
{
    if (!engine) return false;
    config_data_t config;
    config_store_from_engine(engine, &config);
    return persistent_store_save(&config, identity);
}

bool nvs_store_save(power_engine_t *engine)
{
    return nvs_store_save_with_identity(engine, NULL);
}

static bool bridge_recent_valid(const bridge_device_t *device)
{
    if (!device) return false;
    bridge_config_t checked;
    power_bridge_config_defaults(&checked);
    checked.source = BRIDGE_SOURCE_REAL;
    checked.device_count = 1;
    checked.devices[0] = *device;
    checked.devices[0].name[0] = 0;
    return !power_bridge_config_validate(&checked);
}

bool nvs_store_load_bridge_recent(bridge_device_t *device)
{
    if (!device) return false;
    *device = (bridge_device_t){0};
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    uint8_t wire[BRIDGE_RECENT_WIRE_LEN];
    size_t length = sizeof(wire);
    esp_err_t err = nvs_get_blob(handle, NVS_KEY_BRIDGE_RECENT, wire, &length);
    nvs_close(handle);
    if (err != ESP_OK || length != sizeof(wire) || wire[0] != BRIDGE_RECENT_WIRE_VERSION || wire[1] > 1) return false;
    bridge_device_t loaded = {.bound = 1, .addr_type = wire[1]};
    memcpy(loaded.addr, wire + 2, sizeof(loaded.addr));
    if (!bridge_recent_valid(&loaded)) return false;
    *device = loaded;
    return true;
}

bool nvs_store_save_bridge_recent(const bridge_device_t *device)
{
    if (!bridge_recent_valid(device)) return false;
    if (!s_recent_mutex) s_recent_mutex = xSemaphoreCreateMutexStatic(&s_recent_mutex_buffer);
    if (!s_recent_mutex || xSemaphoreTake(s_recent_mutex, pdMS_TO_TICKS(NVS_SAVE_LOCK_MS)) != pdTRUE) return false;
    uint8_t wire[BRIDGE_RECENT_WIRE_LEN] = {BRIDGE_RECENT_WIRE_VERSION, (uint8_t)device->addr_type};
    memcpy(wire + 2, device->addr, sizeof(device->addr));
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, NVS_KEY_BRIDGE_RECENT, wire, sizeof(wire));
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    xSemaphoreGive(s_recent_mutex);
    return err == ESP_OK;
}
