#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include "bikepower_config.h"
#include "ble_bikepower.h"
#include "ble_config_codec.h"
#include "ble_scanner.h"
#include "board_detect.h"
#include "captive_portal.h"
#include "device_identity_store.h"
#include "profile.h"
#include "web_index_html.h"
#include "wifi_portal.h"

static const char *TAG = "PORTAL";
static httpd_handle_t s_httpd;
static esp_timer_handle_t s_timer;
// 超时/关闭/事务由 HTTP、定时器及主任务共享，全部通过 s_lock 读写。
static uint32_t s_deadline_sec;
static bool s_closing;
static bool s_done_called;
static bool s_done_inflight;
static uint32_t s_generation;
static bool s_done_retry_logged;
static bool s_ble_restart_requested;
static wifi_portal_done_callback_t s_done_cb;
static wifi_portal_config_callback_t s_config_cb;
static wifi_portal_bridge_callback_t s_bridge_cb;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static config_data_t s_config;
static snapshot_t s_output;
static running_snapshot_t s_running;
static bridge_runtime_snapshot_t s_bridge_runtime = {
    .active_slot = -1,
    .candidate_slot = -1,
};
static uint32_t s_revision = 1;
static uint32_t s_next_transaction;
// 仅 HTTP 服务任务使用；构造、合并与发送串行复用，不在定时器或主任务中访问。
static char s_json_response[WIFI_JSON_STATE_CAPACITY];
_Static_assert(WIFI_JSON_STATE_CAPACITY >= WIFI_JSON_RUNTIME_CAPACITY, "runtime 容量超出共享区");
_Static_assert(WIFI_JSON_STATE_CAPACITY >= WIFI_JSON_SCAN_CAPACITY, "scan 容量超出共享区");
_Static_assert(WIFI_JSON_STATE_CAPACITY >= CONFIG_JSON_MAX_LEN, "config 容量超出共享区");
static struct {
    uint32_t id;
    uint8_t result;
    uint8_t restart;
    bool pending;
    bool close_wifi;
} s_transaction;

static uint32_t now_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static int remaining_seconds(void)
{
    uint32_t now = now_sec();
    portENTER_CRITICAL(&s_lock);
    uint32_t deadline = s_deadline_sec;
    portEXIT_CRITICAL(&s_lock);
    int32_t remaining = (int32_t)(deadline - now);
    return remaining > 0 ? remaining : 0;
}

static bool portal_closing(void)
{
    portENTER_CRITICAL(&s_lock);
    bool closing = s_closing;
    portEXIT_CRITICAL(&s_lock);
    return closing;
}

static void arm_deadline(void)
{
    uint32_t deadline = now_sec() + WIFI_SHUTDOWN_MS / 1000;
    portENTER_CRITICAL(&s_lock);
    if (!s_closing) s_deadline_sec = deadline;
    portEXIT_CRITICAL(&s_lock);
}

