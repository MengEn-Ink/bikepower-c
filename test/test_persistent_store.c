#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvs_stubs.h"
#include "bikepower_config.h"
#include "nvs_store.h"
#include "device_identity_store.h"
#include "persistent_store.h"

typedef struct {
    char key[32];
    int space;
    size_t length;
    uint8_t value[CONFIG_SNAPSHOT_MAX_LEN];
} entry_t;
static entry_t s_entries[64], s_backup[64];
static int s_count, s_backup_count;
static int s_operations, s_fault_at, s_fault_mode;
static bool s_crash_ready;
static jmp_buf s_crash;
static const char *s_fault_key;
static int s_assertions;

#define CHECK(condition) do { s_assertions++; assert(condition); } while (0)

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer) { return buffer; }
int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned timeout) { (void)mutex; (void)timeout; return pdTRUE; }
void xSemaphoreGive(SemaphoreHandle_t mutex) { (void)mutex; }
const char *esp_err_to_name(esp_err_t error) { (void)error; return "fake"; }
void nvs_close(nvs_handle_t handle) { (void)handle; }

static entry_t *entry(int space, const char *key, bool create)
{
    for (int i = 0; i < s_count; i++) if (s_entries[i].space == space && !strcmp(key, s_entries[i].key)) return &s_entries[i];
    if (!create) return NULL;
    assert(s_count < 64);
    entry_t *item = &s_entries[s_count++];
    memset(item, 0, sizeof(*item));
    item->space = space;
    strcpy(item->key, key);
    return item;
}

esp_err_t nvs_open(const char *space, int mode, nvs_handle_t *handle)
{
    (void)mode;
    *handle = strcmp(space, NVS_NAMESPACE) ? 2 : 1;
    return ESP_OK;
}

static bool inject(void)
{
    return ++s_operations == s_fault_at;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length)
{
    bool fault = inject() || (s_fault_key && !strcmp(s_fault_key, key));
    if (fault && s_fault_mode == 1) return ESP_FAIL;
    entry_t *item = entry(handle, key, true);
    assert(length <= sizeof(item->value));
    item->length = length;
    memcpy(item->value, value, length);
    if (fault && s_fault_mode == 2) { item->length /= 2; return ESP_FAIL; }
    if (fault && s_fault_mode == 3 && s_crash_ready) longjmp(s_crash, 1);
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    if (inject()) {
        if (s_fault_mode == 3 && s_crash_ready) longjmp(s_crash, 1);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length)
{
    bool fault = inject();
    if (fault && s_fault_mode == 1) return ESP_FAIL;
    entry_t *item = entry(handle, key, false);
    if (!item) return ESP_ERR_NVS_NOT_FOUND;
    if (*length < item->length) { *length = item->length; return ESP_ERR_NVS_INVALID_LENGTH; }
    *length = item->length;
    memcpy(out, item->value, item->length);
    if (fault && s_fault_mode == 2 && item->length) ((uint8_t *)out)[0] ^= 1;
    if (fault && s_fault_mode == 3 && s_crash_ready) longjmp(s_crash, 1);
    return ESP_OK;
}
esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value) { return nvs_set_blob(handle, key, &value, sizeof(value)); }
esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *value) { size_t length = sizeof(*value); return nvs_get_blob(handle, key, value, &length); }
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) { return nvs_set_blob(handle, key, value, strlen(value) + 1); }
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *length) { return nvs_get_blob(handle, key, out, length); }
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) { entry_t *item = entry(handle, key, false); if (!item) return ESP_ERR_NVS_NOT_FOUND; item->length = 0; return ESP_OK; }

static void no_fault(void) { s_fault_at = 0; s_fault_key = NULL; s_fault_mode = 0; s_operations = 0; }
static void backup(void) { memcpy(s_backup, s_entries, sizeof(s_entries)); s_backup_count = s_count; }
static void restore(void) { memcpy(s_entries, s_backup, sizeof(s_entries)); s_count = s_backup_count; no_fault(); }

static void old_code_failure(void)
{
    s_count = 0;
    no_fault();
    power_engine_t engine;
    power_engine_init(&engine);
    nvs_store_load(&engine);
    CHECK(nvs_store_save(&engine));
    power_engine_set_power(&engine, 345);
    power_engine_set_cadence(&engine, 111);
    s_fault_key = NVS_KEY_CADENCE;
    s_fault_mode = 1;
    CHECK(!nvs_store_save(&engine));
    no_fault();
    power_engine_init(&engine);
    nvs_store_load(&engine);
    CHECK(engine.base_power == DEFAULT_POWER && engine.cadence_base == DEFAULT_CADENCE);
}

