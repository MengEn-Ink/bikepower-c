/**
 * BikePower C — 应用入口
 * 运行环境：ESP-IDF v5.5.1 + NimBLE，目标芯片 ESP32-C3
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "bikepower_config.h"

static const char *TAG = "APP";

void app_main(void)
{
    ESP_LOGI(TAG, "BikePower C 启动 v%s (target=esp32c3, idf=v5.5.1)", FIRMWARE_VERSION);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS 初始化完成");

    ESP_LOGI(TAG, "app_main 启动日志：阶段0 骨架运行正常");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "心跳：可用堆 %lu 字节", (unsigned long)esp_get_free_heap_size());
    }
}
