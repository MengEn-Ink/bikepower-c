#include <stddef.h>
#include "esp_idf_version.h"
#include "ble_hs_priv.h"
#include "ble_gatt_priv.h"
#include "nimble_adapter.h"

_Static_assert(NIMBLE_ADAPTER_CCCD_INDICATE_FLAG == BLE_GATTS_CLT_CFG_F_INDICATE,
               "NimBLE CCCD indication flag changed");

#if ESP_IDF_VERSION_MAJOR != 5 || ESP_IDF_VERSION_MINOR != 5
#error "NimBLE adapter only verified for ESP-IDF 5.5; review private ABI before upgrading"
#endif

bool nimble_adapter_indication_idle(uint16_t connection)
{
    ble_hs_lock();
    struct ble_hs_conn *host_connection = ble_hs_conn_find(connection);
    bool idle = host_connection != NULL
        && host_connection->bhc_gatt_svr.indicate_val_handle == 0;
    ble_hs_unlock();
    return idle;
}
