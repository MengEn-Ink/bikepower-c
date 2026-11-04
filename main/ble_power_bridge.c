#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "os/os_mbuf.h"

#include "bikepower_config.h"
#include "ble_scanner.h"
#include "ble_power_bridge.h"

typedef enum {
    PHASE_NONE, PHASE_SERVICE, PHASE_CHARS, PHASE_DESCRIPTORS,
    PHASE_SUBSCRIBE, PHASE_READ, PHASE_READY
} bridge_phase_t;

typedef struct {
    ble_bridge_status_t status;
    power_bridge_t input;
    uint16_t handle;
    uint16_t service_start;
    uint16_t service_end;
    uint16_t measurement;
    uint16_t descriptor_end;
    uint16_t cccd;
    uint8_t read_index;
    bridge_phase_t phase;
    bool procedure_pending;
    bool desired;
    bool connecting;
    bool terminating;
    uint32_t generation;
    uint32_t started_ms;
    uint32_t connected_ms;
    uint32_t retry_until_ms;
    uint32_t retry_at_ms;
    uint32_t retry_delay_ms;
    bool command_pending;
    bool command_stop;
    bool command_temporary;
    bridge_config_t command;
} bridge_context_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bridge_context_t s_bridge = { .handle = BLE_HS_CONN_HANDLE_NONE };
static const uint16_t s_read_uuids[] = { 0x2a00, 0x2a29, 0x2a24, 0x2a25, 0x2a27, 0x2a26, 0x2a28 };

static uint32_t clock_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool callback_current(uint16_t handle, void *arg)
{
    return (uint32_t)(uintptr_t)arg == s_bridge.generation && handle == s_bridge.handle;
}

static void failed(int error)
{
    s_bridge.status.error = error;
    s_bridge.status.state = BLE_BRIDGE_ERROR;
    s_bridge.phase = PHASE_NONE;
    s_bridge.procedure_pending = false;
}