static bool same_config(const config_data_t *a, const config_data_t *b)
{
    char left[CONFIG_JSON_MAX_LEN], right[CONFIG_JSON_MAX_LEN];
    return config_store_serialize(a, left, sizeof(left)) && config_store_serialize(b, right, sizeof(right)) && !strcmp(left, right);
}

static void snapshot_codec(void)
{
    config_data_t config, decoded;
    config_store_defaults(&config);
    device_identity_t id, out;
    device_identity_defaults(&id);
    strcpy(id.fields[0], "Original");
    uint8_t wire[CONFIG_SNAPSHOT_MAX_LEN];
    size_t length = config_snapshot_encode(&config, &id, UINT32_MAX, wire, sizeof(wire));
    CHECK(length > 0);
    uint32_t generation;
    CHECK(config_snapshot_decode(wire, length, &decoded, &out, &generation));
    CHECK(generation == UINT32_MAX && same_config(&config, &decoded) && !memcmp(&id, &out, sizeof(id)));
    for (size_t cut = 0; cut < length; cut++) CHECK(!config_snapshot_decode(wire, cut, &decoded, &out, &generation));
    for (size_t i = 0; i < length; i++) {
        wire[i] ^= 1;
        CHECK(!config_snapshot_decode(wire, length, &decoded, &out, &generation));
        wire[i] ^= 1;
    }
    CHECK(config_snapshot_crc((const uint8_t *)"123456789", 9) == 0xcbf43926u);
    memset(config.custom_pacer_name, 'A', sizeof(config.custom_pacer_name) - 1);
    memset(config.run_custom_name, 'B', sizeof(config.run_custom_name) - 1);
    memset(config.device_name, 'C', sizeof(config.device_name) - 1);
    config.bridge.source = BRIDGE_SOURCE_REAL;
    config.bridge.device_count = BRIDGE_DEVICE_MAX;
    for (int i = 0; i < BRIDGE_DEVICE_MAX; i++) {
        config.bridge.devices[i].bound = 1;
        config.bridge.devices[i].addr_type = i % 2;
        config.bridge.devices[i].addr[5] = (uint8_t)(i + 1);
        memset(config.bridge.devices[i].name, 'D' + i, BRIDGE_NAME_MAX - 1);
    }
    for (unsigned i = 0; i < DEVICE_IDENTITY_FIELD_COUNT; i++) {
        memset(id.fields[i], 'G' + (int)i, DEVICE_IDENTITY_TEXT_MAX - 1);
        id.fields[i][DEVICE_IDENTITY_TEXT_MAX - 1] = 0;
    }
    length = config_snapshot_encode(&config, &id, 123, wire, sizeof(wire));
    CHECK(length > 0 && length < CONFIG_SNAPSHOT_MAX_LEN);
    CHECK(config_snapshot_decode(wire, length, &decoded, &out, &generation));
    CHECK(same_config(&config, &decoded) && !memcmp(&id, &out, sizeof(id)));
    memset(id.fields[0], 'X', DEVICE_IDENTITY_TEXT_MAX);
    CHECK(config_snapshot_encode(&config, &id, 123, wire, sizeof(wire)) == 0);
}

static void transaction_faults(void)
{
    s_count = 0;
    no_fault();
    config_data_t old, next, restored;
    config_store_defaults(&old);
    next = old;
    next.power = 345; next.cadence = 111; next.run_cadence = 191;
    strcpy(next.device_name, "New device");
    device_identity_t before, after, identity;
    device_identity_defaults(&before);
    device_identity_defaults(&after);
    strcpy(before.fields[0], "Old identity");
    strcpy(after.fields[0], "New identity");
    persistent_store_seed(&old, &before);
    CHECK(persistent_store_save(&old, &before));
    backup();
    no_fault();
    CHECK(persistent_store_save(&next, &after));
    int operation_count = s_operations;
    CHECK(operation_count >= 6);
    for (int mode = 1; mode <= 3; mode++) {
        for (int stage = 1; stage <= operation_count; stage++) {
            restore();
            CHECK(persistent_store_load(&restored, &identity) == PERSISTENT_STORE_OK);
            s_operations = 0; s_fault_at = stage; s_fault_mode = mode;
            s_crash_ready = true;
            if (!setjmp(s_crash)) (void)persistent_store_save(&next, &after);
            s_crash_ready = false;
            no_fault();
            CHECK(persistent_store_load(&restored, &identity) == PERSISTENT_STORE_OK);
            bool was_old = same_config(&restored, &old) && !memcmp(&identity, &before, sizeof(identity));
            bool was_new = same_config(&restored, &next) && !memcmp(&identity, &after, sizeof(identity));
            CHECK(was_old || was_new);
            if (stage <= 3) CHECK(was_old);
        }
    }
    restore();
    CHECK(persistent_store_load(&restored, &identity) == PERSISTENT_STORE_OK);
    s_operations = 0; s_fault_at = 4; s_fault_mode = 1;
    CHECK(!persistent_store_save(&next, &after));
    no_fault();
    CHECK(!persistent_store_save(&next, &after));
    CHECK(persistent_store_load(&restored, &identity) == PERSISTENT_STORE_OK);
    CHECK(persistent_store_save(&next, &after));
    CHECK(persistent_store_save_identity(&before));
    CHECK(persistent_store_load(&restored, &identity) == PERSISTENT_STORE_OK);
    CHECK(same_config(&restored, &next) && !memcmp(&identity, &before, sizeof(identity)));
    for (int i = 0; i < 1000; i++) {
        next.power = 300 + i % 100;
        CHECK(persistent_store_save(&next, &before));
        CHECK(persistent_store_load(&restored, &identity) == PERSISTENT_STORE_OK);
        CHECK(same_config(&restored, &next) && !memcmp(&identity, &before, sizeof(identity)));
    }
}

