#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bridge_stubs.h"
#include "bridge_runtime.h"

static ble_bridge_status_t s_session;
static bridge_device_t s_requests[32];
static bool s_temporary[32];
static int s_request_count;
static int s_stop_count;
static int s_scan_start_count;
static int s_scan_stop_count;
static int s_recent_save_count;
static bridge_device_t s_recent_saved;
static int s_order;
static int s_stop_order;
static int s_scan_order;
static bool s_scanning;

esp_err_t ble_power_bridge_request(const bridge_device_t *peer, bool temporary)
{
    assert(peer && peer->bound);
    assert(s_request_count < (int)(sizeof(s_requests) / sizeof(s_requests[0])));
    s_requests[s_request_count] = *peer;
    s_temporary[s_request_count] = temporary;
    s_request_count++;
    s_session = (ble_bridge_status_t){
        .state = BLE_BRIDGE_CONNECTING,
        .peer = *peer,
        .temporary = temporary,
    };
    return ESP_OK;
}

esp_err_t ble_power_bridge_request_stop(void)
{
    s_stop_count++;
    s_stop_order = ++s_order;
    s_session = (ble_bridge_status_t){.state = BLE_BRIDGE_IDLE};
    return ESP_OK;
}

void ble_power_bridge_tick(const bridge_config_t *calibration, uint32_t now_ms)
{
    (void)calibration;
    (void)now_ms;
}

ble_bridge_status_t ble_power_bridge_status(const bridge_config_t *calibration,
                                            uint32_t now_ms)
{
    (void)calibration;
    (void)now_ms;
    return s_session;
}

void ble_power_bridge_radio_stop(void) {}
void ble_power_bridge_host_reset(void) {}

esp_err_t ble_scanner_start(uint32_t duration_ms)
{
    assert(duration_ms > 0);
    s_scan_start_count++;
    s_scan_order = ++s_order;
    s_scanning = true;
    return ESP_OK;
}

esp_err_t ble_scanner_stop(void)
{
    s_scan_stop_count++;
    s_scanning = false;
    return ESP_OK;
}

bool ble_scanner_is_scanning(void) { return s_scanning; }
size_t ble_scanner_serialize_results(char *out, size_t out_sz)
{
    (void)out;
    (void)out_sz;
    return 0;
}

bool ble_scanner_candidate(uint32_t candidate, bridge_device_t *out)
{
    (void)candidate;
    (void)out;
    return false;
}

static bridge_config_t three_devices(void)
{
    bridge_config_t config;
    power_bridge_config_defaults(&config);
    config.source = BRIDGE_SOURCE_REAL;
    config.device_count = BRIDGE_DEVICE_MAX;
    for (int i = 0; i < BRIDGE_DEVICE_MAX; i++) {
        config.devices[i].bound = 1;
        config.devices[i].addr_type = i & 1;
        config.devices[i].addr[5] = (uint8_t)(0x10 + i);
        snprintf(config.devices[i].name, sizeof(config.devices[i].name),
                 "Meter-%d", i);
    }
    return config;
}

static void reset_fakes(void)
{
    memset(&s_session, 0, sizeof(s_session));
    memset(s_requests, 0, sizeof(s_requests));
    memset(s_temporary, 0, sizeof(s_temporary));
    s_request_count = 0;
    s_stop_count = 0;
    s_scan_start_count = 0;
    s_scan_stop_count = 0;
    s_recent_save_count = 0;
    memset(&s_recent_saved, 0, sizeof(s_recent_saved));
    s_order = 0;
    s_stop_order = 0;
    s_scan_order = 0;
    s_scanning = false;
}

static int requested_slot(const bridge_config_t *config, int index)
{
    assert(index >= 0 && index < s_request_count);
    return power_bridge_find_device(config, &s_requests[index]);
}

static void session_error(void)
{
    s_session.state = BLE_BRIDGE_ERROR;
    s_session.connected = false;
    s_session.sample = (bridge_snapshot_t){0};
}

static void session_valid(void)
{
    s_session.state = BLE_BRIDGE_VALID;
    s_session.connected = true;
    s_session.tested = true;
    s_session.sample = (bridge_snapshot_t){
        .valid = true,
        .raw_power_w = 200,
        .power_w = 200,
    };
}

static void save_recent(const bridge_device_t *device)
{
    assert(device && device->bound);
    s_recent_save_count++;
    s_recent_saved = *device;
}

