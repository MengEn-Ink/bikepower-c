#include <stddef.h>
#include <string.h>
#include "config_snapshot.h"

static void put32(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) out[i] = (uint8_t)(value >> (i * 8));
}

static uint32_t get32(const uint8_t *data)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; i++) value |= (uint32_t)data[i] << (i * 8);
    return value;
}

static uint32_t crc_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

uint32_t config_snapshot_crc(const uint8_t *data, size_t length)
{
    return ~crc_update(UINT32_MAX, data, length);
}

static uint32_t snapshot_crc(const uint8_t *data, size_t length)
{
    uint32_t crc = crc_update(UINT32_MAX, data, 12);
    return ~crc_update(crc, data + CONFIG_SNAPSHOT_HEADER_LEN,
                       length - CONFIG_SNAPSHOT_HEADER_LEN);
}

#define FIELD(name) offsetof(config_data_t, name)
static const size_t s_fields[] = {
    FIELD(schema_ver), FIELD(power), FIELD(cadence), FIELD(mode), FIELD(heartrate),
    FIELD(has_user_hr), FIELD(power_float), FIELD(cadence_float), FIELD(hr_float),
    FIELD(power_step), FIELD(weight), FIELD(ftp), FIELD(thr_hr), FIELD(ride_type),
    FIELD(custom_wkg_low), FIELD(custom_wkg_high), FIELD(sport_profile), FIELD(broadcast_mask),
    FIELD(run_speed_x100), FIELD(run_pace_float_sec), FIELD(run_cadence),
    FIELD(run_cadence_float), FIELD(run_mode), FIELD(run_pacer_type),
    FIELD(run_pace_low_sec), FIELD(run_pace_high_sec),
};
#undef FIELD

static bool put_string(uint8_t *out, size_t *offset, const char *text, size_t size)
{
    const char *end = memchr(text, 0, size);
    if (!end) return false;
    size_t length = (size_t)(end - text);
    out[(*offset)++] = (uint8_t)length;
    memcpy(out + *offset, text, length);
    *offset += length;
    return true;
}

static bool get_string(const uint8_t *data, size_t length, size_t *offset,
                       char *text, size_t capacity)
{
    if (*offset >= length) return false;
    size_t size = data[(*offset)++];
    if (size >= capacity || size > length - *offset || memchr(data + *offset, 0, size)) return false;
    memcpy(text, data + *offset, size);
    text[size] = 0;
    *offset += size;
    return true;
}

size_t config_snapshot_encode(const config_data_t *config, const device_identity_t *identity,
                              uint32_t generation, uint8_t *out, size_t capacity)
{
    if (!config || !identity || !out || capacity < CONFIG_SNAPSHOT_MAX_LEN) return 0;
    config_data_t checked = *config;
    if (checked.schema_ver != CONFIG_VERSION || config_store_validate(&checked)) return 0;
    for (unsigned i = 0; i < DEVICE_IDENTITY_FIELD_COUNT; i++) {
        const char *end = memchr(identity->fields[i], 0, DEVICE_IDENTITY_TEXT_MAX);
        if (!end || !device_identity_text_valid((const uint8_t *)identity->fields[i],
                                                (size_t)(end - identity->fields[i]))) return 0;
    }
    memset(out, 0, CONFIG_SNAPSHOT_HEADER_LEN);
    size_t offset = CONFIG_SNAPSHOT_HEADER_LEN;
    for (size_t i = 0; i < sizeof(s_fields) / sizeof(s_fields[0]); i++) {
        const int *field = (const int *)((const uint8_t *)config + s_fields[i]);
        put32(out + offset, (uint32_t)*field);
        offset += 4;
    }
    if (!put_string(out, &offset, config->custom_pacer_name, sizeof(config->custom_pacer_name))
        || !put_string(out, &offset, config->run_custom_name, sizeof(config->run_custom_name))
        || !put_string(out, &offset, config->device_name, sizeof(config->device_name))) return 0;
    out[offset++] = config->led.silent;
    for (unsigned i = 0; i < LED_SCENE_COUNT; i++) {
        out[offset++] = config->led.scenes[i].mode;
        out[offset++] = config->led.scenes[i].color;
    }
    size_t bridge_length = power_bridge_config_encode(&config->bridge, out + offset + 1,
                                                     capacity - offset - 1);
    if (!bridge_length || bridge_length > UINT8_MAX) return 0;
    out[offset++] = (uint8_t)bridge_length;
    offset += bridge_length;
    size_t identity_length = device_identity_encode(identity, out + offset + 1,
                                                    capacity - offset - 1);
    if (!identity_length || identity_length > UINT8_MAX) return 0;
    out[offset++] = (uint8_t)identity_length;
    offset += identity_length;
    put32(out, CONFIG_SNAPSHOT_MAGIC);
    out[4] = CONFIG_SNAPSHOT_VERSION;
    out[6] = (uint8_t)offset;
    out[7] = (uint8_t)(offset >> 8);
    put32(out + 8, generation);
    put32(out + 12, snapshot_crc(out, offset));
    return offset;
}

