#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cps_input_codec.h"
#include "power_bridge.h"
#include "ble_config_codec.h"
#include "ble_payload_codec.h"

static bridge_device_t peer(uint8_t type, uint8_t tail, const char *name)
{
    bridge_device_t item = { .bound = 1, .addr_type = type };
    item.addr[5] = tail;
    strncpy(item.name, name, sizeof(item.name) - 1);
    return item;
}

static void multi_device_config_cases(void)
{
    bridge_config_t config;
    power_bridge_config_defaults(&config);
    config.source = BRIDGE_SOURCE_REAL;
    config.device_count = 3;
    config.devices[0] = peer(0, 0x11, "Primary power meter");
    config.devices[1] = peer(1, 0x22, "Backup power meter");
    config.devices[2] = peer(1, 0x22, "Duplicate");
    assert(power_bridge_config_validate(&config));
    assert(config.device_count == 2);
    assert(config.devices[0].addr[5] == 0x11);
    assert(config.devices[1].addr[5] == 0x22);

    uint8_t wire[BRIDGE_WIRE_MAX_LEN];
    size_t length = power_bridge_config_encode(&config, wire, sizeof(wire));
    assert(length > 47 && wire[0] == BRIDGE_WIRE_VERSION);
    bridge_config_t restored;
    assert(power_bridge_config_decode(wire, length, &restored));
    assert(power_bridge_config_equal(&config, &restored));

    config.calibration_mode = BRIDGE_CALIBRATION_OFFSET;
    config.watts = 200;
    assert(power_bridge_correct(&config, 0) == 0);
    config.calibration_mode = BRIDGE_CALIBRATION_PERCENT;
    config.percent_x10 = 1500;
    assert(power_bridge_correct(&config, 0) == 0);
}

static void legacy_wire_migration_cases(void)
{
    static const uint8_t legacy[] = {
        BRIDGE_WIRE_LEGACY_VERSION,
        BRIDGE_SOURCE_REAL,
        1,
        1,
        1, 2, 3, 4, 5, 6,
        BRIDGE_CALIBRATION_OFFSET,
        0xe7, 0xff,
        0xe8, 0x03,
        6,
        'L', 'e', 'g', 'a', 'c', 'y',
    };
    bridge_config_t restored;
    assert(power_bridge_config_decode(legacy, sizeof(legacy), &restored));
    assert(restored.source == BRIDGE_SOURCE_REAL);
    assert(restored.device_count == 1);
    assert(restored.devices[0].bound);
    assert(restored.devices[0].addr_type == 1);
    assert(memcmp(restored.devices[0].addr, legacy + 4, 6) == 0);
    assert(strcmp(restored.devices[0].name, "Legacy") == 0);
    assert(restored.calibration_mode == BRIDGE_CALIBRATION_OFFSET);
    assert(restored.watts == -25);
    assert(restored.percent_x10 == 1000);
}

