#include <string.h>

#include "freertos/FreeRTOS.h"

#include "bikepower_config.h"
#include "ble_scanner.h"
#include "bridge_runtime.h"

typedef struct {
    bool pending;
    bridge_runtime_command_t command;
    bridge_device_t device;
} runtime_mailbox_t;

typedef struct {
    bridge_failover_t failover;
    bridge_config_t config;
    bridge_device_t recent;
    int sport_profile;
    int candidate_slot;
    bool initialized;
    bool recent_valid;
    bool manual;
    bool scan_pending;
    bool request_pending;
    bool request_temporary;
    bool error_reported;
    bool valid_reported;
    bridge_device_t request_device;
} runtime_context_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static runtime_mailbox_t s_mailbox;
static runtime_context_t s_runtime;
static bridge_runtime_recent_callback_t s_recent_callback;

static bool configured_real(const bridge_config_t *config, int sport_profile)
{
    return config && sport_profile == SPORT_PROFILE_CYCLING
        && config->source == BRIDGE_SOURCE_REAL && config->device_count > 0;
}

static bool routing_equal(const bridge_config_t *left,
                          const bridge_config_t *right)
{
    if (!left || !right || left->source != right->source
        || left->device_count != right->device_count) {
        return false;
    }
    for (int i = 0; i < left->device_count; i++) {
        if (!bridge_device_equal(&left->devices[i], &right->devices[i])) {
            return false;
        }
    }
    return true;
}

static void copy_config(bridge_config_t *out, const bridge_config_t *input)
{
    if (!out) return;
    if (!input) {
        power_bridge_config_defaults(out);
        return;
    }
    *out = *input;
    power_bridge_config_validate(out);
}

static void reset_session_edges(void)
{
    s_runtime.error_reported = false;
    s_runtime.valid_reported = false;
}

static void queue_session_request(const bridge_device_t *device, int slot,
                                  bool temporary)
{
    if (!device || !device->bound) return;
    s_runtime.request_device = *device;
    s_runtime.request_temporary = temporary;
    s_runtime.request_pending = true;
    s_runtime.candidate_slot = slot;
    reset_session_edges();
}

static void flush_session_request(void)
{
    if (!s_runtime.request_pending) return;
    esp_err_t err = ble_power_bridge_request(&s_runtime.request_device,
                                             s_runtime.request_temporary);
    if (err == ESP_OK) {
        s_runtime.request_pending = false;
    } else if (err != ESP_ERR_INVALID_STATE) {
        s_runtime.request_pending = false;
        s_runtime.error_reported = true;
    }
}

static void persist_recent(int slot)
{
    const bridge_device_t *device =
        power_bridge_device(&s_runtime.config, slot);
    if (!device) return;
    s_runtime.recent = *device;
    s_runtime.recent_valid = true;
    if (s_recent_callback) s_recent_callback(device);
}

static void apply_action(bridge_failover_action_t action)
{
    if (action.persist_recent) persist_recent(action.slot >= 0
        ? action.slot : s_runtime.failover.active_slot);

    if (action.connect) {
        const bridge_device_t *device =
            power_bridge_device(&s_runtime.config, action.slot);
        if (device) queue_session_request(device, action.slot, false);
        return;
    }

    if (action.stop) {
        s_runtime.request_pending = false;
        s_runtime.candidate_slot = -1;
        reset_session_edges();
        ble_power_bridge_request_stop();
    }
}

static bridge_failover_action_t failover_event(
    bridge_failover_event_t event, int session_slot, uint32_t now_ms)
{
    const bridge_device_t *recent =
        s_runtime.recent_valid ? &s_runtime.recent : NULL;
    return bridge_failover_handle(&s_runtime.failover, &s_runtime.config,
                                  recent, event, session_slot, now_ms);
}

