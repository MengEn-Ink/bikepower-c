#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "portal_stubs.h"
#include "../main/wifi_portal.c"

unsigned s_lock_depth;
static int64_t s_now = 1000000;
static char s_body[20000];
static unsigned s_done_attempts;
static int64_t s_min_heap = 120000;
int64_t esp_timer_get_time(void) { return s_now; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *key, const char *value)
{ (void)req; (void)key; (void)value; assert(!s_lock_depth); return 0; }
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{ (void)req; (void)type; return 0; }
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *body)
{ (void)req; assert(!s_lock_depth); snprintf(s_body, sizeof(s_body), "%s", body); return 0; }
int ble_bikepower_conn_count(void) { return 1; }
void ble_bikepower_get_identity(device_identity_t *identity) { device_identity_defaults(identity); }
const board_desc_t *board_get_desc(void) { static const board_desc_t desc = { .name = "generic-c3" }; return &desc; }
void board_firmware_name(char *buffer, size_t size) { snprintf(buffer, size, "generic-c3-v4.2.1"); }
ble_bridge_status_t ble_power_bridge_status(const bridge_config_t *config, uint32_t now)
{
    (void)now;
    ble_bridge_status_t status = { .peer = *config };
    for (size_t i = 0; i < DEVICE_IDENTITY_FIELD_COUNT; i++) {
        memset(status.identity.fields[i], '"', DEVICE_IDENTITY_TEXT_MAX - 1);
    }
    memset(status.device_name, '\\', sizeof(status.device_name) - 1);
    return status;
}
uint32_t esp_get_free_heap_size(void) { return 120000; }
uint32_t esp_get_minimum_free_heap_size(void) { return s_min_heap; }

static bool done(void) { assert(!s_lock_depth); return ++s_done_attempts >= 2; }

static bool next_generation(void)
{
    assert(!s_lock_depth);
    portal_done();
    portENTER_CRITICAL(&s_lock);
    s_generation++;
    s_closing = false;
    s_done_called = false;
    s_done_inflight = false;
    portEXIT_CRITICAL(&s_lock);
    return true;
}

int main(void)
{
    httpd_req_t req = { 0 };
    cJSON *object = cJSON_CreateObject();
    char large[WIFI_JSON_STATE_CAPACITY + 1];
    memset(large, 'x', sizeof(large) - 1);
    large[sizeof(large) - 1] = '\0';
    cJSON_AddStringToObject(object, "large", large);
    send_object(&req, object, WIFI_JSON_STATE_CAPACITY);
    assert(strstr(s_body, "响应超限"));
    object = cJSON_Parse(s_body);
    assert(object);
    cJSON_Delete(object);
    config_store_defaults(&s_config);
    memset(s_config.device_name, '"', sizeof(s_config.device_name) - 1);
    memset(s_config.custom_pacer_name, '\\', sizeof(s_config.custom_pacer_name) - 1);
    memset(s_config.run_custom_name, '"', sizeof(s_config.run_custom_name) - 1);
    h_api_state(&req);
    object = cJSON_Parse(s_body);
    assert(object && cJSON_GetObjectItemCaseSensitive(object, "config"));
    cJSON *config = cJSON_GetObjectItemCaseSensitive(object, "config");
    assert(!strcmp(cJSON_GetObjectItemCaseSensitive(config, "device_name")->valuestring,
                   s_config.device_name));
    assert(!strstr(s_body, "响应超限"));
    cJSON_Delete(object);
    h_api_runtime(&req);
    object = cJSON_Parse(s_body);
    assert(object && cJSON_GetObjectItemCaseSensitive(object, "bridge"));
    cJSON_Delete(object);
    wifi_portal_set_done_callback(done);
    arm_deadline();
    assert(remaining_seconds() == 300);
    s_now += 100000000;
    assert(remaining_seconds() == 200);
    h_api_runtime(&req);
    assert(remaining_seconds() == 200);
    arm_deadline();
    assert(remaining_seconds() == 300);
    s_now += 300000000;
    shutdown_check_cb(NULL);
    assert(s_done_attempts == 1 && s_closing && !s_done_called);
    arm_deadline();
    assert(remaining_seconds() == 0);
    shutdown_check_cb(NULL);
    assert(s_done_attempts == 2 && s_done_called);
    shutdown_check_cb(NULL);
    assert(s_done_attempts == 2);
    wifi_portal_config_result(0, BLE_CONFIG_RESULT_BUSY, BLE_CONFIG_RESTART_GATT, NULL);
    assert(wifi_portal_consume_ble_restart_requested());
    assert(!wifi_portal_consume_ble_restart_requested());
    portENTER_CRITICAL(&s_lock);
    s_done_called = false;
    s_done_inflight = false;
    portEXIT_CRITICAL(&s_lock);
    wifi_portal_set_done_callback(next_generation);
    portal_done();
    assert(!s_done_called && !s_closing && !s_done_inflight);
    arm_deadline();
    assert(remaining_seconds() == 300);
    assert(s_lock_depth == 0);
    puts("生产门户：最大响应/超限 JSON、300 秒续期、done 重试/代次隔离与重启取清通过");
    return 0;
}