static void migration_and_integration(void)
{
    s_count = 0;
    no_fault();
    config_data_t absent;
    device_identity_t id;
    CHECK(persistent_store_load(&absent, &id) == PERSISTENT_STORE_ABSENT);
    CHECK(nvs_set_i32(1, NVS_KEY_SCHEMA_VER, 11) == ESP_OK);
    CHECK(nvs_set_i32(1, NVS_KEY_POWER, 333) == ESP_OK);
    CHECK(nvs_set_i32(1, NVS_KEY_POWER_FLOAT, 5) == ESP_OK);
    CHECK(nvs_set_i32(1, NVS_KEY_RUN_CADENCE, 191) == ESP_OK);
    device_identity_defaults(&id);
    strcpy(id.fields[0], "Legacy manufacturer");
    uint8_t wire[DEVICE_IDENTITY_WIRE_MAX];
    size_t length = device_identity_encode(&id, wire, sizeof(wire));
    CHECK(nvs_set_blob(2, NVS_IDENTITY_KEY, wire, length) == ESP_OK);
    backup();
    for (int stage = 1; stage <= 6; stage++) {
        restore();
        s_fault_key = NVS_KEY_SNAPSHOT_0;
        s_fault_mode = stage % 2 + 1;
        power_engine_t engine;
        power_engine_init(&engine);
        nvs_store_load(&engine);
        CHECK(engine.base_power == 333 && engine.power_float == 50 && engine.running.cadence_spm == 191);
        no_fault();
        power_engine_init(&engine);
        nvs_store_load(&engine);
        CHECK(engine.base_power == 333 && engine.running.cadence_spm == 191);
        device_identity_t loaded;
        device_identity_store_load(&loaded);
        CHECK(!memcmp(&loaded, &id, sizeof(id)));
        CHECK(entry(1, NVS_KEY_POWER, false) != NULL);
        power_engine_set_power(&engine, 444);
        device_identity_t updated;
        device_identity_defaults(&updated);
        strcpy(updated.fields[0], "Updated manufacturer");
        CHECK(nvs_store_save_with_identity(&engine, &updated));
        power_engine_init(&engine);
        nvs_store_load(&engine);
        device_identity_store_load(&loaded);
        CHECK(engine.base_power == 444 && !memcmp(&updated, &loaded, sizeof(loaded)));
        CHECK(device_identity_store_save(&id));
        power_engine_init(&engine);
        nvs_store_load(&engine);
        CHECK(engine.base_power == 444);
        device_identity_defaults(&updated);
        CHECK(device_identity_store_save(&updated));
        device_identity_store_load(&loaded);
        CHECK(!device_identity_has_override(&loaded));
    }
    restore();
    entry(2, NVS_IDENTITY_KEY, false)->value[0] = 99;
    power_engine_t engine;
    power_engine_init(&engine);
    nvs_store_load(&engine);
    CHECK(engine.base_power == DEFAULT_POWER);
    CHECK(!nvs_store_save(&engine));
    CHECK(entry(1, NVS_KEY_COMMIT_0, false) == NULL);
}

