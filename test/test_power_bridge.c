#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "cps_input_codec.h"
#include "power_bridge.h"
#include "ble_config_codec.h"
#include "ble_payload_codec.h"

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
    assert(config.source == BRIDGE_SOURCE_SIMULATED && !config.bound);
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
    assert(c.schema_ver == 13 && c.power == 333 && c.power_float == 50);
    assert(c.bridge.source == BRIDGE_SOURCE_SIMULATED && !c.bridge.bound);
    c.schema_ver = 12;
    c.power_float = 17;
    assert(config_store_migrate(&c));
    assert(c.schema_ver == 13 && c.power_float == 17);

    c.bridge.source = BRIDGE_SOURCE_REAL;
    c.bridge.bound = 1;
    c.bridge.addr_type = 1;
    for (int i = 0; i < 6; i++) c.bridge.addr[i] = (uint8_t)(i + 1);
    c.bridge.calibration_mode = BRIDGE_CALIBRATION_OFFSET;
    c.bridge.watts = -25;
    strcpy(c.bridge.name, "功率计 \"A\"");
    uint8_t wire[BLE_CONFIG_GROUP_MAX_LEN];
    size_t n = ble_config_encode_group(BLE_CONFIG_GROUP_BRIDGE, &c, wire, sizeof(wire));
    assert(n == 16 + strlen(c.bridge.name));
    uint8_t invalid_name[BRIDGE_WIRE_MAX_LEN];
    memcpy(invalid_name, wire, n);
    invalid_name[16] = 0xc0;
    bridge_config_t invalid_config;
    assert(!power_bridge_config_decode(invalid_name, n, &invalid_config));
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
    c.bridge.bound = 0;
    assert(config_store_validate(&c));
    assert(c.bridge.source == BRIDGE_SOURCE_SIMULATED);
}

int main(void)
{
    codec_cases();
    runtime_cases();
    config_cases();
    output_cases();
    puts("桥接领域测试通过：解析、失效、曲柄、校正、迁移与配置往返");
    return 0;
}
