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
#include "ble_bikepower.h"
#include "power_engine.h"

static const char *TAG = "APP";

static power_engine_t s_engine;

// FTMS 控制点 ERG 回调：写入领域层目标功率（在 NimBLE host 任务上下文调用）
static void on_erg_target(int target_power)
{
    power_engine_set_erg_target(&s_engine, target_power);
}

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

    power_engine_init(&s_engine);
    ESP_LOGI(TAG, "功率引擎初始化 p=%dW c=%dRPM hr=%d mode=%d",
             s_engine.base_power, s_engine.cadence_base,
             s_engine.last_heartrate, s_engine.mode);

    ble_bikepower_set_erg_callback(on_erg_target);
    ble_bikepower_start();

    // 主循环：50ms 节拍生成快照并写入 BLE，BLE 1s 定时器负责实际通知
    int tick = 0;
    while (1) {
        snapshot_t s = power_engine_snapshot(&s_engine);
        ble_bikepower_set_data((uint16_t)s.power, (uint8_t)s.heartrate, (uint16_t)s.cadence);

        if (++tick >= 200) {  // 每 10s 一次心跳日志
            tick = 0;
            ESP_LOGI(TAG, "心跳 堆=%lu p=%dW hr=%d c=%d mode=%d conn=%d",
                     (unsigned long)esp_get_free_heap_size(),
                     s.power, s.heartrate, s.cadence, s.mode,
                     ble_bikepower_conn_count());
        }
        vTaskDelay(pdMS_TO_TICKS(HR_LOOP_INTERVAL_MS));
    }
}
