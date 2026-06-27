/**
 * 射频控制器实现：WiFi/BLE 共存配网（方案 C：不停 BLE 不重启）
 *
 * RF 边界：本模块独占 WiFi SoftAP 启动/停止的职责。wifi_portal 只负责 HTTP 门户。
 * 共存前提：sdkconfig 已启用 CONFIG_ESP_COEX_SW_COEXIST_ENABLE，ESP32-C3 coex 模块按时间片仲裁。
 * 失败兜底：仅在 WiFi 资源彻底无法初始化时降级为旧独占方案（停 BLE + esp_restart）。
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

// 共存模式下的 netif/handle 句柄，退出配网时按序释放
static esp_netif_t *s_ap_netif = NULL;
static bool s_wifi_inited = false;     // esp_wifi_init 已调用且未 deinit
static bool s_event_loop_created = false;  // 自建默认事件循环（与 BLE 共用时不重复创建）
static bool s_netif_inited = false;    // 本模块调 esp_netif_init 成功（INVALID_STATE 表示别人已 init，不能 deinit）

// 前向声明：stop_wifi_ap 在 start_wifi_ap 失败时被复用做统一回滚
void radio_ctrl_stop_wifi_ap(void);

/**
 * 启动 WiFi SoftAP（共存版：不停 BLE）。
 * 初始化网络栈、事件循环、WiFi 驱动并按配置启动 AP 模式。
 * 失败时逐步回滚已分配的资源，并清理本模块记录的句柄标志。
 * 无参数。
 * 返回值：ESP_OK 表示 AP 已启动，否则为错误码。
 * 特殊行为：BLE 是否在运行由调用方决定；本函数只负责 AP，不触碰 NimBLE。
 *           成功后会记录 netif/init/event_loop 句柄标志，供 radio_ctrl_exit_wifi_coex 按序释放。
 */
esp_err_t radio_ctrl_start_wifi_ap(void)
{
    esp_err_t ret;

    // esp_netif_init 在 BLE 启动前可能未调用过，重复调用返回 ESP_ERR_INVALID_STATE 是正常情况
    // 只有本模块真正 init 成功时才记录，stop 时才能 deinit，避免释放别人 init 的 netif
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init 失败 ret=%d", ret);
        return ret;
    }
    if (ret == ESP_OK) s_netif_inited = true;

    // 默认事件循环若已存在（NimBLE 通常已创建），重复创建返回 ESP_ERR_INVALID_STATE
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default 失败 ret=%d", ret);
        if (s_netif_inited) { esp_netif_deinit(); s_netif_inited = false; }
        return ret;
    }
    if (ret == ESP_OK) s_event_loop_created = true;

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_ap 返回 NULL");
        if (s_event_loop_created) {
            esp_event_loop_delete_default();
            s_event_loop_created = false;
        }
        if (s_netif_inited) { esp_netif_deinit(); s_netif_inited = false; }
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init 失败 ret=%d", ret);
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
        if (s_event_loop_created) {
            esp_event_loop_delete_default();
            s_event_loop_created = false;
        }
        if (s_netif_inited) { esp_netif_deinit(); s_netif_inited = false; }
        return ret;
    }
    s_wifi_inited = true;

    wifi_config_t ap_cfg = { 0 };
    strncpy((char *)ap_cfg.ap.ssid, WIFI_SSID, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(WIFI_SSID);
    strncpy((char *)ap_cfg.ap.password, WIFI_PASSWORD, sizeof(ap_cfg.ap.password));
    ap_cfg.ap.channel = WIFI_AP_CHANNEL;
    ap_cfg.ap.max_connection = WIFI_MAX_STA_CONN;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode 失败 ret=%d", ret);
        goto fail;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config 失败 ret=%d", ret);
        goto fail;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start 失败 ret=%d", ret);
        goto fail;
    }

    ESP_LOGI(TAG, "SoftAP 启动 ssid=%s ip=192.168.4.1（BLE 共存）", WIFI_SSID);
    return ESP_OK;

