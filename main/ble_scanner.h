#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "power_bridge.h"

esp_err_t ble_scanner_start(uint32_t duration_ms);
esp_err_t ble_scanner_stop(void);
bool ble_scanner_is_scanning(void);
size_t ble_scanner_serialize_results(char *out, size_t out_sz);
/* 候选编号包含扫描代次；拒绝旧代次、非 CPS 设备与未知地址类型。 */
bool ble_scanner_candidate(uint32_t candidate, bridge_config_t *out);
