#pragma once

#include "cps_input_codec.h"
#include "domain_config.h"

typedef enum {
    BRIDGE_SOURCE_SIMULATED = 0,
    BRIDGE_SOURCE_REAL = 1,
} bridge_source_t;

typedef enum {
    BRIDGE_CALIBRATION_NONE = 0,
    BRIDGE_CALIBRATION_OFFSET = 1,
    BRIDGE_CALIBRATION_PERCENT = 2,
} bridge_calibration_t;

typedef struct {
    int bound;
    int addr_type;
    uint8_t addr[6];
    char name[BRIDGE_NAME_MAX];
} bridge_device_t;

typedef struct {
    int source;
    int calibration_mode;
    int watts;
    int percent_x10;
    int device_count;
    bridge_device_t devices[BRIDGE_DEVICE_MAX];
} bridge_config_t;

typedef struct {
    cps_input_t input;
    uint32_t last_sample_ms;
    uint32_t last_crank_ms;
    uint16_t cadence_half_rpm;
    uint8_t valid_samples;
    bool crank_baseline;
    bool cadence_valid;
} power_bridge_t;

typedef struct {
    bool valid;
    bool has_crank;
    bool cadence_valid;
    int raw_power_w;
    int power_w;
    uint16_t cadence_half_rpm;
    uint16_t crank_revolutions;
    uint16_t crank_event_time_1024;
    uint32_t age_ms;
} bridge_snapshot_t;

void power_bridge_config_defaults(bridge_config_t *config);
/* 修正非法配置并返回是否变化；无绑定时不能选择真实来源。 */
bool power_bridge_config_validate(bridge_config_t *config);
bool power_bridge_config_equal(const bridge_config_t *a, const bridge_config_t *b);
bool bridge_device_equal(const bridge_device_t *a, const bridge_device_t *b);
const bridge_device_t *power_bridge_device(const bridge_config_t *config, int slot);
int power_bridge_find_device(const bridge_config_t *config, const bridge_device_t *device);
int power_bridge_correct(const bridge_config_t *config, int raw_power_w);
/* 完整设备表的显式小端 NVS 编码；不包含结构体 padding。 */
size_t power_bridge_config_encode(const bridge_config_t *config, uint8_t *out, size_t capacity);
bool power_bridge_config_decode(const uint8_t *data, size_t length, bridge_config_t *out);
void power_bridge_init(power_bridge_t *bridge);
void power_bridge_disconnect(power_bridge_t *bridge);
void power_bridge_receive(power_bridge_t *bridge, const cps_input_t *input, uint32_t now_ms);
bridge_snapshot_t power_bridge_snapshot(const power_bridge_t *bridge,
                                       const bridge_config_t *config, uint32_t now_ms);
