#pragma once

#include "esp_err.h"
#include "device_template.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

esp_err_t ble_scanner_start(uint32_t duration_ms);
esp_err_t ble_scanner_stop(void);
bool ble_scanner_is_scanning(void);
size_t ble_scanner_serialize_results(char *out, size_t out_sz);
esp_err_t ble_scanner_read_template(const char *addr, uint8_t addr_type, uint32_t timeout_ms, device_template_t *out);