static void portal_done(void)
{
    portENTER_CRITICAL(&s_lock);
    if (s_done_called || s_done_inflight) {
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    s_closing = true;
    s_done_inflight = true;
    uint32_t generation = s_generation;
    portEXIT_CRITICAL(&s_lock);
    bool accepted = s_done_cb && s_done_cb();
    bool log_retry = false;
    portENTER_CRITICAL(&s_lock);
    if (generation != s_generation) {
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    s_done_inflight = false;
    if (accepted) {
        s_done_called = true;
    } else if (!s_done_retry_logged) {
        s_done_retry_logged = true;
        log_retry = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (log_retry) ESP_LOGW(TAG, "配网结束事件未接收，下个节拍重试");
}

static void shutdown_check_cb(void *arg)
{
    (void)arg;
    if (!remaining_seconds()) portal_done();
}

static esp_err_t send_json(httpd_req_t *req, const char *body)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t send_object(httpd_req_t *req, cJSON *object, size_t capacity)
{
    if (!object) return send_json(req, "{\"ok\":false,\"error\":\"内存不足\"}");
    bool ok = capacity <= sizeof(s_json_response)
        && cJSON_PrintPreallocated(object, s_json_response, (int)capacity, false);
    cJSON_Delete(object);
    if (!ok) return send_json(req, "{\"ok\":false,\"error\":\"响应超限\"}");
    return send_json(req, s_json_response);
}

static cJSON *receive_object(httpd_req_t *req)
{
    if (!req->content_len || req->content_len > HTTP_MAX_BODY_LEN) return NULL;
    char *text = malloc(req->content_len + 1);
    if (!text) return NULL;
    size_t used = 0;
    unsigned retries = 0;
    while (used < req->content_len) {
        int n = httpd_req_recv(req, text + used, req->content_len - used);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && retries++ < HTTP_RECV_TIMEOUT_RETRIES) continue;
        if (n <= 0) { free(text); return NULL; }
        used += n;
        retries = 0;
    }
    text[used] = '\0';
    const char *end = NULL;
    cJSON *object = cJSON_ParseWithOpts(text, &end, true);
    free(text);
    if (!cJSON_IsObject(object)) {
        cJSON_Delete(object);
        return NULL;
    }
    return object;
}

static cJSON *config_object(const config_data_t *config)
{
    if (!config_store_serialize(config, s_json_response, CONFIG_JSON_MAX_LEN)) return NULL;
    return cJSON_Parse(s_json_response);
}

void wifi_portal_sync(const power_engine_t *engine)
{
    config_data_t config;
    config_store_from_engine(engine, &config);
    portENTER_CRITICAL(&s_lock);
    if (memcmp(&s_config, &config, sizeof(config))) {
        s_config = config;
        s_revision++;
        if (!s_revision) s_revision = 1;
    }
    s_output = engine->snapshot;
    s_running = engine->running.snapshot;
    if (engine->sport_profile == SPORT_PROFILE_RUNNING) {
        s_output.cadence = s_running.cadence_spm;
    }
    portEXIT_CRITICAL(&s_lock);
}

void wifi_portal_sync_bridge(const bridge_runtime_snapshot_t *snapshot)
{
    if (!snapshot) return;
    portENTER_CRITICAL(&s_lock);
    s_bridge_runtime = *snapshot;
    portEXIT_CRITICAL(&s_lock);
}

bool wifi_portal_revision_matches(uint32_t revision)
{
    portENTER_CRITICAL(&s_lock);
    bool matches = revision == s_revision;
    portEXIT_CRITICAL(&s_lock);
    return matches;
}

void wifi_portal_config_result(uint32_t transaction, uint8_t result,
                               uint8_t restart_flags, const config_data_t *applied)
{
    uint32_t deadline = now_sec() + WIFI_PORTAL_REBOOT_DELAY_SEC;
    portENTER_CRITICAL(&s_lock);
    if (s_transaction.pending && s_transaction.id == transaction) {
        if (result == BLE_CONFIG_RESULT_SUCCESS && !applied) result = BLE_CONFIG_RESULT_SAVE_FAILED;
        if (result == BLE_CONFIG_RESULT_SUCCESS && applied) {
            s_config = *applied;
            s_revision++;
            if (s_transaction.close_wifi) {
                s_closing = true;
                s_deadline_sec = deadline;
            }
        }
        s_transaction.pending = false;
        s_transaction.result = result;
        s_transaction.restart = restart_flags;
    }
    if (restart_flags & BLE_CONFIG_RESTART_GATT) s_ble_restart_requested = true;
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t h_home(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, index_html, index_html_len);
}

static esp_err_t h_favicon(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static cJSON *bridge_object(const ble_bridge_status_t *status)
{
    cJSON *object = cJSON_CreateObject();
    if (!object || !status) return object;
    cJSON_AddNumberToObject(object, "state", status->state);
    cJSON_AddBoolToObject(object, "connected", status->connected);
    cJSON_AddBoolToObject(object, "temporary", status->temporary);
    cJSON_AddBoolToObject(object, "tested", status->tested);
    cJSON_AddBoolToObject(object, "valid", status->sample.valid);
    cJSON_AddNumberToObject(object, "raw_power", status->sample.raw_power_w);
    cJSON_AddNumberToObject(object, "power", status->sample.power_w);
    cJSON_AddNumberToObject(object, "cadence",
                            status->sample.cadence_half_rpm / 2.0);
    cJSON_AddNumberToObject(object, "age_ms", status->sample.age_ms);
    cJSON_AddNumberToObject(object, "error", status->error);
    cJSON_AddStringToObject(object, "device_name", status->device_name);
    char address[13];
    for (size_t i = 0; i < 6; i++) {
        snprintf(address + i * 2, 3, "%02x", status->peer.addr[i]);
    }
    cJSON_AddStringToObject(object, "address", address);
    cJSON_AddNumberToObject(object, "addr_type", status->peer.addr_type);
    cJSON_AddStringToObject(object, "name", status->peer.name);
    cJSON *identity = cJSON_AddObjectToObject(object, "identity");
    static const char *keys[] = { "manufacturer", "model", "serial", "hardware", "firmware", "software" };
    for (size_t i = 0; i < DEVICE_IDENTITY_FIELD_COUNT; i++) {
        cJSON_AddStringToObject(identity, keys[i], status->identity.fields[i]);
    }
    return object;
}

static const char *bridge_runtime_state_name(bridge_failover_state_t state)
{
    static const char *names[] = {
        "disabled",
        "fast_recovery",
        "live",
        "fallback_simulation",
        "background_probe",
        "paused",
    };
    return state >= BRIDGE_FAILOVER_DISABLED && state <= BRIDGE_FAILOVER_PAUSED
        ? names[state] : "disabled";
}

static cJSON *bridge_runtime_object(
    const bridge_runtime_snapshot_t *snapshot)
{
    cJSON *object = cJSON_CreateObject();
    if (!object || !snapshot) return object;
    cJSON_AddStringToObject(object, "state",
                            bridge_runtime_state_name(snapshot->state));
    cJSON_AddBoolToObject(object, "configured_real",
                          snapshot->configured_real);
    cJSON_AddStringToObject(object, "actual_source",
                            snapshot->actual_simulation
                                ? "simulation" : "real");
    cJSON_AddNumberToObject(object, "active_slot", snapshot->active_slot);
    cJSON_AddNumberToObject(object, "candidate_slot",
                            snapshot->candidate_slot);
    cJSON_AddNumberToObject(object, "next_attempt_ms",
                            snapshot->next_attempt_ms);
    return object;
}

static esp_err_t h_api_state(httpd_req_t *req)
{
    portENTER_CRITICAL(&s_lock);
    config_data_t config = s_config;
    uint32_t revision = s_revision;
    bridge_runtime_snapshot_t bridge = s_bridge_runtime;
    portEXIT_CRITICAL(&s_lock);
    cJSON *object = cJSON_CreateObject();
    if (!object) return send_object(req, NULL, WIFI_JSON_STATE_CAPACITY);
    cJSON_AddItemToObject(object, "config", config_object(&config));
    cJSON_AddNumberToObject(object, "revision", revision);
    cJSON_AddStringToObject(object, "firmware_version", FIRMWARE_VERSION);
    char name[64];
    board_firmware_name(name, sizeof(name));
    cJSON_AddStringToObject(object, "firmware_name", name);
    cJSON_AddStringToObject(object, "board", board_get_desc()->name);
    cJSON_AddStringToObject(object, "git_sha", BIKEPOWER_GIT_SHA);
    cJSON_AddStringToObject(object, "build_time", BIKEPOWER_BUILD_TIME);
    device_identity_t identity;
    ble_bikepower_get_identity(&identity);
    cJSON *identity_json = cJSON_AddObjectToObject(object, "identity");
    static const char *identity_keys[] = { "manufacturer", "model", "serial", "hardware", "firmware", "software" };
    for (size_t i = 0; i < DEVICE_IDENTITY_FIELD_COUNT; i++) {
        cJSON_AddStringToObject(identity_json, identity_keys[i],
            device_identity_effective(&identity, (device_identity_field_t)i, NULL));
    }
    cJSON_AddNumberToObject(object, "remaining_sec", remaining_seconds());
    cJSON_AddBoolToObject(object, "closing", portal_closing());
    cJSON *pacers = cJSON_AddArrayToObject(object, "pacers");
    for (int i = 0; i < PACER_COUNT; i++) {
        cJSON *pacer = cJSON_CreateObject();
        cJSON_AddNumberToObject(pacer, "id", i);
        cJSON_AddStringToObject(pacer, "name", ROAD_PACERS[i].name);
        cJSON_AddNumberToObject(pacer, "low", ROAD_PACERS[i].wkg_low);
        cJSON_AddNumberToObject(pacer, "high", ROAD_PACERS[i].wkg_high);
        cJSON_AddItemToArray(pacers, pacer);
    }
    cJSON *run_pacers = cJSON_AddArrayToObject(object, "run_pacers");
    for (int i = 0; i < RUN_PACER_PRESET_COUNT; i++) {
        cJSON *pacer = cJSON_CreateObject();
        cJSON_AddNumberToObject(pacer, "id", i);
        cJSON_AddStringToObject(pacer, "name", RUN_PACERS[i].name);
        cJSON_AddNumberToObject(pacer, "low", RUN_PACERS[i].pace_low_sec);
        cJSON_AddNumberToObject(pacer, "high", RUN_PACERS[i].pace_high_sec);
        cJSON_AddNumberToObject(pacer, "cadence", RUN_PACERS[i].cadence_spm);
        cJSON_AddItemToArray(run_pacers, pacer);
    }
    cJSON_AddItemToObject(object, "bridge", bridge_object(&bridge.session));
    return send_object(req, object, WIFI_JSON_STATE_CAPACITY);
}

static esp_err_t h_api_runtime(httpd_req_t *req)
{
    portENTER_CRITICAL(&s_lock);
    config_data_t config = s_config;
    snapshot_t output = s_output;
    running_snapshot_t running = s_running;
    uint32_t revision = s_revision;
    uint32_t transaction = s_transaction.id;
    bool pending = s_transaction.pending;
    uint8_t result = s_transaction.result;
    uint8_t restart = s_transaction.restart;
    bridge_runtime_snapshot_t bridge = s_bridge_runtime;
    portEXIT_CRITICAL(&s_lock);
    cJSON *object = cJSON_CreateObject();
    if (!object) return send_object(req, NULL, WIFI_JSON_RUNTIME_CAPACITY);
    cJSON_AddNumberToObject(object, "revision", revision);
    cJSON_AddNumberToObject(object, "transaction", transaction);
    cJSON_AddBoolToObject(object, "pending", pending);
    cJSON_AddNumberToObject(object, "result", result);
    cJSON_AddNumberToObject(object, "restart", restart);
    cJSON_AddNumberToObject(object, "source", config.bridge.source);
    cJSON_AddNumberToObject(object, "power", output.power);
    cJSON_AddNumberToObject(object, "cadence", output.cadence);
    cJSON_AddNumberToObject(object, "heartrate", output.heartrate);
    cJSON_AddNumberToObject(object, "run_speed_x100", running.speed_x100_kmh);
    cJSON_AddNumberToObject(object, "run_distance_m", running.total_distance_m);
    cJSON_AddNumberToObject(object, "run_elapsed_sec", running.elapsed_time_s);
    cJSON_AddNumberToObject(object, "connections", ble_bikepower_conn_count());
    cJSON_AddNumberToObject(object, "remaining_sec", remaining_seconds());
    cJSON_AddBoolToObject(object, "closing", portal_closing());
    cJSON_AddItemToObject(object, "bridge", bridge_object(&bridge.session));
    cJSON_AddItemToObject(object, "bridge_runtime",
                          bridge_runtime_object(&bridge));
    return send_object(req, object, WIFI_JSON_RUNTIME_CAPACITY);
}

static bool merge_config(cJSON *body, const config_data_t *base, config_data_t *out)
{
    cJSON *object = config_object(base);
    if (!object) return false;
    cJSON *field;
    cJSON_ArrayForEach(field, body) {
        cJSON *current = cJSON_GetObjectItemCaseSensitive(object, field->string);
        if (!current) continue;
        if ((cJSON_IsNumber(current) && (!cJSON_IsNumber(field) || field->valuedouble != field->valueint))
                || (cJSON_IsString(current) && !cJSON_IsString(field))) {
            cJSON_Delete(object);
            return false;
        }
        if (cJSON_IsString(field)) {
            size_t limit = !strcmp(field->string, "device_name") ? DEVICE_NAME_MAX - 1
                : !strcmp(field->string, "bridge_address") ? 12 : 31;
            size_t length = strlen(field->valuestring);
            if (length > limit || !device_identity_text_valid((const uint8_t *)field->valuestring, length)) {
                cJSON_Delete(object);
                return false;
            }
        }
        cJSON *copy = cJSON_Duplicate(field, true);
        if (!copy || !cJSON_ReplaceItemInObjectCaseSensitive(object, field->string, copy)) {
            cJSON_Delete(copy);
            cJSON_Delete(object);
            return false;
        }
    }
    bool printed = cJSON_PrintPreallocated(object, s_json_response, CONFIG_JSON_MAX_LEN, false);
    cJSON_Delete(object);
    bool ok = printed && config_store_deserialize(s_json_response, out)
        && out->schema_ver == CONFIG_VERSION;
    return ok;
}

static int added_bridge_devices(const bridge_config_t *base,
                                const bridge_config_t *requested,
                                bridge_device_t *added)
{
    int count = 0;
    for (int i = 0; i < requested->device_count; i++) {
        if (power_bridge_find_device(base, &requested->devices[i]) < 0) {
            if (added && count == 0) added[0] = requested->devices[i];
            count++;
        }
    }
    return count;
}

static bool replacement_slot_valid(cJSON *body,
                                   const bridge_config_t *base,
                                   const bridge_config_t *requested,
                                   int added_count)
{
    if (base->device_count < BRIDGE_DEVICE_MAX || added_count == 0) return true;
    cJSON *slot = cJSON_GetObjectItemCaseSensitive(body, "replace_slot");
    if (added_count != 1 || !cJSON_IsNumber(slot)
        || slot->valuedouble != slot->valueint || slot->valueint < 0
        || slot->valueint >= base->device_count) {
        return false;
    }
    return power_bridge_find_device(requested,
                                    &base->devices[slot->valueint]) < 0;
}

static esp_err_t submit_config(httpd_req_t *req, bool close_wifi)
{
    if (portal_closing() || !remaining_seconds()) return send_json(req, "{\"ok\":false,\"error\":\"配网已结束\"}");
    cJSON *body = receive_object(req);
    if (!body) return send_json(req, "{\"ok\":false,\"error\":\"无效请求\"}");
    portENTER_CRITICAL(&s_lock);
    config_data_t base = s_config;
    bool busy = s_transaction.pending;
    bridge_runtime_snapshot_t bridge = s_bridge_runtime;
    portEXIT_CRITICAL(&s_lock);
    cJSON *revision_field = cJSON_GetObjectItemCaseSensitive(body, "revision");
    uint32_t revision = cJSON_IsNumber(revision_field) && revision_field->valuedouble > 0
        && revision_field->valuedouble <= UINT32_MAX
        && revision_field->valuedouble == (double)(uint32_t)revision_field->valuedouble
        ? (uint32_t)revision_field->valuedouble : 0;
    config_data_t requested;
    bool ok = !busy && revision && wifi_portal_revision_matches(revision)
        && merge_config(body, &base, &requested);
    device_identity_t identity;
    device_identity_t *identity_ptr = NULL;
    bool copy_identity = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body, "copy_identity"));
    bridge_device_t added = {0};
    int added_count = ok
        ? added_bridge_devices(&base.bridge, &requested.bridge, &added) : 0;
    bool tested_new = added_count == 1 && bridge.session.temporary
        && bridge.session.tested
        && bridge_device_equal(&bridge.session.peer, &added);
    if (ok && !replacement_slot_valid(body, &base.bridge,
                                       &requested.bridge, added_count)) {
        ok = false;
    }
    if (ok && added_count > 0 && !tested_new) ok = false;
    if (ok && copy_identity) {
        if (!tested_new) {
            ok = false;
        } else {
            identity = bridge.session.identity;
            identity_ptr = &identity;
        }
    }
    cJSON_Delete(body);
    if (!ok || !s_config_cb) {
        return send_json(req, "{\"ok\":false,\"error\":\"配置已变化、参数无效或功率计尚未通过测试\"}");
    }
    portENTER_CRITICAL(&s_lock);
    uint32_t transaction = ++s_next_transaction;
    if (!transaction) transaction = ++s_next_transaction;
    s_transaction.id = transaction;
    s_transaction.pending = true;
    s_transaction.close_wifi = close_wifi;
    s_transaction.result = BLE_CONFIG_RESULT_BUSY;
    portEXIT_CRITICAL(&s_lock);
    if (!s_config_cb(&requested, transaction, revision, close_wifi, identity_ptr)) {
        wifi_portal_config_result(transaction, BLE_CONFIG_RESULT_BUSY, 0, NULL);
        return send_json(req, "{\"ok\":false,\"error\":\"设备忙，请稍后重试\"}");
    }
    char response[80];
    snprintf(response, sizeof(response), "{\"ok\":true,\"pending\":true,\"transaction\":%lu}", (unsigned long)transaction);
    return send_json(req, response);
}

static esp_err_t h_api_config(httpd_req_t *req) { return submit_config(req, false); }
static esp_err_t h_api_close_wifi(httpd_req_t *req) { return submit_config(req, true); }

static esp_err_t h_api_scan(httpd_req_t *req)
{
    if (req->method == HTTP_POST) {
        if (portal_closing() || !remaining_seconds()) return send_json(req, "{\"ok\":false,\"error\":\"配网已结束\"}");
        cJSON *body = receive_object(req);
        if (!body) return send_json(req, "{\"ok\":false,\"error\":\"无效请求\"}");
        cJSON *stop = cJSON_GetObjectItemCaseSensitive(body, "stop");
        bridge_runtime_command_t command = cJSON_IsTrue(stop)
            ? BRIDGE_RUNTIME_SCAN_STOP : BRIDGE_RUNTIME_SCAN_START;
        esp_err_t result = s_bridge_cb
            ? s_bridge_cb(command, NULL) : ESP_ERR_INVALID_STATE;
        cJSON_Delete(body);
        if (result != ESP_OK) return send_json(req, "{\"ok\":false,\"error\":\"扫描忙或蓝牙未就绪\"}");
        return send_json(req, "{\"ok\":true,\"pending\":true}");
    }
    if (!ble_scanner_serialize_results(s_json_response, WIFI_JSON_SCAN_CAPACITY)) {
        return send_json(req, "{\"ok\":false,\"error\":\"扫描结果超限\"}");
    }
    return send_json(req, s_json_response);
}

static esp_err_t h_api_bridge_control(httpd_req_t *req)
{
    if (portal_closing() || !remaining_seconds()) return send_json(req, "{\"ok\":false,\"error\":\"配网已结束\"}");
    cJSON *body = receive_object(req);
    cJSON *action = cJSON_GetObjectItemCaseSensitive(body, "action");
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(action) && strcmp(action->valuestring, "stop") == 0) {
        result = s_bridge_cb
            ? s_bridge_cb(BRIDGE_RUNTIME_STOP, NULL) : ESP_ERR_INVALID_STATE;
    } else if (cJSON_IsString(action) && strcmp(action->valuestring, "test") == 0) {
        cJSON *candidate = cJSON_GetObjectItemCaseSensitive(body, "candidate");
        bridge_device_t peer;
        if (cJSON_IsNumber(candidate) && candidate->valuedouble >= 0
                && candidate->valuedouble <= INT32_MAX
                && ble_scanner_candidate((uint32_t)candidate->valuedouble, &peer)) {
            result = s_bridge_cb
                ? s_bridge_cb(BRIDGE_RUNTIME_TEST, &peer)
                : ESP_ERR_INVALID_STATE;
        }
    } else if (cJSON_IsString(action) && strcmp(action->valuestring, "resume") == 0) {
        result = s_bridge_cb
            ? s_bridge_cb(BRIDGE_RUNTIME_RESUME, NULL)
            : ESP_ERR_INVALID_STATE;
    } else if (cJSON_IsString(action)
               && strcmp(action->valuestring, "retry") == 0) {
        result = s_bridge_cb
            ? s_bridge_cb(BRIDGE_RUNTIME_RETRY_NOW, NULL)
            : ESP_ERR_INVALID_STATE;
    }
    cJSON_Delete(body);
    return send_json(req, result == ESP_OK ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"候选已失效或设备忙\"}");
}

