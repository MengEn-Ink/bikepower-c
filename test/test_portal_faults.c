#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portal_stubs.h"
static void *request_malloc(size_t size);
#define malloc request_malloc
#include "../main/wifi_portal.c"
#undef malloc

unsigned s_lock_depth;
static const char *s_input;
static size_t s_offset;
static size_t s_chunk = 7;
static unsigned s_timeouts;
static unsigned s_calls;
static bool s_accept = true;
static config_data_t s_requested;
static uint32_t s_received_transaction;
static char s_body[20000];
static bool s_fail_request_alloc;

static void *request_malloc(size_t size)
{
    return s_fail_request_alloc ? NULL : malloc(size);
}

int64_t esp_timer_get_time(void) { return 1000000; }
esp_err_t httpd_resp_set_hdr(httpd_req_t *req, const char *key, const char *value)
{ (void)req; (void)key; (void)value; assert(!s_lock_depth); return 0; }
esp_err_t httpd_resp_set_type(httpd_req_t *req, const char *type)
{ (void)req; (void)type; return 0; }
esp_err_t httpd_resp_sendstr(httpd_req_t *req, const char *body)
{ (void)req; assert(!s_lock_depth); snprintf(s_body, sizeof(s_body), "%s", body); return 0; }
int httpd_req_recv(httpd_req_t *req, char *buffer, size_t length)
{
    (void)req;
    assert(!s_lock_depth);
    if (s_timeouts) { s_timeouts--; return HTTPD_SOCK_ERR_TIMEOUT; }
    size_t left = strlen(s_input) - s_offset;
    size_t count = left < length ? left : length;
    if (count > s_chunk) count = s_chunk;
    memcpy(buffer, s_input + s_offset, count);
    s_offset += count;
    return (int)count;
}
int ble_bikepower_conn_count(void) { return 1; }
void ble_bikepower_get_identity(device_identity_t *identity) { device_identity_defaults(identity); }
ble_bridge_status_t ble_power_bridge_status(const bridge_config_t *config, uint32_t now)
{ (void)now; return (ble_bridge_status_t){ .peer = *config }; }

static bool accepted(const config_data_t *config, uint32_t transaction, uint32_t revision,
                     bool close_wifi, const device_identity_t *identity)
{
    (void)close_wifi; (void)identity;
    assert(!s_lock_depth && revision == s_revision);
    s_calls++;
    s_requested = *config;
    s_received_transaction = transaction;
    return s_accept;
}

static void reset(void)
{
    config_store_defaults(&s_config);
    memset(&s_transaction, 0, sizeof(s_transaction));
    s_revision = 1;
    s_closing = false;
    s_calls = 0;
    s_timeouts = 0;
    s_accept = true;
    s_fail_request_alloc = false;
    wifi_portal_set_config_callback(accepted);
    arm_deadline();
}

static bool submit(const char *text, bool close_wifi, size_t extra_length)
{
    s_input = text;
    s_offset = 0;
    httpd_req_t req = { .content_len = strlen(text) + extra_length, .method = HTTP_POST };
    submit_config(&req, close_wifi);
    cJSON *response = cJSON_Parse(s_body);
    assert(response);
    bool ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "ok"));
    cJSON_Delete(response);
    return ok;
}

static void runtime(bool pending, int result, uint32_t revision)
{
    httpd_req_t req = { 0 };
    h_api_runtime(&req);
    cJSON *response = cJSON_Parse(s_body);
    assert(response);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(response, "pending")) == pending);
    assert(cJSON_GetObjectItemCaseSensitive(response, "result")->valueint == result);
    assert(cJSON_GetObjectItemCaseSensitive(response, "revision")->valuedouble == revision);
    cJSON_Delete(response);
}