static void corruption_and_wrap(void)
{
    s_count = 0;
    no_fault();
    config_data_t before, after, restored;
    config_store_defaults(&before);
    after = before;
    after.power = 444;
    device_identity_t id, loaded;
    device_identity_defaults(&id);
    uint8_t wire[CONFIG_SNAPSHOT_MAX_LEN], marker[CONFIG_SNAPSHOT_MARKER_LEN];
    size_t length = config_snapshot_encode(&before, &id, UINT32_MAX, wire, sizeof(wire));
    CHECK(nvs_set_blob(1, NVS_KEY_SNAPSHOT_0, wire, length) == ESP_OK);
    config_snapshot_marker_encode(0, UINT32_MAX, marker);
    CHECK(nvs_set_blob(1, NVS_KEY_COMMIT_0, marker, sizeof(marker)) == ESP_OK);
    CHECK(persistent_store_load(&restored, &loaded) == PERSISTENT_STORE_OK);
    CHECK(persistent_store_save(&after, &id));
    CHECK(persistent_store_load(&restored, &loaded) == PERSISTENT_STORE_OK);
    CHECK(restored.power == 444);
    backup();
    entry(1, NVS_KEY_SNAPSHOT_1, false)->value[30] ^= 1;
    CHECK(persistent_store_load(&restored, &loaded) == PERSISTENT_STORE_OK);
    CHECK(restored.power == before.power);
    restore();
    entry(1, NVS_KEY_COMMIT_1, false)->value[5] ^= 1;
    CHECK(persistent_store_load(&restored, &loaded) == PERSISTENT_STORE_OK);
    CHECK(restored.power == before.power);
    restore();
    entry(1, NVS_KEY_COMMIT_0, false)->value[0] ^= 1;
    entry(1, NVS_KEY_COMMIT_1, false)->value[0] ^= 1;
    CHECK(persistent_store_load(&restored, &loaded) == PERSISTENT_STORE_INVALID);
    CHECK(!persistent_store_save(&after, &id));
    power_engine_t engine;
    power_engine_init(&engine);
    nvs_store_load(&engine);
    CHECK(engine.base_power == DEFAULT_POWER);
    CHECK(!nvs_store_save(&engine));
    restore();
    s_operations = 0; s_fault_at = 1; s_fault_mode = 1;
    CHECK(persistent_store_load(&restored, &loaded) == PERSISTENT_STORE_IO_ERROR);
    no_fault();
    CHECK(!persistent_store_save(&after, &id));
    CHECK(persistent_store_load(&restored, &loaded) == PERSISTENT_STORE_OK);
    length = config_snapshot_encode(&after, &id, 123, wire, sizeof(wire));
    CHECK(nvs_set_blob(1, NVS_KEY_SNAPSHOT_0, wire, length) == ESP_OK);
    CHECK(persistent_store_load(&restored, &loaded) == PERSISTENT_STORE_OK);
    CHECK(restored.power == after.power);
}

static void bootstrap_faults(void)
{
    s_count = 0;
    no_fault();
    config_data_t config, loaded;
    config_store_defaults(&config);
    config.power = 333;
    device_identity_t id, restored;
    device_identity_defaults(&id);
    strcpy(id.fields[0], "Legacy");
    CHECK(nvs_set_i32(1, NVS_KEY_SCHEMA_VER, CONFIG_VERSION) == ESP_OK);
    CHECK(nvs_set_i32(1, NVS_KEY_POWER, 333) == ESP_OK);
    uint8_t encoded[DEVICE_IDENTITY_WIRE_MAX];
    size_t size = device_identity_encode(&id, encoded, sizeof(encoded));
    CHECK(nvs_set_blob(2, NVS_IDENTITY_KEY, encoded, size) == ESP_OK);
    backup();
    for (int mode = 1; mode <= 3; mode++) {
        for (int stage = 1; stage <= 6; stage++) {
            restore();
            CHECK(persistent_store_load(&loaded, &restored) == PERSISTENT_STORE_ABSENT);
            persistent_store_seed(&config, &id);
            s_operations = 0; s_fault_at = stage; s_fault_mode = mode; s_crash_ready = true;
            if (!setjmp(s_crash)) (void)persistent_store_save(&config, &id);
            s_crash_ready = false;
            no_fault();
            power_engine_t engine;
            power_engine_init(&engine);
            nvs_store_load(&engine);
            device_identity_store_load(&restored);
            CHECK(engine.base_power == 333 && !memcmp(&restored, &id, sizeof(id)));
        }
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--legacy-red")) old_code_failure();
    else { snapshot_codec(); transaction_faults(); migration_and_integration(); corruption_and_wrap(); bootstrap_faults(); }
    printf("NVS 稳定性回归：%d 断言通过\n", s_assertions);
    return 0;
}
