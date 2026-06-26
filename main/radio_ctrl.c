/**
 * 射频控制器实现：BLE -> WiFi 软切换（方案 A：保存/超时后重启恢复 BLE）
 *
 * RF 边界：本模块独占 WiFi SoftAP 启动/停止的职责。wifi_portal 只负责 HTTP 门户。
 */
#include "radio_ctrl.h"
#include "wifi_portal.h"
#include "ble_bikepower.h"
#include "bikepower_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "RADIO";

/**
 * 启动 WiFi SoftAP。
 * 初始化网络栈、事件循环、WiFi 驱动并按配置启动 AP 模式。
 * 失败时逐步回滚已分配的资源。
 * 无参数。
 * 返回值：ESP_OK 表示 AP 已启动，否则为错误码。
 * 特殊行为：调用方需保证 BLE 已停用。
 */
esp_err_t radio_ctrl_start_wifi_ap(void)
{
    esp_err_t ret;
    esp_netif_t *ap_netif = NULL;

    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init 失败 ret=%d", ret);
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_event_loop_create_default 失败 ret=%d", ret);
        esp_netif_deinit();
        return ret;
    }

    ap_netif = esp_netif_create_default_wifi_ap();
    if (!ap_netif) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_ap 返回 NULL");
        esp_event_loop_delete_default();
        esp_netif_deinit();
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init 失败 ret=%d", ret);
        esp_netif_destroy(ap_netif);
        esp_event_loop_delete_default();
        esp_netif_deinit();
        return ret;
    }

    wifi_config_t ap_cfg = { 0 };
    strncpy((char *)ap_cfg.ap.ssid, WIFI_SSID, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(WIFI_SSID);
    strncpy((char *)ap_cfg.ap.password, WIFI_PASSWORD, sizeof(ap_cfg.ap.password));
    ap_cfg.ap.channel = WIFI_AP_CHANNEL;
    ap_cfg.ap.max_connection = 2;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode 失败 ret=%d", ret);
        esp_wifi_deinit();
        esp_netif_destroy(ap_netif);
        esp_event_loop_delete_default();
        esp_netif_deinit();
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config 失败 ret=%d", ret);
        esp_wifi_deinit();
        esp_netif_destroy(ap_netif);
        esp_event_loop_delete_default();
        esp_netif_deinit();
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start 失败 ret=%d", ret);
        esp_wifi_deinit();
        esp_netif_destroy(ap_netif);
        esp_event_loop_delete_default();
        esp_netif_deinit();
        return ret;
    }

    ESP_LOGI(TAG, "SoftAP 启动 ssid=%s ip=192.168.4.1", WIFI_SSID);
    return ESP_OK;
}

/**
 * 切换到 WiFi 配网模式。
 * 先按硬件约束停用 BLE，再启动 SoftAP，最后启动 HTTP 配网门户。
 * 若 AP 或 HTTP 启动失败则重启设备恢复 BLE（方案 A 的安全兜底）。
 * engine：供配网门户读写配置的功率引擎实例。
 * 返回值：无。
 * 特殊行为：任何阶段失败都会立即 esp_restart() 恢复 BLE，避免设备卡在"有 AP 没页面"状态。
 */
void radio_ctrl_enter_wifi(power_engine_t *engine)
{
    ESP_LOGI(TAG, "进入 WiFi 配网：停用 BLE 释放射频");
    if (!ble_bikepower_stop()) {
        // BLE 未能释放射频，继续启 WiFi 会违反单路 RF 互斥约束。
        // 直接重启由开机流程干净地恢复 BLE，避免 WiFi/BLE 同时占用射频。
        ESP_LOGE(TAG, "BLE 停用失败 射频未释放，重启恢复 BLE");
        vTaskDelay(pdMS_TO_TICKS(RADIO_RESTART_DELAY_MS));
        esp_restart();
        return;
    }

    // 本函数在已订阅 TWDT 的 app_main 任务上同步执行，nimble_port_stop 与
    // WiFi/netif 初始化均为长阻塞步骤，逐段喂狗避免累计耗时超过 WDT_TIMEOUT_MS 触发误复位。
    esp_task_wdt_reset();

    // 给控制器留出释放射频的时间，避免 WiFi init 与 BLE deinit 竞争
    vTaskDelay(pdMS_TO_TICKS(RADIO_BLE_RELEASE_WAIT_MS));

    esp_err_t ret = radio_ctrl_start_wifi_ap();
    esp_task_wdt_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SoftAP 启动失败 ret=%d，重启恢复 BLE", ret);
        vTaskDelay(pdMS_TO_TICKS(RADIO_RESTART_DELAY_MS));
        esp_restart();
        return;
    }

    ret = wifi_portal_start(engine);
    esp_task_wdt_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 配网门户启动失败 ret=%d，重启恢复 BLE", ret);
        vTaskDelay(pdMS_TO_TICKS(RADIO_RESTART_DELAY_MS));
        esp_restart();
        return;
    }
}
