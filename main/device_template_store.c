/**
 * BLE 设备信息模板 NVS 持久化实现。
 */
#include "device_template_store.h"
#include "bikepower_config.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "TPL";

static SemaphoreHandle_t s_tpl_mutex = NULL;
static StaticSemaphore_t s_tpl_mutex_buf;

static void ensure_mutex(void)
{
    if (s_tpl_mutex == NULL) {
        s_tpl_mutex = xSemaphoreCreateMutexStatic(&s_tpl_mutex_buf);
    }
}

static void key_for_slot(int slot, char *out, size_t out_sz)
{
    snprintf(out, out_sz, "%s%d", NVS_TEMPLATE_KEY_PREFIX, slot);
}

void device_template_store_load(device_template_list_t *list)
{
    if (!list) return;
    device_template_list_defaults(list);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_TEMPLATE_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "设备信息模板为空");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "打开模板 NVS 失败(%s)", esp_err_to_name(err));
        return;
    }
    for (int i = 0; i < DEVICE_TEMPLATE_MAX; i++) {
        char key[8];
        key_for_slot(i, key, sizeof(key));
        size_t len = sizeof(device_template_t);
        device_template_t tpl;
        device_template_defaults(&tpl);
        err = nvs_get_blob(h, key, &tpl, &len);
        if (err == ESP_ERR_NVS_NOT_FOUND) continue;
        if (err != ESP_OK || len != sizeof(device_template_t)) {
            ESP_LOGW(TAG, "读取模板 %d 失败(%s len=%u)", i, esp_err_to_name(err), (unsigned)len);
            continue;
        }
        tpl.used = true;
        tpl.id = (uint8_t)i;
        device_template_validate(&tpl);
        list->items[i] = tpl;
    }
    nvs_close(h);
    device_template_list_recount(list);
    ESP_LOGI(TAG, "设备信息模板加载完成 count=%u", (unsigned)list->count);
}

bool device_template_store_save_list(const device_template_list_t *list)
{
    if (!list) return false;
    ensure_mutex();
    if (xSemaphoreTake(s_tpl_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "模板保存互斥锁等待超时");
        return false;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_TEMPLATE_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "打开模板 NVS 失败(%s)", esp_err_to_name(err));
        xSemaphoreGive(s_tpl_mutex);
        return false;
    }
    bool ok = true;
    for (int i = 0; i < DEVICE_TEMPLATE_MAX; i++) {
        char key[8];
        key_for_slot(i, key, sizeof(key));
        if (list->items[i].used) {
            device_template_t tpl = list->items[i];
            tpl.id = (uint8_t)i;
            device_template_validate(&tpl);
            err = nvs_set_blob(h, key, &tpl, sizeof(tpl));
        } else {
            err = nvs_erase_key(h, key);
            if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "写入模板 %s 失败(%s)", key, esp_err_to_name(err));
            ok = false;
        }
    }
    if (ok) {
        err = nvs_commit(h);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "模板 NVS commit 失败(%s)", esp_err_to_name(err));
            ok = false;
        }
    }
    nvs_close(h);
    xSemaphoreGive(s_tpl_mutex);
    return ok;
}

bool device_template_store_add(const device_template_t *tpl, int *slot_out)
{
    if (slot_out) *slot_out = -1;
    device_template_list_t list;
    device_template_store_load(&list);
    int slot = device_template_list_add(&list, tpl);
    if (slot < 0) return false;
    if (!device_template_store_save_list(&list)) return false;
    if (slot_out) *slot_out = slot;
    return true;
}

bool device_template_store_delete(uint8_t id)
{
    device_template_list_t list;
    device_template_store_load(&list);
    if (!device_template_list_delete(&list, id)) return false;
    return device_template_store_save_list(&list);
}