int main(void)
{
    const char *invalid[] = { "", "[]", "null", "{", "{} trailing", "{\"revision\":0}",
        "{\"revision\":2}", "{\"revision\":\"1\"}", "{\"revision\":1,\"power\":\"300\"}",
        "{\"revision\":1,\"power\":300.5}", "{\"revision\":1,\"device_name\":\"bad\\nname\"}",
        "{\"revision\":4294967296}" };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        reset();
        assert(!submit(invalid[i], false, 0));
        assert(!s_calls && !s_transaction.pending && s_revision == 1);
    }
    reset();
    s_fail_request_alloc = true;
    assert(!submit("{\"revision\":1}", false, 0));
    assert(!s_calls && !s_transaction.pending);
    s_fail_request_alloc = false;
    assert(!submit("{\"revision\":1}", false, HTTP_MAX_BODY_LEN));
    assert(!s_calls);
    assert(!submit("{\"revision\":1}", false, 3));
    assert(!s_calls);
    s_timeouts = HTTP_RECV_TIMEOUT_RETRIES + 1;
    assert(!submit("{\"revision\":1}", false, 0));
    assert(!s_calls);
    s_timeouts = HTTP_RECV_TIMEOUT_RETRIES;
    assert(submit("{\"revision\":1,\"power\":300}", false, 0));
    assert(s_calls == 1 && s_requested.power == 300 && s_config.power != 300);
    runtime(true, BLE_CONFIG_RESULT_BUSY, 1);
    assert(!submit("{\"revision\":1,\"power\":320}", false, 0));
    assert(s_calls == 1);
    wifi_portal_config_result(s_received_transaction + 1, BLE_CONFIG_RESULT_SUCCESS, 0, &s_requested);
    runtime(true, BLE_CONFIG_RESULT_BUSY, 1);
    wifi_portal_config_result(s_received_transaction, BLE_CONFIG_RESULT_SAVE_FAILED, 0, NULL);
    runtime(false, BLE_CONFIG_RESULT_SAVE_FAILED, 1);
    assert(s_config.power != 300);
    assert(submit("{\"revision\":1,\"power\":320}", false, 0));
    wifi_portal_config_result(s_received_transaction, BLE_CONFIG_RESULT_SUCCESS, 0, &s_requested);
    runtime(false, BLE_CONFIG_RESULT_SUCCESS, 2);
    assert(s_config.power == 320);
    wifi_portal_config_result(s_received_transaction, BLE_CONFIG_RESULT_SUCCESS, 0, &s_requested);
    assert(s_revision == 2);
    assert(!submit("{\"revision\":1}", false, 0));
    reset();
    s_accept = false;
    assert(!submit("{\"revision\":1}", true, 0));
    assert(!s_transaction.pending && !s_closing);
    s_accept = true;
    assert(submit("{\"revision\":1}", true, 0));
    wifi_portal_config_result(s_received_transaction, BLE_CONFIG_RESULT_SAVE_FAILED, 0, NULL);
    assert(!s_closing && remaining_seconds() == 300);
    assert(submit("{\"revision\":1}", true, 0));
    wifi_portal_config_result(s_received_transaction, BLE_CONFIG_RESULT_SUCCESS, 0, &s_requested);
    assert(s_closing && remaining_seconds() == WIFI_PORTAL_REBOOT_DELAY_SEC);
    assert(!submit("{\"revision\":2}", false, 0));
    reset();
    assert(!submit("{\"revision\":1.5,\"power\":300}", false, 0));
    assert(!s_calls && s_revision == 1);
    assert(submit("{\"revision\":1}", false, 0));
    wifi_portal_config_result(s_received_transaction, BLE_CONFIG_RESULT_SUCCESS, 0, NULL);
    runtime(false, BLE_CONFIG_RESULT_SAVE_FAILED, 1);
    assert(submit("{\"revision\":1}", true, 0));
    wifi_portal_config_result(s_received_transaction, BLE_CONFIG_RESULT_SUCCESS, 0, NULL);
    assert(!s_closing && s_revision == 1);
    assert(!s_lock_depth);
    puts("生产门户异常：请求内存不足、非法/截断/分段/超时输入、忙拒收、过期 revision、串事务、保存失败/重试、关闭及空成功结果通过");
    return 0;
}