static void automatic_failover_and_background_recovery(void)
{
    reset_fakes();
    bridge_config_t config = three_devices();
    bridge_device_t recent = config.devices[1];
    bridge_runtime_set_recent_callback(save_recent);
    bridge_runtime_init(&config, SPORT_PROFILE_CYCLING, &recent, 0);
    assert(s_request_count == 1);
    assert(requested_slot(&config, 0) == 1);
    assert(!s_temporary[0]);

    const int fast_expected[] = {2, 0, 1, 2, 0};
    for (size_t i = 0; i < sizeof(fast_expected) / sizeof(fast_expected[0]); i++) {
        session_error();
        bridge_runtime_tick(&config, SPORT_PROFILE_CYCLING,
                            (uint32_t)(i + 1) * 8000);
        assert(requested_slot(&config, s_request_count - 1)
               == fast_expected[i]);
    }
    session_error();
    bridge_runtime_tick(&config, SPORT_PROFILE_CYCLING, 56000);
    assert(bridge_runtime_uses_simulation());
    bridge_runtime_snapshot_t fallback =
        bridge_runtime_snapshot(&config, 56000);
    assert(fallback.state == BRIDGE_FAILOVER_FALLBACK_SIMULATION);
    assert(fallback.next_attempt_ms == BRIDGE_BACKGROUND_INTERVAL_MS);

    int requests_before = s_request_count;
    bridge_runtime_tick(&config, SPORT_PROFILE_CYCLING, 116000);
    assert(s_request_count == requests_before + 1);
    assert(requested_slot(&config, s_request_count - 1) == 1);
    assert(bridge_runtime_uses_simulation());
    assert(bridge_runtime_snapshot(&config, 116000).actual_simulation);

    session_error();
    bridge_runtime_tick(&config, SPORT_PROFILE_CYCLING, 124000);
    assert(bridge_runtime_uses_simulation());
    bridge_runtime_tick(&config, SPORT_PROFILE_CYCLING, 184000);
    assert(requested_slot(&config, s_request_count - 1) == 2);
    assert(bridge_runtime_uses_simulation());

    session_valid();
    bridge_runtime_tick(&config, SPORT_PROFILE_CYCLING, 185000);
    assert(!bridge_runtime_uses_simulation());
    assert(s_recent_save_count == 1);
    assert(bridge_device_equal(&s_recent_saved, &config.devices[2]));
    bridge_runtime_tick(&config, SPORT_PROFILE_CYCLING, 186000);
    assert(s_recent_save_count == 1);
}

static void manual_scan_and_test_stay_simulated(void)
{
    reset_fakes();
    bridge_config_t config = three_devices();
    bridge_device_t recent = config.devices[0];
    bridge_runtime_set_recent_callback(save_recent);
    bridge_runtime_init(&config, SPORT_PROFILE_CYCLING, &recent, 0);
    assert(s_request_count == 1);

    assert(bridge_runtime_request(BRIDGE_RUNTIME_SCAN_START, NULL) == ESP_OK);
    assert(bridge_runtime_request(BRIDGE_RUNTIME_RETRY_NOW, NULL)
           == ESP_ERR_INVALID_STATE);
    bridge_runtime_tick(&config, SPORT_PROFILE_CYCLING, 100);
    assert(s_stop_count == 1 && s_scan_start_count == 1);
    assert(s_stop_order > 0 && s_scan_order > s_stop_order);
    assert(bridge_runtime_uses_simulation());

    assert(bridge_runtime_request(BRIDGE_RUNTIME_SCAN_STOP, NULL) == ESP_OK);
    bridge_runtime_tick(&config, SPORT_PROFILE_CYCLING, 200);
    assert(s_scan_stop_count >= 1 && !s_scanning);

    bridge_device_t test_device = config.devices[2];
    test_device.addr[5] = 0x7f;
    strcpy(test_device.name, "Temporary");
    assert(bridge_runtime_request(BRIDGE_RUNTIME_TEST, &test_device) == ESP_OK);
    bridge_runtime_tick(&config, SPORT_PROFILE_CYCLING, 300);
    assert(s_temporary[s_request_count - 1]);
    assert(bridge_device_equal(&s_requests[s_request_count - 1],
                               &test_device));
    session_valid();
    bridge_runtime_tick(&config, SPORT_PROFILE_CYCLING, 400);
    assert(bridge_runtime_uses_simulation());
    assert(s_recent_save_count == 0);
    assert(config.device_count == BRIDGE_DEVICE_MAX);
    assert(bridge_device_equal(&config.devices[2], &recent) == false);

    int scans_before_close = s_scan_start_count;
    assert(bridge_runtime_request(BRIDGE_RUNTIME_SCAN_START, NULL) == ESP_OK);
    assert(bridge_runtime_request(BRIDGE_RUNTIME_RESUME, NULL) == ESP_OK);
    bridge_runtime_tick(&config, SPORT_PROFILE_CYCLING, 500);
    assert(s_scan_start_count == scans_before_close);
}

int main(void)
{
    automatic_failover_and_background_recovery();
    manual_scan_and_test_stay_simulated();
    puts("桥接运行时：最近优先/两轮降级/轮转恢复/扫描暂停/临时测试通过");
    return 0;
}
