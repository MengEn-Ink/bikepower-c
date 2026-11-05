#include "power_bridge.h"
#include "device_identity.h"

#include <string.h>

static int clamp_int(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

static bool address_is_zero(const uint8_t addr[6])
{
    static const uint8_t zero_addr[6] = {0};
    return memcmp(addr, zero_addr, sizeof(zero_addr)) == 0;
}

void power_bridge_config_defaults(bridge_config_t *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->percent_x10 = BRIDGE_PERCENT_DEFAULT;
}

bool bridge_device_equal(const bridge_device_t *a, const bridge_device_t *b)
{
    return a && b && a->bound && b->bound && a->addr_type == b->addr_type
        && memcmp(a->addr, b->addr, sizeof(a->addr)) == 0;
}

const bridge_device_t *power_bridge_device(const bridge_config_t *c, int slot)
{
    if (!c || slot < 0 || slot >= c->device_count || slot >= BRIDGE_DEVICE_MAX) return NULL;
    return &c->devices[slot];
}

int power_bridge_find_device(const bridge_config_t *c, const bridge_device_t *device)
{
    if (!c || !device) return -1;
    for (int i = 0; i < c->device_count && i < BRIDGE_DEVICE_MAX; i++) {
        if (bridge_device_equal(&c->devices[i], device)) return i;
    }
    return -1;
}

bool power_bridge_config_equal(const bridge_config_t *a, const bridge_config_t *b)
{
    if (!a || !b || a->source != b->source
        || a->calibration_mode != b->calibration_mode || a->watts != b->watts
        || a->percent_x10 != b->percent_x10 || a->device_count != b->device_count) {
        return false;
    }
    for (int i = 0; i < a->device_count; i++) {
        const bridge_device_t *left = &a->devices[i];
        const bridge_device_t *right = &b->devices[i];
        if (left->bound != right->bound || left->addr_type != right->addr_type
            || memcmp(left->addr, right->addr, sizeof(left->addr)) != 0
            || strcmp(left->name, right->name) != 0) {
            return false;
        }
    }
    return true;
}

bool power_bridge_config_validate(bridge_config_t *c)
{
    if (!c) return false;
    bridge_config_t original = *c;
    bool changed = false;
    if (c->source < BRIDGE_SOURCE_SIMULATED || c->source > BRIDGE_SOURCE_REAL) c->source = 0;
    int input_count = c->device_count;
    if (input_count < 0) input_count = 0;
    if (input_count > BRIDGE_DEVICE_MAX) input_count = BRIDGE_DEVICE_MAX;
    int kept = 0;
    for (int i = 0; i < input_count; i++) {
        bridge_device_t candidate = c->devices[i];
        bool terminated = memchr(candidate.name, '\0', sizeof(candidate.name)) != NULL;
        if (!terminated) candidate.name[BRIDGE_NAME_MAX - 1] = '\0';
        bool valid = candidate.bound == 1
            && candidate.addr_type >= 0 && candidate.addr_type <= 1
            && !address_is_zero(candidate.addr) && terminated
            && device_identity_text_valid((const uint8_t *)candidate.name,
                                          strlen(candidate.name));
        if (!valid) {
            changed = true;
            continue;
        }
        bool duplicate = false;
        for (int j = 0; j < kept; j++) {
            if (bridge_device_equal(&c->devices[j], &candidate)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            changed = true;
            continue;
        }
        if (kept != i || memcmp(&c->devices[kept], &candidate, sizeof(candidate)) != 0) {
            changed = true;
        }
        c->devices[kept++] = candidate;
    }
    for (int i = kept; i < BRIDGE_DEVICE_MAX; i++) {
        bridge_device_t empty = {0};
        if (memcmp(&c->devices[i], &empty, sizeof(empty)) != 0) changed = true;
        c->devices[i] = empty;
    }
    if (c->device_count != kept) changed = true;
    c->device_count = kept;
    if (c->device_count == 0) c->source = BRIDGE_SOURCE_SIMULATED;
    if (c->calibration_mode < 0 || c->calibration_mode > BRIDGE_CALIBRATION_PERCENT) {
        c->calibration_mode = BRIDGE_CALIBRATION_NONE;
    }
    c->watts = clamp_int(c->watts, BRIDGE_OFFSET_MIN, BRIDGE_OFFSET_MAX);
    c->percent_x10 = clamp_int(c->percent_x10, BRIDGE_PERCENT_MIN, BRIDGE_PERCENT_MAX);
    return changed || !power_bridge_config_equal(&original, c);
}

int power_bridge_correct(const bridge_config_t *c, int raw)
{
    if (!c || raw <= 0) return 0;
    int bounded = clamp_int(raw, 0, INT16_MAX);
    int result = bounded;
    if (c->calibration_mode == BRIDGE_CALIBRATION_OFFSET) {
        result += clamp_int(c->watts, BRIDGE_OFFSET_MIN, BRIDGE_OFFSET_MAX);
    } else if (c->calibration_mode == BRIDGE_CALIBRATION_PERCENT) {
        result = (bounded * clamp_int(c->percent_x10, BRIDGE_PERCENT_MIN, BRIDGE_PERCENT_MAX)
                  + 500) / 1000;
    }
    return clamp_int(result, 0, POWER_MAX);
}

size_t power_bridge_config_encode(const bridge_config_t *c, uint8_t *out, size_t capacity)
{
    if (!c || !out) return 0;
    bridge_config_t checked = *c;
    if (power_bridge_config_validate(&checked)) return 0;
    size_t required = 8;
    for (int i = 0; i < c->device_count; i++) {
        size_t name_len = strlen(c->devices[i].name);
        required += 8 + name_len;
    }
    if (required > capacity) return 0;
    out[0] = BRIDGE_WIRE_VERSION;
    out[1] = (uint8_t)c->source;
    out[2] = (uint8_t)c->device_count;
    out[3] = (uint8_t)c->calibration_mode;
    out[4] = (uint8_t)c->watts;
    out[5] = (uint8_t)((uint16_t)c->watts >> 8);
    out[6] = (uint8_t)c->percent_x10;
    out[7] = (uint8_t)((uint16_t)c->percent_x10 >> 8);
    size_t offset = 8;
    for (int i = 0; i < c->device_count; i++) {
        const bridge_device_t *device = &c->devices[i];
        size_t name_len = strlen(device->name);
        out[offset++] = (uint8_t)device->addr_type;
        memcpy(out + offset, device->addr, sizeof(device->addr));
        offset += sizeof(device->addr);
        out[offset++] = (uint8_t)name_len;
        memcpy(out + offset, device->name, name_len);
        offset += name_len;
    }
    return offset;
}

static bool decode_legacy(const uint8_t *data, size_t length, bridge_config_t *out)
{
    if (length < 16 || data[15] >= BRIDGE_NAME_MAX
        || length != (size_t)16 + data[15]
        || memchr(data + 16, 0, data[15])) return false;
    bridge_config_t c;
    power_bridge_config_defaults(&c);
    c.source = data[1];
    c.calibration_mode = data[10];
    c.watts = (int16_t)((uint16_t)data[11] | ((uint16_t)data[12] << 8));
    c.percent_x10 = (uint16_t)data[13] | ((uint16_t)data[14] << 8);
    if (data[2]) {
        c.device_count = 1;
        c.devices[0].bound = data[2];
        c.devices[0].addr_type = data[3];
        memcpy(c.devices[0].addr, data + 4, 6);
        memcpy(c.devices[0].name, data + 16, data[15]);
    }
    power_bridge_config_validate(&c);
    *out = c;
    return true;
}

bool power_bridge_config_decode(const uint8_t *data, size_t length, bridge_config_t *out)
{
    if (!data || !out || length == 0) return false;
    if (data[0] == BRIDGE_WIRE_LEGACY_VERSION) {
        return decode_legacy(data, length, out);
    }
    if (data[0] != BRIDGE_WIRE_VERSION || length < 8 || data[2] > BRIDGE_DEVICE_MAX) {
        return false;
    }
    bridge_config_t c;
    power_bridge_config_defaults(&c);
    c.source = data[1];
    c.device_count = data[2];
    c.calibration_mode = data[3];
    c.watts = (int16_t)((uint16_t)data[4] | ((uint16_t)data[5] << 8));
    c.percent_x10 = (uint16_t)data[6] | ((uint16_t)data[7] << 8);
    size_t offset = 8;
    for (int i = 0; i < c.device_count; i++) {
        if (length - offset < 8) return false;
        bridge_device_t *device = &c.devices[i];
        device->bound = 1;
        device->addr_type = data[offset++];
        memcpy(device->addr, data + offset, sizeof(device->addr));
        offset += sizeof(device->addr);
        size_t name_len = data[offset++];
        if (name_len >= BRIDGE_NAME_MAX || length - offset < name_len
            || memchr(data + offset, 0, name_len)) {
            return false;
        }
        memcpy(device->name, data + offset, name_len);
        offset += name_len;
    }
    if (offset != length) return false;
    power_bridge_config_validate(&c);
    *out = c;
    return true;
}

void power_bridge_init(power_bridge_t *b)
{
    if (b) memset(b, 0, sizeof(*b));
}

void power_bridge_disconnect(power_bridge_t *b)
{
    power_bridge_init(b);
}

void power_bridge_receive(power_bridge_t *b, const cps_input_t *in, uint32_t now)
{
    if (!b || !in) return;
    if (b->valid_samples && (uint32_t)(now - b->last_sample_ms) >= BRIDGE_STALE_MS) {
        power_bridge_init(b);
    }
    if (b->valid_samples < BRIDGE_VALID_SAMPLE_COUNT) b->valid_samples++;
    if (!in->has_crank || in->power_w <= 0) b->cadence_valid = false;
    if (in->has_crank && b->crank_baseline) {
        uint16_t revs = (uint16_t)(in->crank_revolutions - b->input.crank_revolutions);
        uint16_t elapsed = (uint16_t)(in->crank_event_time_1024
                                     - b->input.crank_event_time_1024);
        if (revs || elapsed) {
            uint32_t cadence = elapsed ? ((uint32_t)revs * 120 * 1024 + elapsed / 2) / elapsed : 0;
            b->cadence_valid = revs && elapsed && cadence <= BRIDGE_CADENCE_HALF_RPM_MAX
                                && in->power_w > 0;
            b->cadence_half_rpm = b->cadence_valid ? (uint16_t)cadence : 0;
            b->last_crank_ms = now;
        }
    }
    b->crank_baseline = in->has_crank;
    b->input = *in;
    b->last_sample_ms = now;
}

bridge_snapshot_t power_bridge_snapshot(const power_bridge_t *b,
                                       const bridge_config_t *c, uint32_t now)
{
    bridge_snapshot_t snap = {0};
    if (!b || !c) return snap;
    snap.age_ms = (uint32_t)(now - b->last_sample_ms);
    snap.valid = b->valid_samples >= BRIDGE_VALID_SAMPLE_COUNT && snap.age_ms < BRIDGE_STALE_MS;
    if (!snap.valid) return snap;
    snap.raw_power_w = b->input.power_w;
    snap.power_w = power_bridge_correct(c, b->input.power_w);
    snap.has_crank = b->input.has_crank;
    snap.crank_revolutions = b->input.crank_revolutions;
    snap.crank_event_time_1024 = b->input.crank_event_time_1024;
    snap.cadence_valid = b->cadence_valid && (uint32_t)(now - b->last_crank_ms) < BRIDGE_STALE_MS;
    snap.cadence_half_rpm = snap.cadence_valid ? b->cadence_half_rpm : 0;
    return snap;
}