fail:
    radio_ctrl_stop_wifi_ap();
    return ret;
}

/**
 * 关闭 WiFi SoftAP 并释放 netif/事件循环资源。
 * 按 WiFi → netif → event_loop → netif_deinit 顺序释放，与启动顺序严格相反。
 * 无参数。
 * 返回值：无。
 * 特殊行为：仅释放本模块自建的资源；不重启设备，不触碰 BLE；可重复调用（幂等）。
 */
void radio_ctrl_stop_wifi_ap(void)
{
    if (s_wifi_inited) {
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_inited = false;
    }
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
    if (s_event_loop_created) {
        esp_event_loop_delete_default();
        s_event_loop_created = false;
    }
    if (s_netif_inited) {
        esp_netif_deinit();
        s_netif_inited = false;
    }
    ESP_LOGI(TAG, "SoftAP 已关闭 射频归还 BLE");
}

/**
 * 进入 WiFi 配网模式（共存版：不停 BLE 不重启）。
 * BLE 保持在线继续通知，直接启动 SoftAP + HTTP 配网门户。
 * 失败兜底：仅当 WiFi 资源彻底无法初始化时降级为旧独占方案（停 BLE + esp_restart）。
 * engine：供配网门户读写配置的功率引擎实例。
 * 返回值：无。
 * 特殊行为：本函数在已订阅 TWDT 的 app_main 任务上同步执行，WiFi/netif 初始化为长阻塞步骤，
 *           逐段喂狗避免累计耗时超过 WDT_TIMEOUT_MS 触发误复位。
 */
void radio_ctrl_enter_wifi(power_engine_t *engine)
{
    ESP_LOGI(TAG, "进入 WiFi 配网（共存模式）：BLE 保持在线");

    esp_err_t ret = radio_ctrl_start_wifi_ap();
    esp_task_wdt_reset();
    if (ret != ESP_OK) {
        // 共存启动失败：保留旧独占方案作为最后兜底，确保设备不会卡死在"无配网无 BLE"状态。
        ESP_LOGE(TAG, "共存 SoftAP 启动失败 ret=%d，降级为独占方案（停 BLE + 重启）", ret);
        if (!ble_bikepower_stop()) {
            ESP_LOGE(TAG, "BLE 停用失败 射频未释放，强制重启");
            vTaskDelay(pdMS_TO_TICKS(RADIO_RESTART_DELAY_MS));
            esp_restart();
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(RADIO_BLE_RELEASE_WAIT_MS));
        esp_task_wdt_reset();
        ret = radio_ctrl_start_wifi_ap();
        esp_task_wdt_reset();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "独占 SoftAP 仍失败 ret=%d，重启恢复 BLE", ret);
            vTaskDelay(pdMS_TO_TICKS(RADIO_RESTART_DELAY_MS));
            esp_restart();
            return;
        }
    }

    ret = wifi_portal_start(engine);
    esp_task_wdt_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 配网门户启动失败 ret=%d，关闭 WiFi 回到 BLE", ret);
        radio_ctrl_stop_wifi_ap();
        // 不重启，由状态机收到 APP_EVT_WIFI_DONE 后回到 ST_BLE_ACTIVE
    }
}

/**
 * 退出 WiFi 配网模式（共存版：关 WiFi 不重启）。
 * 关闭 HTTP 门户（wifi_portal_stop）+ SoftAP + netif，射频归还 BLE。
 * 无参数。
 * 返回值：无。
 * 特殊行为：仅清理 WiFi 侧资源；BLE 全程未停，无需重激活；不重启设备；可重复调用（幂等）。
 */
void radio_ctrl_exit_wifi(void)
{
    ESP_LOGI(TAG, "退出 WiFi 配网：关闭 HTTP + SoftAP，射频归还 BLE");
    wifi_portal_stop();
    radio_ctrl_stop_wifi_ap();
}
