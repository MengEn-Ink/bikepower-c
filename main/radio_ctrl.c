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

/**
 * 切换到 WiFi 配网模式。
 * 先按硬件约束停用 BLE，再等待射频资源释放，最后启动 WiFi 配网门户。
 * engine：供配网门户读写配置的功率引擎实例。
 * 返回值：无。
 * 特殊行为：该流程会中断当前 BLE 广播和连接，后续由配网门户保存或超时后重启恢复 BLE。
 */
void radio_ctrl_enter_wifi(power_engine_t *engine)
{
    ESP_LOGI(TAG, "进入 WiFi 配网：停用 BLE 释放射频");
    ble_bikepower_stop();

    // 给控制器留出释放射频的时间，避免 WiFi init 与 BLE deinit 竞争
    vTaskDelay(pdMS_TO_TICKS(200));

    wifi_portal_start(engine);
}