static void process_command(runtime_mailbox_t mailbox, uint32_t now_ms)
{
    bridge_failover_action_t action = {.slot = -1};
    switch (mailbox.command) {
    case BRIDGE_RUNTIME_SCAN_START:
        s_runtime.manual = true;
        s_runtime.scan_pending = true;
        ble_scanner_stop();
        action = failover_event(BRIDGE_FAILOVER_PAUSE, -1, now_ms);
        apply_action(action);
        break;
    case BRIDGE_RUNTIME_SCAN_STOP:
        s_runtime.scan_pending = false;
        ble_scanner_stop();
        break;
    case BRIDGE_RUNTIME_TEST:
        s_runtime.manual = true;
        s_runtime.scan_pending = false;
        ble_scanner_stop();
        (void)failover_event(BRIDGE_FAILOVER_PAUSE, -1, now_ms);
        queue_session_request(&mailbox.device, -1, true);
        break;
    case BRIDGE_RUNTIME_STOP:
        s_runtime.manual = true;
        s_runtime.scan_pending = false;
        ble_scanner_stop();
        action = failover_event(BRIDGE_FAILOVER_PAUSE, -1, now_ms);
        apply_action(action);
        break;
    case BRIDGE_RUNTIME_RESUME:
        s_runtime.manual = false;
        s_runtime.scan_pending = false;
        ble_scanner_stop();
        action = failover_event(BRIDGE_FAILOVER_RESUME, -1, now_ms);
        apply_action(action);
        break;
    case BRIDGE_RUNTIME_RETRY_NOW:
        s_runtime.manual = false;
        s_runtime.scan_pending = false;
        ble_scanner_stop();
        action = failover_event(BRIDGE_FAILOVER_RETRY_NOW, -1, now_ms);
        apply_action(action);
        break;
    }
}

void bridge_runtime_set_recent_callback(
    bridge_runtime_recent_callback_t callback)
{
    s_recent_callback = callback;
}

void bridge_runtime_init(const bridge_config_t *config, int sport_profile,
                         const bridge_device_t *recent, uint32_t now_ms)
{
    memset(&s_runtime, 0, sizeof(s_runtime));
    s_runtime.candidate_slot = -1;
    s_runtime.sport_profile = sport_profile;
    copy_config(&s_runtime.config, config);
    if (recent && recent->bound) {
        s_runtime.recent = *recent;
        s_runtime.recent_valid = true;
    }
    bridge_failover_init(&s_runtime.failover);
    s_runtime.initialized = true;

    portENTER_CRITICAL(&s_lock);
    s_mailbox = (runtime_mailbox_t){0};
    portEXIT_CRITICAL(&s_lock);

    bridge_failover_event_t event = configured_real(&s_runtime.config,
        sport_profile) ? BRIDGE_FAILOVER_START : BRIDGE_FAILOVER_TICK;
    apply_action(failover_event(event, -1, now_ms));
    flush_session_request();
}

void bridge_runtime_apply_config(const bridge_config_t *config,
                                 int sport_profile, uint32_t now_ms)
{
    if (!s_runtime.initialized) {
        bridge_runtime_init(config, sport_profile, NULL, now_ms);
        return;
    }
    bridge_config_t next;
    copy_config(&next, config);
    bool route_changed = !routing_equal(&s_runtime.config, &next)
        || s_runtime.sport_profile != sport_profile;
    bool was_manual = s_runtime.manual;
    s_runtime.config = next;
    s_runtime.sport_profile = sport_profile;
    s_runtime.manual = false;
    s_runtime.scan_pending = false;
    ble_scanner_stop();

    bridge_failover_action_t action = {.slot = -1};
    if (!configured_real(&s_runtime.config, sport_profile)) {
        action = failover_event(BRIDGE_FAILOVER_TICK, -1, now_ms);
    } else if (route_changed) {
        action = failover_event(BRIDGE_FAILOVER_START, -1, now_ms);
    } else if (was_manual) {
        action = failover_event(BRIDGE_FAILOVER_RESUME, -1, now_ms);
    }
    apply_action(action);
    flush_session_request();
}

esp_err_t bridge_runtime_request(bridge_runtime_command_t command,
                                 const bridge_device_t *device)
{
    if (command < BRIDGE_RUNTIME_SCAN_START
        || command > BRIDGE_RUNTIME_RETRY_NOW
        || (command == BRIDGE_RUNTIME_TEST && (!device || !device->bound))) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    bool busy = s_mailbox.pending;
    bool supersedes = command == BRIDGE_RUNTIME_RESUME
        || command == BRIDGE_RUNTIME_STOP
        || (command == BRIDGE_RUNTIME_SCAN_STOP && busy
            && s_mailbox.command == BRIDGE_RUNTIME_SCAN_START);
    if (!busy || supersedes) {
        s_mailbox = (runtime_mailbox_t){
            .pending = true,
            .command = command,
        };
        if (device) s_mailbox.device = *device;
    }
    portEXIT_CRITICAL(&s_lock);
    return busy && !supersedes ? ESP_ERR_INVALID_STATE : ESP_OK;
}