static void ble_config_group_cases(void)
{
    assert(BLE_CONFIG_GROUP_COUNT == 8);
    assert(BLE_CONFIG_GROUP_MASK_ALL == 0xff);

    config_data_t source;
    config_store_defaults(&source);
    source.bridge.source = BRIDGE_SOURCE_REAL;
    source.bridge.calibration_mode = BRIDGE_CALIBRATION_OFFSET;
    source.bridge.watts = -25;
    source.bridge.device_count = 3;
    source.bridge.devices[0] = peer(0, 0x11, "Primary");
    source.bridge.devices[1] = peer(1, 0x22, "Backup one");
    source.bridge.devices[2] = peer(0, 0x33, "Backup two");

    uint8_t payload[BLE_CONFIG_GROUP_MAX_LEN];
    size_t primary_len = ble_config_encode_group(BLE_CONFIG_GROUP_BRIDGE,
                                                  &source, payload, sizeof(payload));
    assert(primary_len == 16 + strlen(source.bridge.devices[0].name));
    assert(primary_len <= BLE_CONFIG_GROUP_MAX_LEN);
    assert(payload[0] == BLE_CONFIG_PROTOCOL_VERSION);
    assert(payload[11] == 0xe7 && payload[12] == 0xff);

    config_data_t decoded = source;
    decoded.bridge.devices[0] = peer(1, 0x44, "Old primary");
    assert(ble_config_decode_group(BLE_CONFIG_GROUP_BRIDGE,
                                   payload, primary_len, &decoded));
    assert(bridge_device_equal(&decoded.bridge.devices[0],
                               &source.bridge.devices[0]));
    assert(strcmp(decoded.bridge.devices[1].name, "Backup one") == 0);
    assert(strcmp(decoded.bridge.devices[2].name, "Backup two") == 0);

    const ble_config_group_t backup_groups[] = {
        BLE_CONFIG_GROUP_BRIDGE_BACKUP_1,
        BLE_CONFIG_GROUP_BRIDGE_BACKUP_2,
    };
    for (int i = 0; i < 2; i++) {
        int slot = i + 1;
        size_t length = ble_config_encode_group(backup_groups[i], &source,
                                                payload, sizeof(payload));
        assert(length > 0 && length <= BLE_CONFIG_GROUP_MAX_LEN);
        decoded.bridge.devices[slot] = peer(1, (uint8_t)(0x70 + slot), "Old");
        assert(ble_config_decode_group(backup_groups[i], payload, length, &decoded));
        assert(bridge_device_equal(&decoded.bridge.devices[slot],
                                   &source.bridge.devices[slot]));
        assert(strcmp(decoded.bridge.devices[slot].name,
                      source.bridge.devices[slot].name) == 0);
    }

    config_data_t removal = source;
    memset(&removal.bridge.devices[1], 0, sizeof(removal.bridge.devices[1]));
    size_t removal_len = ble_config_encode_group(BLE_CONFIG_GROUP_BRIDGE_BACKUP_1,
                                                  &removal, payload, sizeof(payload));
    assert(removal_len > 0);
    decoded = source;
    assert(ble_config_decode_group(BLE_CONFIG_GROUP_BRIDGE_BACKUP_1,
                                   payload, removal_len, &decoded));
    assert(!decoded.bridge.devices[1].bound);
    assert(decoded.bridge.devices[2].bound);
    assert(config_store_validate(&decoded));
    assert(decoded.bridge.device_count == 2);
    assert(decoded.bridge.devices[1].addr[5] == 0x33);
}

static void output_cases(void)
{
    uint8_t out[10] = {0};
    assert(ble_payload_encode_cps_bridge(out, sizeof(out), false, 250, 12, 4660) == 4);
    const uint8_t power_only[] = {0, 0, 0xfa, 0};
    assert(memcmp(out, power_only, sizeof(power_only)) == 0);
    assert(ble_payload_encode_cps_bridge(out, sizeof(out), true, 250, 12, 4660) == 8);
    const uint8_t with_crank[] = {0x20, 0, 0xfa, 0, 12, 0, 0x34, 0x12};
    assert(memcmp(out, with_crank, sizeof(with_crank)) == 0);
    assert(ble_payload_encode_ftms_bridge(out, sizeof(out), true, true, true, 180, 250, 150) == 7);
    const uint8_t all[] = {0x45, 0x02, 0xb4, 0, 0xfa, 0, 150};
    assert(memcmp(out, all, sizeof(all)) == 0);
    assert(ble_payload_encode_ftms_bridge(out, sizeof(out), false, true, false, 0, 0, 0) == 4);
    const uint8_t stale[] = {0x41, 0, 0, 0};
    assert(memcmp(out, stale, sizeof(stale)) == 0);
    assert(ble_payload_encode_ftms_bridge(out, 6, true, true, true, 180, 250, 150) == 0);
}

