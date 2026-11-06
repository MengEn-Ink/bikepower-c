#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "esp_log.h"
#include "bikepower_config.h"
#include "persistent_store.h"

static const char *TAG = "PERSIST";
static const char *s_slots[] = {NVS_KEY_SNAPSHOT_0, NVS_KEY_SNAPSHOT_1};
static const char *s_markers[] = {NVS_KEY_COMMIT_0, NVS_KEY_COMMIT_1};
static StaticSemaphore_t s_mutex_buffer;
static SemaphoreHandle_t s_mutex;
static bool s_initialized;
static bool s_writable;
static int s_active = -1;
static uint32_t s_generation;
static config_data_t s_config;
static device_identity_t s_identity;
static uint8_t s_wire[CONFIG_SNAPSHOT_MAX_LEN];
static config_data_t s_checked_config;
static device_identity_t s_checked_identity;

static bool lock_store(void)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buffer);
    return s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(NVS_SAVE_LOCK_MS)) == pdTRUE;
}

static esp_err_t read_blob(nvs_handle_t handle, const char *key, uint8_t *out,
                           size_t *length)
{
    return nvs_get_blob(handle, key, out, length);
}

static bool newer(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) > 0;
}

persistent_store_result_t persistent_store_load(config_data_t *config, device_identity_t *identity)
{
    if (!config || !identity || !lock_store()) return PERSISTENT_STORE_IO_ERROR;
    s_initialized = false;
    s_writable = false;
    s_active = -1;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return err == ESP_ERR_NVS_NOT_FOUND ? PERSISTENT_STORE_ABSENT : PERSISTENT_STORE_IO_ERROR;
    }
    bool found_marker = false;
    unsigned marker_count = 0;
    bool io_error = false;
    for (unsigned i = 0; i < 2; i++) {
        uint8_t marker[CONFIG_SNAPSHOT_MARKER_LEN];
        size_t length = sizeof(marker);
        err = read_blob(handle, s_markers[i], marker, &length);
        if (err == ESP_ERR_NVS_NOT_FOUND) continue;
        found_marker = true;
        marker_count++;
        if (err != ESP_OK && err != ESP_ERR_NVS_INVALID_LENGTH) {
            io_error = true;
            continue;
        }
        unsigned slot;
        uint32_t generation;
        if (err || !config_snapshot_marker_decode(marker, length, &slot, &generation) || slot != i) continue;
        length = sizeof(s_wire);
        err = read_blob(handle, s_slots[i], s_wire, &length);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND && err != ESP_ERR_NVS_INVALID_LENGTH) io_error = true;
        uint32_t actual_generation;
        if (err || !config_snapshot_decode(s_wire, length, &s_checked_config,
                                            &s_checked_identity, &actual_generation)
            || actual_generation != generation) continue;
        if (s_active < 0 || newer(generation, s_generation)) {
            s_config = s_checked_config;
            s_identity = s_checked_identity;
            s_generation = generation;
            s_active = (int)i;
        }
    }
    bool migration_incomplete = false;
    if (s_active < 0 && marker_count == 1 && !io_error) {
        size_t length = sizeof(s_wire);
        uint32_t generation;
        err = read_blob(handle, s_slots[0], s_wire, &length);
        migration_incomplete = err == ESP_OK
            && config_snapshot_decode(s_wire, length, &s_checked_config, &s_checked_identity, &generation)
            && generation == 1;
    }
    nvs_close(handle);
    persistent_store_result_t result;
    if (s_active >= 0 && !io_error) {
        *config = s_config;
        *identity = s_identity;
        s_initialized = true;
        s_writable = true;
        result = PERSISTENT_STORE_OK;
    } else {
        result = io_error ? PERSISTENT_STORE_IO_ERROR
            : migration_incomplete ? PERSISTENT_STORE_MIGRATION_INCOMPLETE
            : found_marker ? PERSISTENT_STORE_INVALID : PERSISTENT_STORE_ABSENT;
    }
    xSemaphoreGive(s_mutex);
    return result;
}

void persistent_store_seed(const config_data_t *config, const device_identity_t *identity)
{
    if (!config || !identity || !lock_store()) return;
    s_config = *config;
    s_identity = *identity;
    s_active = -1;
    s_generation = 0;
    s_initialized = true;
    s_writable = true;
    xSemaphoreGive(s_mutex);
}

void persistent_store_readonly(const config_data_t *config, const device_identity_t *identity)
{
    if (!config || !identity || !lock_store()) return;
    s_config = *config;
    s_identity = *identity;
    s_initialized = true;
    s_writable = false;
    xSemaphoreGive(s_mutex);
}

void persistent_store_readonly_defaults(void)
{
    if (!lock_store()) return;
    config_store_defaults(&s_config);
    device_identity_defaults(&s_identity);
    s_initialized = true;
    s_writable = false;
    xSemaphoreGive(s_mutex);
}

static bool save_locked(const config_data_t *config, const device_identity_t *identity)
{
    if (!s_initialized || !s_writable) return false;
    unsigned slot = s_active == 0 ? 1 : 0;
    uint32_t generation = s_generation + 1;
    size_t length = config_snapshot_encode(config, identity, generation, s_wire, sizeof(s_wire));
    if (!length) return false;
    uint32_t expected_crc = config_snapshot_crc(s_wire, length);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;
    err = nvs_set_blob(handle, s_slots[slot], s_wire, length);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err == ESP_OK) {
        size_t actual_length = sizeof(s_wire);
        err = read_blob(handle, s_slots[slot], s_wire, &actual_length);
        uint32_t actual_generation;
        if (err == ESP_OK && (actual_length != length
            || config_snapshot_crc(s_wire, actual_length) != expected_crc
            || !config_snapshot_decode(s_wire, actual_length, &s_checked_config,
                                        &s_checked_identity, &actual_generation)
            || actual_generation != generation)) err = ESP_FAIL;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGW(TAG, "候选快照写入或回读失败，保留已提交配置");
        return false;
    }
    uint8_t marker[CONFIG_SNAPSHOT_MARKER_LEN];
    config_snapshot_marker_encode(slot, generation, marker);
    err = nvs_set_blob(handle, s_markers[slot], marker, sizeof(marker));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err == ESP_OK) {
        uint8_t readback[CONFIG_SNAPSHOT_MARKER_LEN];
        size_t actual_length = sizeof(readback);
        err = read_blob(handle, s_markers[slot], readback, &actual_length);
        if (err == ESP_OK && (actual_length != sizeof(marker)
            || memcmp(marker, readback, sizeof(marker)))) err = ESP_FAIL;
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        s_writable = false;
        ESP_LOGE(TAG, "提交标记结果不确定，保留运行态并禁止继续写入，需重启核对");
        return false;
    }
    s_config = *config;
    s_identity = *identity;
    s_generation = generation;
    s_active = (int)slot;
    return true;
}

bool persistent_store_save(const config_data_t *config, const device_identity_t *identity)
{
    if (!config || !lock_store()) return false;
    bool ok = save_locked(config, identity ? identity : &s_identity);
    xSemaphoreGive(s_mutex);
    return ok;
}

bool persistent_store_save_identity(const device_identity_t *identity)
{
    if (!identity || !lock_store()) return false;
    bool ok = save_locked(&s_config, identity);
    xSemaphoreGive(s_mutex);
    return ok;
}

bool persistent_store_identity(device_identity_t *identity)
{
    if (!identity || !lock_store()) return false;
    bool ok = s_initialized;
    if (ok) *identity = s_identity;
    xSemaphoreGive(s_mutex);
    return ok;
}
