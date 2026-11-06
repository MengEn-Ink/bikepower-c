/**
 * DIS 设备身份覆盖 NVS 持久化实现。
 * 以线格式（device_identity_encode）作为 blob 存储，读取时复用 decode 做完整校验。
 */
#include "device_identity_store.h"
#include "bikepower_config.h"
#include "persistent_store.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "IDENT";

bool device_identity_store_load_legacy(device_identity_t *id)
{
    if (!id) return false;
    device_identity_defaults(id);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_IDENTITY_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "打开身份覆盖 NVS 失败(%s)", esp_err_to_name(err));
        return false;
    }
    uint8_t buf[DEVICE_IDENTITY_WIRE_MAX];
    size_t len = sizeof(buf);
    err = nvs_get_blob(h, NVS_IDENTITY_KEY, buf, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK || !device_identity_decode(buf, len, id)) {
        ESP_LOGW(TAG, "身份覆盖数据无效(%s)，回退内置 DIS", esp_err_to_name(err));
        device_identity_defaults(id);
        return false;
    }
    if (device_identity_has_override(id)) {
        ESP_LOGI(TAG, "已加载 DIS 身份覆盖 厂商=%s 型号=%s",
                 id->fields[DEVICE_IDENTITY_MANUFACTURER],
                 id->fields[DEVICE_IDENTITY_MODEL]);
    }
    return true;
}

void device_identity_store_load(device_identity_t *id)
{
    if (!id || persistent_store_identity(id)) return;
    device_identity_store_load_legacy(id);
}

bool device_identity_store_save(const device_identity_t *id)
{
    return persistent_store_save_identity(id);
}
