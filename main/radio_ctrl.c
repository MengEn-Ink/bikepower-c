/**
 * 射频控制器实现：BLE -> WiFi 软切换（方案 A：保存/超时后重启恢复 BLE）
 */
#include "radio_ctrl.h"
#include "wifi_portal.h"
#include "ble_bikepower.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RADIO";

void radio_ctrl_enter_wifi(power_engine_t *engine)
{
    ESP_LOGI(TAG, "进入 WiFi 配网：停用 BLE 释放射频");
    ble_bikepower_stop();

    // 给控制器留出释放射频的时间，避免 WiFi init 与 BLE deinit 竞争
    vTaskDelay(pdMS_TO_TICKS(200));

    wifi_portal_start(engine);
}
