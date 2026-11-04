#include "power_bridge.h"
#include "device_identity.h"

#include <string.h>

static int clamp_int(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

void power_bridge_config_defaults(bridge_config_t *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->percent_x10 = BRIDGE_PERCENT_DEFAULT;
}

bool power_bridge_config_equal(const bridge_config_t *a, const bridge_config_t *b)
{
    return a && b && a->source == b->source && a->bound == b->bound
        && a->addr_type == b->addr_type && memcmp(a->addr, b->addr, sizeof(a->addr)) == 0
        && a->calibration_mode == b->calibration_mode && a->watts == b->watts
        && a->percent_x10 == b->percent_x10 && strcmp(a->name, b->name) == 0;
}

bool power_bridge_config_validate(bridge_config_t *c)
{
    if (!c) return false;
    bridge_config_t original = *c;
    c->name[BRIDGE_NAME_MAX - 1] = '\0';
    bool changed = memchr(original.name, '\0', sizeof(original.name)) == NULL;
    if (!device_identity_text_valid((const uint8_t *)c->name, strlen(c->name))) {
        c->name[0] = '\0';
        changed = true;
    }
    if (c->source < BRIDGE_SOURCE_SIMULATED || c->source > BRIDGE_SOURCE_REAL) c->source = 0;
    if (c->bound != 0 && c->bound != 1) c->bound = 0;
    if (c->addr_type < 0 || c->addr_type > 1) {
        c->addr_type = 0;
        c->bound = 0;
    }
    static const uint8_t zero_addr[6] = {0};
    if (memcmp(c->addr, zero_addr, sizeof(c->addr)) == 0) c->bound = 0;
    if (!c->bound) c->source = BRIDGE_SOURCE_SIMULATED;
    if (c->calibration_mode < 0 || c->calibration_mode > BRIDGE_CALIBRATION_PERCENT) {
        c->calibration_mode = BRIDGE_CALIBRATION_NONE;
    }
    c->watts = clamp_int(c->watts, BRIDGE_OFFSET_MIN, BRIDGE_OFFSET_MAX);
    c->percent_x10 = clamp_int(c->percent_x10, BRIDGE_PERCENT_MIN, BRIDGE_PERCENT_MAX);
    original.name[BRIDGE_NAME_MAX - 1] = '\0';
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
    size_t len = 0;
    while (len < sizeof(c->name) && c->name[len]) len++;
    if (len >= sizeof(c->name) || capacity < 16 + len) return 0;
    bridge_config_t checked = *c;
    if (power_bridge_config_validate(&checked)) return 0;
    out[0] = BRIDGE_WIRE_VERSION;
    out[1] = (uint8_t)c->source;
    out[2] = (uint8_t)c->bound;
    out[3] = (uint8_t)c->addr_type;
    memcpy(out + 4, c->addr, 6);
    out[10] = (uint8_t)c->calibration_mode;
    out[11] = (uint8_t)c->watts;
    out[12] = (uint8_t)((uint16_t)c->watts >> 8);
    out[13] = (uint8_t)c->percent_x10;
    out[14] = (uint8_t)((uint16_t)c->percent_x10 >> 8);
    out[15] = (uint8_t)len;
    memcpy(out + 16, c->name, len);
    return 16 + len;
}

bool power_bridge_config_decode(const uint8_t *data, size_t length, bridge_config_t *out)
{
    if (!data || !out || length < 16 || data[0] != BRIDGE_WIRE_VERSION
        || data[15] >= BRIDGE_NAME_MAX || length != (size_t)16 + data[15]
        || memchr(data + 16, 0, data[15])) return false;
    bridge_config_t c;
    power_bridge_config_defaults(&c);
    c.source = data[1];
    c.bound = data[2];
    c.addr_type = data[3];
    memcpy(c.addr, data + 4, 6);
    c.calibration_mode = data[10];
    c.watts = (int16_t)((uint16_t)data[11] | ((uint16_t)data[12] << 8));
    c.percent_x10 = (uint16_t)data[13] | ((uint16_t)data[14] << 8);
    memcpy(c.name, data + 16, data[15]);
    if (power_bridge_config_validate(&c)) return false;
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
