/**
 * 射频控制器实现：WiFi/BLE 共存配网（方案 C：不停 BLE 不重启）
 *
 * RF 边界：本模块独占 WiFi SoftAP 启动/停止的职责。wifi_portal 只负责 HTTP 门户。
 * 共存前提：sdkconfig 已启用 CONFIG_ESP_COEX_SW_COEXIST_ENABLE，ESP32-C3 coex 模块按时间片仲裁。
 * 失败兜底：仅在 WiFi 资源彻底无法初始化时降级为旧独占方案（停 BLE + esp_restart）。
 */
#include "radio_ctrl.h"
#include "wifi_portal.h"
#include "captive_portal.h"
#include "ble_bikepower.h"
#include "bikepower_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "lwip/ip4_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "RADIO";

#define DHCPS_OFFER_DNS 0x02

// 共存模式下的 netif/handle 句柄，退出配网时按序释放
static esp_netif_t *s_ap_netif = NULL;
static bool s_wifi_inited = false;     // esp_wifi_init 已调用且未 deinit
static bool s_event_loop_created = false;  // 自建默认事件循环（与 BLE 共用时不重复创建）
static bool s_netif_inited = false;    // 本模块调 esp_netif_init 成功（INVALID_STATE 表示别人已 init，不能 deinit）

// STA 模式（v3.1.6 在线 OTA）：APSTA 共存时用
static esp_netif_t *s_sta_netif = NULL;
static EventGroupHandle_t s_sta_event_group = NULL;
static bool s_sta_handler_registered = false;
#define STA_GOT_IP_BIT       (1 << 0)
#define STA_DISCONNECTED_BIT (1 << 1)

// 前向声明：stop_wifi_ap 在 start_wifi_ap 失败时被复用做统一回滚
void radio_ctrl_stop_wifi_ap(void);
static void sta_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);

static void configure_captive_portal_dhcp(void)
{
    if (s_ap_netif == NULL) return;

    esp_netif_dns_info_t dns = { 0 };
    IP4_ADDR(&dns.ip.u_addr.ip4, 192, 168, 4, 1);
    dns.ip.type = ESP_IPADDR_TYPE_V4;

    uint8_t offer_dns = DHCPS_OFFER_DNS;
    esp_err_t ret = esp_netif_dhcps_option(s_ap_netif,
                                           ESP_NETIF_OP_SET,
                                           ESP_NETIF_DOMAIN_NAME_SERVER,
                                           &offer_dns,
                                           sizeof(offer_dns));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DHCP DNS offer 配置失败 ret=%d", ret);
    }

    ret = esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DHCP DNS 地址配置失败 ret=%d", ret);
    }

    const char *portal_uri = "http://" CAPTIVE_PORTAL_HOST "/";
    ret = esp_netif_dhcps_option(s_ap_netif,
                                 ESP_NETIF_OP_SET,
                                 ESP_NETIF_CAPTIVEPORTAL_URI,
                                 (void *)portal_uri,
                                 strlen(portal_uri));
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "DHCP Captive Portal URI 配置失败 ret=%d", ret);
    } else {
        ESP_LOGI(TAG, "DHCP 已下发 Captive Portal DNS/URI: %s", portal_uri);
    }
}

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

    configure_captive_portal_dhcp();

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

    ESP_LOGI(TAG, "SoftAP 启动 ssid=%s ip=192.168.4.1（BLE 共存）堆=%lu", WIFI_SSID, (unsigned long)esp_get_free_heap_size());
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
    if (s_sta_handler_registered) {
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, sta_event_handler);
        esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, sta_event_handler);
        s_sta_handler_registered = false;
    }
    if (s_wifi_inited) {
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_inited = false;
    }
    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
    if (s_sta_event_group) {
        xEventGroupClearBits(s_sta_event_group, STA_GOT_IP_BIT | STA_DISCONNECTED_BIT);
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

    // AP 就绪后启动 DNS 劫持，使手机连上 SoftAP 后自动弹出配网页（Captive Portal）
    esp_err_t dns_ret = captive_portal_start_dns();
    if (dns_ret != ESP_OK) {
        ESP_LOGW(TAG, "DNS 劫持启动失败，降级为手动输入 IP 配网");
    }

    ret = wifi_portal_start(engine);
    esp_task_wdt_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 配网门户启动失败 ret=%d，关闭 WiFi 回到 BLE", ret);
        captive_portal_stop_dns();
        radio_ctrl_stop_wifi_ap();
        // wifi_portal_start 失败时已通过 portal_done 投递 APP_EVT_WIFI_DONE，
        // 状态机会调 radio_ctrl_exit_wifi 做幂等清理并回到 ST_BLE_ACTIVE。
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
    ESP_LOGI(TAG, "退出 WiFi 配网：关闭 HTTP + DNS + SoftAP，射频归还 BLE");
    wifi_portal_stop();
    captive_portal_stop_dns();
    radio_ctrl_stop_wifi_ap();
}

