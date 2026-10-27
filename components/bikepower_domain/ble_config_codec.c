#include "ble_config_codec.h"

#include <string.h>

static void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
    out[2] = (uint8_t)((value >> 16) & 0xffu);
    out[3] = (uint8_t)(value >> 24);
}

static uint16_t get_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static size_t encode_string(uint8_t *out, size_t out_size, size_t offset,
                            const char *value, size_t value_size)
{
    size_t len = strnlen(value, value_size);
    if (len == 0 || len >= value_size || len > UINT8_MAX || offset + 1 + len > out_size) {
        return 0;
    }
    out[offset] = (uint8_t)len;
    memcpy(out + offset + 1, value, len);
    return offset + 1 + len;
}

static bool decode_string(const uint8_t *data, size_t len, size_t offset,
                          char *out, size_t out_size)
{
    if (offset >= len || out_size < 2) return false;
    size_t value_len = data[offset];
    if (value_len == 0 || value_len >= out_size || offset + 1 + value_len != len) return false;
    if (memchr(data + offset + 1, '\0', value_len) != NULL) return false;
    memcpy(out, data + offset + 1, value_len);
    out[value_len] = '\0';
    return true;
}

size_t ble_config_encode_meta(uint8_t *out, size_t out_size, uint32_t revision)
{
    if (!out || out_size < BLE_CONFIG_META_LEN) return 0;
    out[0] = BLE_CONFIG_PROTOCOL_VERSION;
    out[1] = CONFIG_VERSION;
    out[2] = BLE_CONFIG_GROUP_MASK_ALL;
    out[3] = BLE_CONFIG_CAPABILITIES;
    put_u32(out + 4, revision);
    put_u16(out + 8, BLE_CONFIG_GROUP_MAX_LEN);
    put_u16(out + 10, 0);
    return BLE_CONFIG_META_LEN;
}

size_t ble_config_encode_group(ble_config_group_t group, const config_data_t *c,
                               uint8_t *out, size_t out_size)
{
    if (!c || !out) return 0;
    switch (group) {
    case BLE_CONFIG_GROUP_CORE:
        if (out_size < BLE_CONFIG_CORE_LEN) return 0;
        out[0] = BLE_CONFIG_PROTOCOL_VERSION;
        put_u16(out + 1, (uint16_t)c->power);
        put_u16(out + 3, (uint16_t)c->cadence);
        out[5] = (uint8_t)c->mode;
        put_u16(out + 6, (uint16_t)c->heartrate);
        out[8] = (uint8_t)c->has_user_hr;
        out[9] = (uint8_t)c->power_float;
        out[10] = (uint8_t)c->cadence_float;
        out[11] = (uint8_t)c->hr_float;
        out[12] = (uint8_t)c->power_step;
        out[13] = (uint8_t)c->sport_profile;
        out[14] = (uint8_t)c->broadcast_mask;
        return BLE_CONFIG_CORE_LEN;
    case BLE_CONFIG_GROUP_PROFILE:
        if (out_size < 11) return 0;
        out[0] = BLE_CONFIG_PROTOCOL_VERSION;
        put_u16(out + 1, (uint16_t)c->weight);
        put_u16(out + 3, (uint16_t)c->ftp);
        put_u16(out + 5, (uint16_t)c->thr_hr);
        out[7] = (uint8_t)c->ride_type;
        out[8] = (uint8_t)c->custom_wkg_low;
        out[9] = (uint8_t)c->custom_wkg_high;
        return encode_string(out, out_size, 10, c->custom_pacer_name,
                             sizeof(c->custom_pacer_name));
    case BLE_CONFIG_GROUP_RUNNING:
        if (out_size < 15) return 0;
        out[0] = BLE_CONFIG_PROTOCOL_VERSION;
        put_u16(out + 1, (uint16_t)c->run_speed_x100);
        out[3] = (uint8_t)c->run_pace_float_sec;
        put_u16(out + 4, (uint16_t)c->run_cadence);
        out[6] = (uint8_t)c->run_cadence_float;
        out[7] = (uint8_t)c->run_mode;
        out[8] = (uint8_t)c->run_pacer_type;
        put_u16(out + 9, (uint16_t)c->run_pace_low_sec);
        put_u16(out + 11, (uint16_t)c->run_pace_high_sec);
        return encode_string(out, out_size, 13, c->run_custom_name,
                             sizeof(c->run_custom_name));
    case BLE_CONFIG_GROUP_LED:
        if (out_size < BLE_CONFIG_LED_LEN) return 0;
        out[0] = BLE_CONFIG_PROTOCOL_VERSION;
        out[1] = c->led.silent;
        for (int i = 0; i < LED_SCENE_COUNT; i++) {
            out[2 + i * 2] = c->led.scenes[i].mode;
            out[3 + i * 2] = c->led.scenes[i].color;
        }
        return BLE_CONFIG_LED_LEN;
    case BLE_CONFIG_GROUP_IDENTITY:
        if (out_size < 2) return 0;
        out[0] = BLE_CONFIG_PROTOCOL_VERSION;
        return encode_string(out, out_size, 1, c->device_name, sizeof(c->device_name));
    default:
        return 0;
    }
}