bool config_snapshot_decode(const uint8_t *data, size_t length, config_data_t *config,
                            device_identity_t *identity, uint32_t *generation)
{
    if (!data || !config || !identity || !generation || length < CONFIG_SNAPSHOT_HEADER_LEN
        || length > CONFIG_SNAPSHOT_MAX_LEN || get32(data) != CONFIG_SNAPSHOT_MAGIC
        || data[4] != CONFIG_SNAPSHOT_VERSION || data[5] != 0
        || ((size_t)data[6] | ((size_t)data[7] << 8)) != length
        || get32(data + 12) != snapshot_crc(data, length)) return false;
    config_data_t next;
    config_store_defaults(&next);
    device_identity_t id;
    size_t offset = CONFIG_SNAPSHOT_HEADER_LEN;
    for (size_t i = 0; i < sizeof(s_fields) / sizeof(s_fields[0]); i++) {
        if (length - offset < 4 || get32(data + offset) > INT32_MAX) return false;
        int *field = (int *)((uint8_t *)&next + s_fields[i]);
        *field = (int)get32(data + offset);
        offset += 4;
    }
    if (!get_string(data, length, &offset, next.custom_pacer_name, sizeof(next.custom_pacer_name))
        || !get_string(data, length, &offset, next.run_custom_name, sizeof(next.run_custom_name))
        || !get_string(data, length, &offset, next.device_name, sizeof(next.device_name))) return false;
    if (length - offset < 1 + LED_SCENE_COUNT * 2) return false;
    next.led.silent = data[offset++];
    for (unsigned i = 0; i < LED_SCENE_COUNT; i++) {
        next.led.scenes[i].mode = data[offset++];
        next.led.scenes[i].color = data[offset++];
    }
    if (offset >= length) return false;
    size_t bridge_length = data[offset++];
    if (bridge_length > length - offset
        || !power_bridge_config_decode(data + offset, bridge_length, &next.bridge)) return false;
    offset += bridge_length;
    if (offset >= length) return false;
    size_t identity_length = data[offset++];
    if (identity_length != length - offset
        || !device_identity_decode(data + offset, identity_length, &id)
        || next.schema_ver != CONFIG_VERSION || config_store_validate(&next)) return false;
    *config = next;
    *identity = id;
    *generation = get32(data + 8);
    return true;
}

void config_snapshot_marker_encode(unsigned slot, uint32_t generation,
                                   uint8_t out[CONFIG_SNAPSHOT_MARKER_LEN])
{
    memset(out, 0, CONFIG_SNAPSHOT_MARKER_LEN);
    put32(out, CONFIG_SNAPSHOT_MAGIC);
    out[4] = CONFIG_SNAPSHOT_VERSION;
    out[5] = (uint8_t)slot;
    put32(out + 8, generation);
    put32(out + 12, config_snapshot_crc(out, 12));
}

bool config_snapshot_marker_decode(const uint8_t *data, size_t length,
                                   unsigned *slot, uint32_t *generation)
{
    if (!data || !slot || !generation || length != CONFIG_SNAPSHOT_MARKER_LEN
        || get32(data) != CONFIG_SNAPSHOT_MAGIC || data[4] != CONFIG_SNAPSHOT_VERSION
        || data[5] > 1 || data[6] || data[7]
        || get32(data + 12) != config_snapshot_crc(data, 12)) return false;
    *slot = data[5];
    *generation = get32(data + 8);
    return true;
}