static esp_err_t h_api_time(httpd_req_t *req)
{
    char text[16];
    snprintf(text, sizeof(text), "%d", remaining_seconds());
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, text);
}

static esp_err_t h_api_renew(httpd_req_t *req) { arm_deadline(); return h_api_time(req); }

static esp_err_t h_api_mem_stats(httpd_req_t *req)
{
    char text[96];
    snprintf(text, sizeof(text), "{\"free\":%u,\"min_free\":%u}",
        (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
    return send_json(req, text);
}

static esp_err_t h_captive_api(httpd_req_t *req)
{
    return send_json(req, portal_closing() ? "{\"captive\":false}"
        : "{\"captive\":true,\"user-portal-url\":\"http://" CAPTIVE_PORTAL_HOST "/\"}");
}

static esp_err_t h_not_found(httpd_req_t *req, httpd_err_code_t error)
{
    (void)error;
    return captive_portal_detect_handler(req);
}

esp_err_t wifi_portal_start(power_engine_t *engine)
{
    if (s_httpd) return ESP_ERR_INVALID_STATE;
    wifi_portal_sync(engine);
    portENTER_CRITICAL(&s_lock);
    s_generation++;
    s_closing = s_done_called = s_done_retry_logged = s_done_inflight = false;
    portEXIT_CRITICAL(&s_lock);
    arm_deadline();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WIFI_PORT;
    config.lru_purge_enable = true;
    config.max_uri_handlers = HTTP_MAX_URI_HANDLERS;
    config.stack_size = TASK_STACK_HTTPD;
    config.max_open_sockets = HTTP_MAX_OPEN_SOCKETS;
    config.keep_alive_enable = false;
    esp_err_t result = httpd_start(&s_httpd, &config);
    if (result != ESP_OK) { portal_done(); return result; }
    httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = h_home },
        { .uri = "/favicon.ico", .method = HTTP_GET, .handler = h_favicon },
        { .uri = CAPTIVE_PORTAL_API_PATH, .method = HTTP_GET, .handler = h_captive_api },
        { .uri = "/api/state", .method = HTTP_GET, .handler = h_api_state },
        { .uri = "/api/runtime", .method = HTTP_GET, .handler = h_api_runtime },
        { .uri = "/api/config", .method = HTTP_POST, .handler = h_api_config },
        { .uri = "/api/close-wifi", .method = HTTP_POST, .handler = h_api_close_wifi },
        { .uri = "/api/bridge/scan", .method = HTTP_POST, .handler = h_api_scan },
        { .uri = "/api/bridge/scan", .method = HTTP_GET, .handler = h_api_scan },
        { .uri = "/api/bridge/control", .method = HTTP_POST, .handler = h_api_bridge_control },
        { .uri = "/api/time", .method = HTTP_GET, .handler = h_api_time },
        { .uri = "/api/renew", .method = HTTP_POST, .handler = h_api_renew },
        { .uri = "/api/mem_stats", .method = HTTP_GET, .handler = h_api_mem_stats },
        { .uri = "/generate_204", .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/gen_204", .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/library/test/success.html", .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/canonical.html", .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/success.txt", .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/redirect", .method = HTTP_GET, .handler = captive_portal_detect_handler },
        { .uri = "/kindle-wifi/wifistub.html", .method = HTTP_GET, .handler = captive_portal_detect_handler },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        result = httpd_register_uri_handler(s_httpd, &routes[i]);
        if (result != ESP_OK) break;
    }
    if (result == ESP_OK) result = httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, h_not_found);
    const esp_timer_create_args_t timer = { .callback = shutdown_check_cb, .name = "portal_to" };
    if (result == ESP_OK) result = esp_timer_create(&timer, &s_timer);
    if (result == ESP_OK) result = esp_timer_start_periodic(s_timer, WIFI_PORTAL_CHECK_INTERVAL_US);
    if (result != ESP_OK) { wifi_portal_stop(); portal_done(); }
    else ESP_LOGI(TAG, "配网就绪：%u 秒，轮询不续期", WIFI_SHUTDOWN_MS / 1000);
    return result;
}