bool ble_config_decode_group(ble_config_group_t group, const uint8_t *data, size_t len,
                             config_data_t *c)
{
    if (!data || !c || len < 1 || data[0] != BLE_CONFIG_PROTOCOL_VERSION) return false;
    config_data_t next = *c;
    switch (group) {
    case BLE_CONFIG_GROUP_CORE:
        if (len != BLE_CONFIG_CORE_LEN) return false;
        next.power = get_u16(data + 1);
        next.cadence = get_u16(data + 3);
        next.mode = data[5];
        next.heartrate = get_u16(data + 6);
        next.has_user_hr = data[8];
        next.power_float = data[9];
        next.cadence_float = data[10];
        next.hr_float = data[11];
        next.power_step = data[12];
        next.sport_profile = data[13];
        next.broadcast_mask = data[14];
        break;
    case BLE_CONFIG_GROUP_PROFILE:
        if (len < 12) return false;
        next.weight = get_u16(data + 1);
        next.ftp = get_u16(data + 3);
        next.thr_hr = get_u16(data + 5);
        next.ride_type = data[7];
        next.custom_wkg_low = data[8];
        next.custom_wkg_high = data[9];
        if (!decode_string(data, len, 10, next.custom_pacer_name,
                           sizeof(next.custom_pacer_name))) return false;
        break;
    case BLE_CONFIG_GROUP_RUNNING:
        if (len < 15) return false;
        next.run_speed_x100 = get_u16(data + 1);
        next.run_pace_float_sec = data[3];
        next.run_cadence = get_u16(data + 4);
        next.run_cadence_float = data[6];
        next.run_mode = data[7];
        next.run_pacer_type = data[8];
        next.run_pace_low_sec = get_u16(data + 9);
        next.run_pace_high_sec = get_u16(data + 11);
        if (!decode_string(data, len, 13, next.run_custom_name,
                           sizeof(next.run_custom_name))) return false;
        break;
    case BLE_CONFIG_GROUP_LED:
        if (len != BLE_CONFIG_LED_LEN) return false;
        next.led.silent = data[1];
        for (int i = 0; i < LED_SCENE_COUNT; i++) {
            next.led.scenes[i].mode = data[2 + i * 2];
            next.led.scenes[i].color = data[3 + i * 2];
        }
        break;
    case BLE_CONFIG_GROUP_IDENTITY:
        if (!decode_string(data, len, 1, next.device_name,
                           sizeof(next.device_name))) return false;
        break;
    default:
        return false;
    }
    *c = next;
    return true;
}

size_t ble_config_encode_control_response(uint8_t *out, size_t out_size,
                                          uint8_t request_opcode, uint8_t result,
                                          uint8_t restart_flags, uint32_t transaction_id,
                                          uint32_t revision)
{
    if (!out || out_size < BLE_CONFIG_CONTROL_RESPONSE_LEN) return 0;
    out[0] = BLE_CONFIG_OP_RESPONSE;
    out[1] = request_opcode;
    out[2] = result;
    out[3] = restart_flags;
    put_u32(out + 4, transaction_id);
    put_u32(out + 8, revision);
    return BLE_CONFIG_CONTROL_RESPONSE_LEN;
}