// ============================ STA 模式（v3.1.6 在线 OTA） ============================

/**
 * STA 事件处理：捕获获 IP 和断连事件，唤醒等待的 sta_connect。
 * 只处理 STA 相关事件，AP 事件由原有逻辑（如有）处理。
 * arg：未使用。base/id：事件类型。data：事件数据（IP_EVENT_STA_GOT_IP 含 IP 信息）。
 * 返回值：无。
 * 特殊行为：仅设置事件组位，不做事其他副作用；handler 只注册一次。
 */
static void sta_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (s_sta_event_group == NULL) return;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_sta_event_group, STA_GOT_IP_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_sta_event_group, STA_DISCONNECTED_BIT);
    }
}

/**
 * 启动 WiFi STA 独占模式（无 AP，纯 STA 连路由器）。
 * 复用 start_wifi_ap 的 netif/event_loop/wifi_init 初始化逻辑，但 mode=STA。
 * 调用前需确保 WiFi 未启动（s_wifi_inited=false）。
 * ssid/psk：路由器凭证。
 * 返回值：ESP_OK 表示 STA 已启动（尚未连接），失败返回错误码。
 * 特殊行为：成功后 s_wifi_inited=true，STA netif 和 event handler 已就绪。
 */
static esp_err_t start_wifi_sta_only(const char *ssid, const char *psk)
{
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init 失败 ret=%d", ret);
        return ret;
    }
    if (ret == ESP_OK) s_netif_inited = true;

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default 失败 ret=%d", ret);
        if (s_netif_inited) { esp_netif_deinit(); s_netif_inited = false; }
        return ret;
    }
    if (ret == ESP_OK) s_event_loop_created = true;

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (!s_sta_netif) {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta 返回 NULL");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init 失败 ret=%d", ret);
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
        return ret;
    }
    s_wifi_inited = true;

    // 注册 STA 事件（事件组 + handler）
    if (s_sta_event_group == NULL) {
        s_sta_event_group = xEventGroupCreate();
    }
    if (!s_sta_handler_registered) {
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, sta_event_handler, NULL);
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, sta_event_handler, NULL);
        s_sta_handler_registered = true;
    }

    wifi_config_t sta_cfg = { 0 };
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, psk, sizeof(sta_cfg.sta.password) - 1);

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(STA) 失败 ret=%d", ret);
        return ret;
    }
    ret = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config(STA) 失败 ret=%d", ret);
        return ret;
    }
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start 失败 ret=%d", ret);
        return ret;
    }
    return ESP_OK;
}

esp_err_t radio_ctrl_sta_connect(const char *ssid, const char *psk, int timeout_ms)
{
    if (ssid == NULL || psk == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    // 必须已处于 AP 模式（配网态）
    if (!s_wifi_inited || s_ap_netif == NULL) {
        ESP_LOGE(TAG, "sta_connect 前置条件不满足：AP 未启动");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_sta_event_group == NULL) {
        s_sta_event_group = xEventGroupCreate();
    }
    xEventGroupClearBits(s_sta_event_group, STA_GOT_IP_BIT | STA_DISCONNECTED_BIT);

    // 首次创建 STA netif 并注册事件
    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            ESP_LOGE(TAG, "创建 STA netif 失败");
            return ESP_FAIL;
        }
    }
    if (!s_sta_handler_registered) {
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, sta_event_handler, NULL);
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, sta_event_handler, NULL);
        s_sta_handler_registered = true;
    }

    // 切 APSTA 模式（AP 保持 + 新增 STA）
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "切 APSTA 失败 ret=%d", ret);
        return ret;
    }

    wifi_config_t sta_cfg = { 0 };
    strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, psk, sizeof(sta_cfg.sta.password) - 1);
    ret = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set STA config 失败 ret=%d", ret);
        esp_wifi_set_mode(WIFI_MODE_AP);
        return ret;
    }

    ESP_LOGI(TAG, "STA 连接 %s 超时=%dms 堆=%lu", ssid, timeout_ms, (unsigned long)esp_get_free_heap_size());
    xEventGroupClearBits(s_sta_event_group, STA_GOT_IP_BIT | STA_DISCONNECTED_BIT);
    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect 失败 ret=%d", ret);
        esp_wifi_set_mode(WIFI_MODE_AP);
        return ret;
    }

    EventBits_t bits = xEventGroupWaitBits(s_sta_event_group,
                                           STA_GOT_IP_BIT | STA_DISCONNECTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & STA_GOT_IP_BIT) {
        ESP_LOGI(TAG, "STA 已获 IP 可访问互联网 堆=%lu", (unsigned long)esp_get_free_heap_size());
        return ESP_OK;
    }
    ESP_LOGE(TAG, "STA 连接超时或失败 bits=0x%x", (unsigned)bits);
    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_AP);
    return ESP_ERR_TIMEOUT;
}

