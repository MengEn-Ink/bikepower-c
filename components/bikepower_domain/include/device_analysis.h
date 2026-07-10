/**
 * 外部 BLE 运动设备分析纯逻辑。
 * 固件侧负责采样字节流，领域层负责解析和生成可展示摘要。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_template.h"

#define DEVICE_ANALYSIS_STATUS_MAX 32
#define DEVICE_ANALYSIS_SUMMARY_MAX 96
#define DEVICE_ANALYSIS_RAW_HEX_MAX 65

typedef struct {
    uint16_t sample_count;
    uint16_t service_flags;
    int16_t power_watts;
    uint16_t cadence_rpm;
    uint16_t heart_rate_bpm;
    uint16_t speed_x10_kmh;
    uint8_t ftms_control_supported;
    char last_cps_hex[DEVICE_ANALYSIS_RAW_HEX_MAX];
    char last_hrs_hex[DEVICE_ANALYSIS_RAW_HEX_MAX];
    char last_csc_hex[DEVICE_ANALYSIS_RAW_HEX_MAX];
    char last_ftms_hex[DEVICE_ANALYSIS_RAW_HEX_MAX];
    char ftms_feature_hex[DEVICE_ANALYSIS_RAW_HEX_MAX];
    char status[DEVICE_ANALYSIS_STATUS_MAX];
    char summary[DEVICE_ANALYSIS_SUMMARY_MAX];
} device_analysis_t;

void device_analysis_defaults(device_analysis_t *a);
bool device_analysis_parse_cps(device_analysis_t *a, const uint8_t *data, size_t len);
bool device_analysis_parse_hr(device_analysis_t *a, const uint8_t *data, size_t len);
bool device_analysis_parse_csc(device_analysis_t *a, const uint8_t *data, size_t len);
bool device_analysis_parse_ftms_feature(device_analysis_t *a, const uint8_t *data, size_t len);
bool device_analysis_parse_ftms_bike(device_analysis_t *a, const uint8_t *data, size_t len);
void device_analysis_finalize(device_analysis_t *a);
size_t device_analysis_serialize_json(const device_analysis_t *a, char *out, size_t out_sz);