static void codec_cases(void)
{
    cps_input_t input = {0};
    const uint8_t simple[] = {0, 0, 0xfb, 0xff};
    assert(cps_input_decode(simple, sizeof(simple), &input));
    assert(input.power_w == -5 && !input.has_crank);
    const uint8_t crank[] = {0x20, 0, 200, 0, 0xff, 0xff, 0xff, 0xff};
    assert(cps_input_decode(crank, sizeof(crank), &input));
    assert(input.has_crank && input.crank_revolutions == 65535);
    assert(input.crank_event_time_1024 == 65535);
    for (size_t n = 0; n < sizeof(crank); n++) {
        cps_input_t saved = input;
        assert(!cps_input_decode(crank, n, &input));
        assert(memcmp(&saved, &input, sizeof(input)) == 0);
    }
    const uint8_t optional[] = {
        0x35, 0, 123, 0, 50, 1, 2, 3, 4, 5, 6, 7, 8, 42, 0, 0, 4
    };
    assert(cps_input_decode(optional, sizeof(optional), &input));
    assert(input.power_w == 123 && input.crank_revolutions == 42);
    assert(input.crank_event_time_1024 == 1024);
    uint8_t all[34] = {0xf5, 0x1f, 10, 0};
    assert(cps_input_decode(all, sizeof(all), &input));
    for (size_t n = 4; n < sizeof(all); n++) assert(!cps_input_decode(all, n, &input));
    all[1] |= 0x80;
    assert(!cps_input_decode(all, sizeof(all), &input));
    assert(!cps_input_decode(NULL, 4, &input));
}

static void runtime_cases(void)
{
    bridge_config_t config;
    power_bridge_config_defaults(&config);
    assert(config.source == BRIDGE_SOURCE_SIMULATED && config.device_count == 0);
    assert(power_bridge_correct(&config, 123) == 123);
    config.calibration_mode = BRIDGE_CALIBRATION_OFFSET;
    config.watts = 25;
    assert(power_bridge_correct(&config, 0) == 0);
    assert(power_bridge_correct(&config, -50) == 0);
    assert(power_bridge_correct(&config, 123) == 148);
    assert(power_bridge_correct(&config, 1990) == 2000);
    config.watts = -200;
    assert(power_bridge_correct(&config, 123) == 0);
    config.calibration_mode = BRIDGE_CALIBRATION_PERCENT;
    config.percent_x10 = 1050;
    assert(power_bridge_correct(&config, 101) == 106);
    assert(power_bridge_correct(&config, 0) == 0);
    config.percent_x10 = 500;
    assert(power_bridge_correct(&config, 2500) == 1250);
    config.percent_x10 = 1050;

    power_bridge_t bridge;
    power_bridge_init(&bridge);
    cps_input_t sample = { .power_w = 200, .has_crank = true,
                          .crank_revolutions = 65535, .crank_event_time_1024 = 65000 };
    power_bridge_receive(&bridge, &sample, 100);
    assert(!power_bridge_snapshot(&bridge, &config, 100).valid);
    sample.crank_revolutions = 0;
    sample.crank_event_time_1024 = 488;
    power_bridge_receive(&bridge, &sample, 1100);
    bridge_snapshot_t snap = power_bridge_snapshot(&bridge, &config, 1100);
    assert(snap.valid && snap.raw_power_w == 200 && snap.power_w == 210);
    assert(snap.cadence_valid && snap.cadence_half_rpm == 120);
    power_bridge_receive(&bridge, &sample, 2100);
    assert(power_bridge_snapshot(&bridge, &config, 2100).cadence_valid);
    power_bridge_receive(&bridge, &sample, 4100);
    assert(!power_bridge_snapshot(&bridge, &config, 4100).cadence_valid);
    assert(power_bridge_snapshot(&bridge, &config, 7099).valid);
    snap = power_bridge_snapshot(&bridge, &config, 7100);
    assert(!snap.valid && snap.power_w == 0 && !snap.cadence_valid);
    power_bridge_receive(&bridge, &sample, 7200);
    assert(!power_bridge_snapshot(&bridge, &config, 7200).valid);
    power_bridge_receive(&bridge, &sample, 8200);
    assert(power_bridge_snapshot(&bridge, &config, 8200).valid);
    power_bridge_disconnect(&bridge);
    assert(!power_bridge_snapshot(&bridge, &config, 8200).valid);
    sample.has_crank = false;
    power_bridge_receive(&bridge, &sample, 9000);
    power_bridge_receive(&bridge, &sample, 10000);
    snap = power_bridge_snapshot(&bridge, &config, 10000);
    assert(snap.valid && !snap.has_crank && !snap.cadence_valid);

    power_bridge_init(&bridge);
    sample.has_crank = true;
    sample.crank_revolutions = 10;
    sample.crank_event_time_1024 = 100;
    power_bridge_receive(&bridge, &sample, 100);
    sample.crank_revolutions++;
    power_bridge_receive(&bridge, &sample, 1100);
    assert(!power_bridge_snapshot(&bridge, &config, 1100).cadence_valid);
    sample.crank_event_time_1024++;
    sample.crank_revolutions++;
    power_bridge_receive(&bridge, &sample, 2100);
    assert(!power_bridge_snapshot(&bridge, &config, 2100).cadence_valid);
    sample.power_w = 0;
    power_bridge_receive(&bridge, &sample, 3100);
    snap = power_bridge_snapshot(&bridge, &config, 3100);
    assert(snap.valid && snap.power_w == 0 && !snap.cadence_valid);
}