void bridge_runtime_tick(const bridge_config_t *config, int sport_profile,
                         uint32_t now_ms)
{
    if (!s_runtime.initialized) {
        bridge_runtime_init(config, sport_profile, NULL, now_ms);
    } else if (!routing_equal(&s_runtime.config, config)
               || s_runtime.sport_profile != sport_profile) {
        bridge_runtime_apply_config(config, sport_profile, now_ms);
    } else if (config) {
        s_runtime.config = *config;
    }

    runtime_mailbox_t mailbox = {0};
    portENTER_CRITICAL(&s_lock);
    if (s_mailbox.pending) {
        mailbox = s_mailbox;
        s_mailbox = (runtime_mailbox_t){0};
    }
    portEXIT_CRITICAL(&s_lock);
    if (mailbox.pending) process_command(mailbox, now_ms);

    flush_session_request();
    ble_power_bridge_tick(&s_runtime.config, now_ms);
    ble_bridge_status_t status =
        ble_power_bridge_status(&s_runtime.config, now_ms);

    if (s_runtime.scan_pending && !status.connected
        && status.state != BLE_BRIDGE_CONNECTING
        && status.state != BLE_BRIDGE_DISCOVERING
        && status.state != BLE_BRIDGE_SUBSCRIBING
        && status.state != BLE_BRIDGE_STOPPING) {
        s_runtime.scan_pending = false;
        ble_scanner_start(BLE_SCAN_DEFAULT_MS);
    }

    if (s_runtime.manual) return;

    bridge_failover_action_t action = {.slot = -1};
    bool handled = false;
    if (status.sample.valid && !status.temporary
        && !s_runtime.valid_reported) {
        s_runtime.valid_reported = true;
        action = failover_event(BRIDGE_FAILOVER_VALID,
                                s_runtime.candidate_slot, now_ms);
        handled = true;
    } else if (status.state == BLE_BRIDGE_ERROR
               && !s_runtime.error_reported) {
        s_runtime.error_reported = true;
        bridge_failover_event_t event =
            s_runtime.failover.state == BRIDGE_FAILOVER_LIVE
                ? BRIDGE_FAILOVER_DISCONNECTED
                : BRIDGE_FAILOVER_FAILED;
        action = failover_event(event, s_runtime.candidate_slot, now_ms);
        handled = true;
    }
    if (!handled) {
        action = failover_event(BRIDGE_FAILOVER_TICK,
                                s_runtime.candidate_slot, now_ms);
    }
    apply_action(action);
    flush_session_request();
}

bridge_runtime_snapshot_t bridge_runtime_snapshot(
    const bridge_config_t *calibration, uint32_t now_ms)
{
    bridge_runtime_snapshot_t snapshot = {
        .state = s_runtime.failover.state,
        .active_slot = s_runtime.failover.active_slot,
        .candidate_slot = s_runtime.candidate_slot,
        .configured_real = configured_real(&s_runtime.config,
                                           s_runtime.sport_profile),
        .actual_simulation = bridge_runtime_uses_simulation(),
        .session = ble_power_bridge_status(calibration, now_ms),
    };
    if (snapshot.state == BRIDGE_FAILOVER_FAST_RECOVERY) {
        int32_t remaining =
            (int32_t)(s_runtime.failover.fast_deadline_ms - now_ms);
        snapshot.next_attempt_ms = remaining > 0 ? (uint32_t)remaining : 0;
    } else if (snapshot.state == BRIDGE_FAILOVER_FALLBACK_SIMULATION) {
        int32_t remaining =
            (int32_t)(s_runtime.failover.next_probe_ms - now_ms);
        snapshot.next_attempt_ms = remaining > 0 ? (uint32_t)remaining : 0;
    }
    return snapshot;
}

bool bridge_runtime_uses_simulation(void)
{
    return !s_runtime.initialized || s_runtime.manual
        || bridge_failover_uses_simulation(&s_runtime.failover);
}
