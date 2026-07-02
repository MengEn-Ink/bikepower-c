/**
 * BLE 设备信息模板 NVS 持久化。
 */
#pragma once

#include "device_template.h"
#include <stdbool.h>
#include <stdint.h>

void device_template_store_load(device_template_list_t *list);
bool device_template_store_save_list(const device_template_list_t *list);
bool device_template_store_add(const device_template_t *tpl, int *slot_out);
bool device_template_store_delete(uint8_t id);