void radio_ctrl_sta_disconnect(void)
{
    esp_wifi_disconnect();
    esp_wifi_set_mode(WIFI_MODE_AP);
    if (s_sta_event_group) {
        xEventGroupClearBits(s_sta_event_group, STA_GOT_IP_BIT | STA_DISCONNECTED_BIT);
    }
    ESP_LOGI(TAG, "STA 已断开，切回 AP-only");
}

esp_err_t radio_ctrl_enter_sta_exclusive(const char *ssid, const char *psk)
{
    if (ssid == NULL || psk == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "进入 STA 独占模式（停 AP + 停 BLE） ssid=%s", ssid);

    // 1. 停 HTTP 门户 + DNS + SoftAP（复用 exit_wifi 的清理逻辑，但不投递 done 事件）
    wifi_portal_stop();
    captive_portal_stop_dns();
    radio_ctrl_stop_wifi_ap();
    esp_task_wdt_reset();

    // 2. 停 BLE 释放射频
    if (!ble_bikepower_stop()) {
        ESP_LOGE(TAG, "BLE 停用失败，仍尝试继续 STA");
    }
    vTaskDelay(pdMS_TO_TICKS(RADIO_BLE_RELEASE_WAIT_MS));
    esp_task_wdt_reset();

    // 3. 启动 STA 独占模式
    esp_err_t ret = start_wifi_sta_only(ssid, psk);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "STA 独占启动失败 ret=%d", ret);
        return ret;
    }
    esp_task_wdt_reset();

    // 4. 连接路由器并等待获 IP
    if (s_sta_event_group == NULL) {
        s_sta_event_group = xEventGroupCreate();
    }
    xEventGroupClearBits(s_sta_event_group, STA_GOT_IP_BIT | STA_DISCONNECTED_BIT);

    esp_err_t cr = esp_wifi_connect();
    if (cr != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect 失败 ret=%d", cr);
        return cr;
    }

    EventBits_t bits = xEventGroupWaitBits(s_sta_event_group,
                                           STA_GOT_IP_BIT | STA_DISCONNECTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(OTA_STA_CONNECT_MS));
    if (bits & STA_GOT_IP_BIT) {
        ESP_LOGI(TAG, "STA 独占模式就绪，可下载固件 堆=%lu", (unsigned long)esp_get_free_heap_size());
        return ESP_OK;
    }
    ESP_LOGE(TAG, "STA 独占连接超时 bits=0x%x", (unsigned)bits);
    return ESP_ERR_TIMEOUT;
}

esp_err_t radio_ctrl_wifi_scan(wifi_ap_record_t *aps, int max_num, int *out_count)
{
    if (aps == NULL || max_num <= 0 || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (!s_wifi_inited || s_ap_netif == NULL) {
        ESP_LOGE(TAG, "scan 前置条件不满足：AP 未启动");
        return ESP_ERR_INVALID_STATE;
    }

    // 首次扫描时创建 STA netif（APSTA 模式需要 STA 接口才能扫描）
    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            ESP_LOGE(TAG, "scan 创建 STA netif 失败");
            return ESP_FAIL;
        }
    }

    // 切 APSTA 模式（AP 保持 + 新增 STA 接口用于扫描）
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "scan 切 APSTA 失败 ret=%d", ret);
        return ret;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    ret = esp_wifi_scan_start(&scan_cfg, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "scan_start 失败 ret=%d", ret);
        esp_wifi_set_mode(WIFI_MODE_AP);
        return ret;
    }

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num > max_num) {
        ap_num = max_num;
    }
    ret = esp_wifi_scan_get_ap_records(&ap_num, aps);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "get_ap_records 失败 ret=%d", ret);
        esp_wifi_set_mode(WIFI_MODE_AP);
        return ret;
    }

    // 清空扫描缓存，切回 AP-only
    esp_wifi_clear_ap_list();
    esp_wifi_set_mode(WIFI_MODE_AP);

    *out_count = ap_num;
    ESP_LOGI(TAG, "WiFi 扫描完成，发现 %d 个热点 堆=%lu", ap_num, (unsigned long)esp_get_free_heap_size());
    return ESP_OK;
}