static int service_cb(uint16_t handle, const struct ble_gatt_error *error,
                      const struct ble_gatt_svc *service, void *arg)
{
    portENTER_CRITICAL(&s_lock);
    if (callback_current(handle, arg)) {
        if (error->status == 0 && service) {
            s_bridge.service_start = service->start_handle;
            s_bridge.service_end = service->end_handle;
        } else {
            s_bridge.procedure_pending = false;
            if (error->status == BLE_HS_EDONE && s_bridge.service_start) {
                s_bridge.phase = PHASE_CHARS;
            } else {
                failed(error->status ? error->status : BLE_HS_ENOENT);
            }
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return 0;
}

static int char_cb(uint16_t handle, const struct ble_gatt_error *error,
                   const struct ble_gatt_chr *chr, void *arg)
{
    portENTER_CRITICAL(&s_lock);
    if (callback_current(handle, arg)) {
        if (error->status == 0 && chr) {
            if (s_bridge.measurement && !s_bridge.descriptor_end && chr->def_handle > s_bridge.measurement) {
                s_bridge.descriptor_end = chr->def_handle - 1;
            }
            if (ble_uuid_u16(&chr->uuid.u) == POWER_CHAR_UUID
                    && (chr->properties & BLE_GATT_CHR_PROP_NOTIFY)) {
                s_bridge.measurement = chr->val_handle;
            }
        } else {
            s_bridge.procedure_pending = false;
            if (error->status == BLE_HS_EDONE && s_bridge.measurement) {
                if (!s_bridge.descriptor_end) s_bridge.descriptor_end = s_bridge.service_end;
                s_bridge.phase = PHASE_DESCRIPTORS;
            } else {
                failed(error->status ? error->status : BLE_HS_ENOENT);
            }
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return 0;
}

static int descriptor_cb(uint16_t handle, const struct ble_gatt_error *error,
                         uint16_t chr_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)chr_handle;
    portENTER_CRITICAL(&s_lock);
    if (callback_current(handle, arg)) {
        if (error->status == 0 && dsc) {
            if (ble_uuid_u16(&dsc->uuid.u) == 0x2902) s_bridge.cccd = dsc->handle;
        } else {
            s_bridge.procedure_pending = false;
            if (error->status == BLE_HS_EDONE && s_bridge.cccd) {
                s_bridge.phase = PHASE_SUBSCRIBE;
            } else {
                failed(error->status ? error->status : BLE_HS_ENOENT);
            }
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return 0;
}

static int subscribe_cb(uint16_t handle, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    portENTER_CRITICAL(&s_lock);
    if (callback_current(handle, arg)) {
        s_bridge.procedure_pending = false;
        if (error->status == 0) {
            s_bridge.phase = PHASE_READ;
            s_bridge.status.state = BLE_BRIDGE_WAITING;
        } else {
            failed(error->status);
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return 0;
}

static int read_cb(uint16_t handle, const struct ble_gatt_error *error,
                   struct ble_gatt_attr *attr, void *arg)
{
    uint8_t text[DEVICE_IDENTITY_TEXT_MAX];
    int length = attr && attr->om ? OS_MBUF_PKTLEN(attr->om) : 0;
    bool valid = error->status == 0 && length > 0 && length < (int)sizeof(text)
        && os_mbuf_copydata(attr->om, 0, length, text) == 0
        && device_identity_text_valid(text, length);
    portENTER_CRITICAL(&s_lock);
    if (callback_current(handle, arg)) {
        if (valid) {
            char *dst = s_bridge.read_index == 0 ? s_bridge.status.device_name
                : s_bridge.status.identity.fields[s_bridge.read_index - 1];
            memcpy(dst, text, length);
            dst[length] = '\0';
        }
        if (error->status != 0) {
            s_bridge.read_index++;
            s_bridge.procedure_pending = false;
            if (s_bridge.read_index >= sizeof(s_read_uuids) / sizeof(s_read_uuids[0])) {
                s_bridge.phase = PHASE_READY;
            }
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return 0;
}

static int gap_cb(struct ble_gap_event *event, void *arg)
{
    uint32_t now = clock_ms();
    cps_input_t sample;
    bool parsed = false;
    if (event->type == BLE_GAP_EVENT_NOTIFY_RX) {
        uint8_t bytes[BLE_BRIDGE_CPS_INPUT_MAX];
        int length = OS_MBUF_PKTLEN(event->notify_rx.om);
        parsed = length <= (int)sizeof(bytes) && length > 0
            && os_mbuf_copydata(event->notify_rx.om, 0, length, bytes) == 0
            && cps_input_decode(bytes, length, &sample);
    }
    portENTER_CRITICAL(&s_lock);
    if ((uint32_t)(uintptr_t)arg != s_bridge.generation) {
        portEXIT_CRITICAL(&s_lock);
        return 0;
    }
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        s_bridge.connecting = false;
        if (event->connect.status == 0) {
            s_bridge.handle = event->connect.conn_handle;
            s_bridge.connected_ms = now;
            s_bridge.status.connected = true;
            s_bridge.status.state = BLE_BRIDGE_DISCOVERING;
            s_bridge.phase = PHASE_SERVICE;
            s_bridge.procedure_pending = false;
        } else {
            failed(event->connect.status);
            s_bridge.retry_at_ms = now + s_bridge.retry_delay_ms;
            if (s_bridge.retry_delay_ms < BLE_BRIDGE_RETRY_MAX_MS) s_bridge.retry_delay_ms *= 2;
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        if (event->disconnect.conn.conn_handle != s_bridge.handle) break;
        s_bridge.handle = BLE_HS_CONN_HANDLE_NONE;
        s_bridge.terminating = false;
        s_bridge.status.connected = false;
        power_bridge_disconnect(&s_bridge.input);
        failed(event->disconnect.reason);
        s_bridge.retry_at_ms = now + s_bridge.retry_delay_ms;
        if (s_bridge.retry_delay_ms < BLE_BRIDGE_RETRY_MAX_MS) s_bridge.retry_delay_ms *= 2;
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (callback_current(event->notify_rx.conn_handle, arg)
                && event->notify_rx.attr_handle == s_bridge.measurement
                && s_bridge.phase >= PHASE_READ && parsed) {
            power_bridge_receive(&s_bridge.input, &sample, now);
            if (s_bridge.input.valid_samples >= BRIDGE_VALID_SAMPLE_COUNT) {
                s_bridge.status.tested = true;
            }
        }
        break;
    default:
        break;
    }
    portEXIT_CRITICAL(&s_lock);
    return 0;
}

esp_err_t ble_power_bridge_request(const bridge_config_t *peer, bool temporary)
{
    if (!peer || !peer->bound) return ESP_ERR_INVALID_ARG;
    bridge_config_t checked = *peer;
    if (power_bridge_config_validate(&checked)) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_lock);
    bool busy = s_bridge.command_pending;
    if (!busy) {
        s_bridge.command = checked;
        s_bridge.command_temporary = temporary;
        s_bridge.command_stop = false;
        s_bridge.command_pending = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return busy ? ESP_ERR_INVALID_STATE : ESP_OK;
}

esp_err_t ble_power_bridge_request_stop(void)
{
    portENTER_CRITICAL(&s_lock);
    s_bridge.command_stop = true;
    s_bridge.command_pending = true;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

static void terminate(uint16_t handle, bool connecting)
{
    if (connecting) ble_gap_conn_cancel();
    if (handle != BLE_HS_CONN_HANDLE_NONE) {
        int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc == BLE_HS_ENOTCONN) {
            portENTER_CRITICAL(&s_lock);
            if (s_bridge.handle == handle) {
                s_bridge.handle = BLE_HS_CONN_HANDLE_NONE;
                s_bridge.status.connected = false;
                s_bridge.terminating = false;
            }
            portEXIT_CRITICAL(&s_lock);
        }
    }
}

void ble_power_bridge_tick(const bridge_config_t *calibration, uint32_t now)
{
    uint16_t handle;
    bool connecting;
    bool stop = false;
    bool connect = false;
    bridge_phase_t phase = PHASE_NONE;
    bridge_context_t action;

    portENTER_CRITICAL(&s_lock);
    handle = s_bridge.handle;
    connecting = s_bridge.connecting;
    if (s_bridge.command_pending) {
        bool promote = !s_bridge.command_stop && !s_bridge.command_temporary
            && handle != BLE_HS_CONN_HANDLE_NONE && !s_bridge.terminating
            && s_bridge.command.addr_type == s_bridge.status.peer.addr_type
            && memcmp(s_bridge.command.addr, s_bridge.status.peer.addr, 6) == 0;
        if (promote) {
            s_bridge.command_pending = false;
            s_bridge.status.temporary = false;
            s_bridge.status.peer = s_bridge.command;
            s_bridge.desired = true;
            s_bridge.retry_until_ms = now + BLE_BRIDGE_RECONNECT_MS;
        } else if (handle != BLE_HS_CONN_HANDLE_NONE || connecting) {
            stop = !s_bridge.terminating;
            s_bridge.terminating = true;
            s_bridge.desired = false;
            s_bridge.status.state = BLE_BRIDGE_STOPPING;
            power_bridge_disconnect(&s_bridge.input);
        } else {
            s_bridge.command_pending = false;
            s_bridge.desired = !s_bridge.command_stop;
            s_bridge.terminating = false;
            if (s_bridge.desired) {
                s_bridge.status = (ble_bridge_status_t) {
                    .peer = s_bridge.command,
                    .temporary = s_bridge.command_temporary,
                    .state = BLE_BRIDGE_CONNECTING,
                };
                power_bridge_init(&s_bridge.input);
                s_bridge.started_ms = now;
                s_bridge.retry_until_ms = now + BLE_BRIDGE_RECONNECT_MS;
                s_bridge.retry_at_ms = now;
                s_bridge.retry_delay_ms = BLE_BRIDGE_RETRY_MIN_MS;
            } else {
                s_bridge.status.state = BLE_BRIDGE_IDLE;
            }
        }
    }
    if (s_bridge.desired && s_bridge.status.temporary
            && now - s_bridge.started_ms >= BLE_BRIDGE_TEST_MS) {
        s_bridge.desired = false;
        stop = !s_bridge.terminating;
        s_bridge.terminating = true;
        s_bridge.status.state = BLE_BRIDGE_IDLE;
        power_bridge_disconnect(&s_bridge.input);
    }
    if (s_bridge.desired && handle != BLE_HS_CONN_HANDLE_NONE) {
        uint32_t last = s_bridge.input.valid_samples ? s_bridge.input.last_sample_ms : s_bridge.connected_ms;
        if (s_bridge.status.state == BLE_BRIDGE_ERROR || now - last >= BLE_BRIDGE_NO_SAMPLE_MS) {
            stop = !s_bridge.terminating;
            s_bridge.terminating = true;
            power_bridge_disconnect(&s_bridge.input);
        } else if (s_bridge.phase >= PHASE_READ) {
            bridge_snapshot_t snapshot = power_bridge_snapshot(&s_bridge.input, calibration, now);
            s_bridge.status.state = snapshot.valid ? BLE_BRIDGE_VALID
                : s_bridge.status.tested ? BLE_BRIDGE_STALE : BLE_BRIDGE_WAITING;
            if (snapshot.valid) {
                s_bridge.retry_until_ms = now + BLE_BRIDGE_RECONNECT_MS;
                s_bridge.retry_delay_ms = BLE_BRIDGE_RETRY_MIN_MS;
            }
        }
        if (!s_bridge.terminating && !s_bridge.procedure_pending
                && s_bridge.phase > PHASE_NONE && s_bridge.phase < PHASE_READY) {
            phase = s_bridge.phase;
            s_bridge.procedure_pending = true;
            if (phase == PHASE_SUBSCRIBE) s_bridge.status.state = BLE_BRIDGE_SUBSCRIBING;
        }
    }
    if (s_bridge.desired && handle == BLE_HS_CONN_HANDLE_NONE && !connecting) {
        if ((int32_t)(now - s_bridge.retry_until_ms) >= 0) {
            s_bridge.desired = false;
            s_bridge.status.state = BLE_BRIDGE_ERROR;
        } else if ((int32_t)(now - s_bridge.retry_at_ms) >= 0) {
            connect = true;
            s_bridge.connecting = true;
            s_bridge.terminating = false;
            s_bridge.generation++;
            s_bridge.service_start = s_bridge.service_end = s_bridge.measurement = 0;
            s_bridge.descriptor_end = s_bridge.cccd = s_bridge.read_index = 0;
            s_bridge.phase = PHASE_NONE;
            s_bridge.status.state = BLE_BRIDGE_CONNECTING;
            s_bridge.retry_at_ms = now + BLE_BRIDGE_CONNECT_MS + s_bridge.retry_delay_ms;
        } else {
            s_bridge.status.state = BLE_BRIDGE_RETRYING;
        }
    }
    action = s_bridge;
    portEXIT_CRITICAL(&s_lock);

    if (stop) terminate(handle, connecting);
    int rc = 0;
    void *arg = (void *)(uintptr_t)action.generation;
    if (connect) {
        ble_scanner_stop();
        uint8_t own_type;
        ble_addr_t peer = { .type = (uint8_t)action.status.peer.addr_type };
        memcpy(peer.val, action.status.peer.addr, sizeof(peer.val));
        rc = ble_hs_id_infer_auto(0, &own_type);
        if (!rc) rc = ble_gap_connect(own_type, &peer, BLE_BRIDGE_CONNECT_MS, NULL, gap_cb, arg);
        if (rc) {
            portENTER_CRITICAL(&s_lock);
            s_bridge.connecting = false;
            failed(rc);
            s_bridge.retry_at_ms = now + s_bridge.retry_delay_ms;
            if (s_bridge.retry_delay_ms < BLE_BRIDGE_RETRY_MAX_MS) s_bridge.retry_delay_ms *= 2;
            portEXIT_CRITICAL(&s_lock);
        }
    } else if (phase != PHASE_NONE) {
        ble_uuid16_t uuid = BLE_UUID16_INIT(POWER_SERVICE_UUID);
        switch (phase) {
        case PHASE_SERVICE:
            rc = ble_gattc_disc_svc_by_uuid(handle, &uuid.u, service_cb, arg);
            break;
        case PHASE_CHARS:
            rc = ble_gattc_disc_all_chrs(handle, action.service_start, action.service_end, char_cb, arg);
            break;
        case PHASE_DESCRIPTORS:
            rc = ble_gattc_disc_all_dscs(handle, action.measurement, action.descriptor_end, descriptor_cb, arg);
            break;
        case PHASE_SUBSCRIBE: {
            static const uint8_t enable[2] = { 1, 0 };
            rc = ble_gattc_write_flat(handle, action.cccd, enable, sizeof(enable), subscribe_cb, arg);
            break;
        }
        case PHASE_READ:
            uuid.value = s_read_uuids[action.read_index];
            rc = ble_gattc_read_by_uuid(handle, 1, 0xffff, &uuid.u, read_cb, arg);
            break;
        default:
            break;
        }
        if (rc) {
            struct ble_gatt_error error = { .status = rc };
            if (phase == PHASE_READ) read_cb(handle, &error, NULL, arg);
            else {
                portENTER_CRITICAL(&s_lock);
                failed(rc);
                portEXIT_CRITICAL(&s_lock);
            }
        }
    }
}

ble_bridge_status_t ble_power_bridge_status(const bridge_config_t *calibration, uint32_t now)
{
    portENTER_CRITICAL(&s_lock);
    ble_bridge_status_t status = s_bridge.status;
    status.sample = power_bridge_snapshot(&s_bridge.input, calibration, now);
    portEXIT_CRITICAL(&s_lock);
    return status;
}

void ble_power_bridge_radio_stop(void)
{
    portENTER_CRITICAL(&s_lock);
    uint16_t handle = s_bridge.handle;
    bool connecting = s_bridge.connecting;
    s_bridge.desired = false;
    s_bridge.command_pending = false;
    s_bridge.status.state = BLE_BRIDGE_IDLE;
    power_bridge_disconnect(&s_bridge.input);
    portEXIT_CRITICAL(&s_lock);
    terminate(handle, connecting);
}

void ble_power_bridge_portal_stop(void)
{
    portENTER_CRITICAL(&s_lock);
    bool temporary = s_bridge.status.temporary
        || (s_bridge.command_pending && s_bridge.command_temporary);
    portEXIT_CRITICAL(&s_lock);
    if (temporary) ble_power_bridge_request_stop();
}

void ble_power_bridge_host_reset(void)
{
    uint32_t now = clock_ms();
    portENTER_CRITICAL(&s_lock);
    s_bridge.generation++;
    s_bridge.handle = BLE_HS_CONN_HANDLE_NONE;
    s_bridge.connecting = false;
    s_bridge.terminating = false;
    s_bridge.procedure_pending = false;
    s_bridge.status.connected = false;
    s_bridge.phase = PHASE_NONE;
    s_bridge.status.state = BLE_BRIDGE_ERROR;
    s_bridge.retry_at_ms = now + BLE_BRIDGE_RETRY_MIN_MS;
    power_bridge_disconnect(&s_bridge.input);
    portEXIT_CRITICAL(&s_lock);
}
