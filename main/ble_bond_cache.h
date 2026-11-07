/** GATT布局变更时仅使旧订阅缓存失效，保留绑定密钥与用户配置。 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "host/ble_gatt.h"

void ble_bond_cache_begin(void);
void ble_bond_cache_register(struct ble_gatt_register_ctxt *ctxt, void *arg);
esp_err_t ble_bond_cache_sync(bool *changed);