static void config_cases(void)
{
    config_data_t c;
    config_store_defaults(&c);
    c.schema_ver = 11;
    c.power = 333;
    c.power_float = 5;
    assert(config_store_migrate(&c));
    assert(c.schema_ver == 14 && c.power == 333 && c.power_float == 50);
    assert(c.bridge.source == BRIDGE_SOURCE_SIMULATED && c.bridge.device_count == 0);
    c.schema_ver = 12;
    c.power_float = 17;
    assert(config_store_migrate(&c));
    assert(c.schema_ver == 14 && c.power_float == 17);

    c.bridge.source = BRIDGE_SOURCE_REAL;
    c.bridge.device_count = 1;
    c.bridge.devices[0].bound = 1;
    c.bridge.devices[0].addr_type = 1;
    for (int i = 0; i < 6; i++) c.bridge.devices[0].addr[i] = (uint8_t)(i + 1);
    c.bridge.calibration_mode = BRIDGE_CALIBRATION_OFFSET;
    c.bridge.watts = -25;
    strcpy(c.bridge.devices[0].name, "功率计 \"A\"");
    uint8_t wire[BLE_CONFIG_GROUP_MAX_LEN];
    size_t n = ble_config_encode_group(BLE_CONFIG_GROUP_BRIDGE, &c, wire, sizeof(wire));
    assert(n == 16 + strlen(c.bridge.devices[0].name));
    uint8_t invalid_name[BRIDGE_WIRE_MAX_LEN];
    memcpy(invalid_name, wire, n);
    invalid_name[16] = 0xc0;
    bridge_config_t invalid_config;
    assert(power_bridge_config_decode(invalid_name, n, &invalid_config));
    assert(invalid_config.device_count == 0);
    assert(invalid_config.source == BRIDGE_SOURCE_SIMULATED);
    assert(wire[11] == 0xe7 && wire[12] == 0xff);
    config_data_t restored;
    config_store_defaults(&restored);
    assert(ble_config_decode_group(BLE_CONFIG_GROUP_BRIDGE, wire, n, &restored));
    assert(power_bridge_config_equal(&c.bridge, &restored.bridge));
    for (size_t cut = 0; cut < n; cut++) {
        assert(!ble_config_decode_group(BLE_CONFIG_GROUP_BRIDGE, wire, cut, &restored));
    }
    char json[2048];
    assert(config_store_serialize(&c, json, sizeof(json)) > 0);
    assert(config_store_deserialize(json, &restored));
    assert(power_bridge_config_equal(&c.bridge, &restored.bridge));
    power_engine_t engine;
    power_engine_init(&engine);
    assert(config_store_apply_to_engine(&c, &engine));
    assert(!power_engine_set_erg_target(&engine, 250));
    config_store_from_engine(&engine, &restored);
    assert(power_bridge_config_equal(&c.bridge, &restored.bridge));
    c.bridge.device_count = 0;
    memset(c.bridge.devices, 0, sizeof(c.bridge.devices));
    assert(config_store_validate(&c));
    assert(c.bridge.source == BRIDGE_SOURCE_SIMULATED);
}

int main(void)
{
    multi_device_config_cases();
    legacy_wire_migration_cases();
    ble_config_group_cases();
    codec_cases();
    runtime_cases();
    config_cases();
    output_cases();
    puts("桥接领域测试通过：解析、失效、曲柄、校正、迁移与配置往返");
    return 0;
}