void wifi_portal_stop(void)
{
    if (s_bridge_cb) s_bridge_cb(BRIDGE_RUNTIME_RESUME, NULL);
    if (s_timer) { esp_timer_stop(s_timer); esp_timer_delete(s_timer); s_timer = NULL; }
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = NULL; }
    portENTER_CRITICAL(&s_lock);
    s_generation++;
    s_closing = s_done_called = s_done_retry_logged = s_done_inflight = false;
    portEXIT_CRITICAL(&s_lock);
}

void wifi_portal_set_done_callback(wifi_portal_done_callback_t cb) { s_done_cb = cb; }
void wifi_portal_set_config_callback(wifi_portal_config_callback_t cb) { s_config_cb = cb; }
void wifi_portal_set_bridge_callback(wifi_portal_bridge_callback_t cb) { s_bridge_cb = cb; }
bool wifi_portal_ble_restart_requested(void)
{
    portENTER_CRITICAL(&s_lock);
    bool requested = s_ble_restart_requested;
    portEXIT_CRITICAL(&s_lock);
    return requested;
}
bool wifi_portal_consume_ble_restart_requested(void)
{
    portENTER_CRITICAL(&s_lock);
    bool requested = s_ble_restart_requested;
    s_ble_restart_requested = false;
    portEXIT_CRITICAL(&s_lock);
    return requested;
}
