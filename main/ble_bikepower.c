/**
 * BLE 蓝牙功率计模块（NimBLE）
 * 阶段1：CPS 单服务 + 广播 + 1s 通知
 *
 * 数据格式与 MicroPython ble_service.py 位级兼容：
 *   CPS Measurement: flags(2)=0x0020 + power(2) + crankRev(2) + crankTime1024(2)
 */
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "bikepower_config.h"
#include "ble_bikepower.h"

static const char *TAG = "BLE";

static uint8_t g_own_addr_type;
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_cps_val_handle;

// 当前通知数据（由领域层写入，notify_tick 读取）
static volatile uint16_t g_power_w   = DEFAULT_POWER;
static volatile uint8_t  g_hr_bpm    = DEFAULT_HEARTRATE;
static volatile uint16_t g_cadence   = DEFAULT_CADENCE;

// 曲柄累计（CPS/CSC 共用）
static uint16_t g_crank_rev = 0;
static uint32_t g_last_crank_ms = 0;

static uint8_t g_cps_buf[8];

static void start_advertising(void);

// ---- GATT 访问回调：CPS Measurement 读取返回当前缓冲区 ----
static int cps_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int rc = os_mbuf_append(ctxt->om, g_cps_buf, sizeof(g_cps_buf));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(POWER_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = BLE_UUID16_DECLARE(POWER_CHAR_UUID),
                .access_cb = cps_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_cps_val_handle,
            },
            { 0 }
        }
    },
    { 0 }
};

// ---- CPS 数据打包（与 MicroPython update_data 位级一致） ----
static void cps_pack(void)
{
    uint16_t power = g_power_w;
    uint16_t cadence = g_cadence;
    if (cadence < 1) cadence = 1;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t adjusted_ms;
    if (g_last_crank_ms == 0) {
        adjusted_ms = now_ms;
    } else {
        // target_time_diff = rev_increment(2) * 60 * 1000 / cadence
        uint32_t target = (2u * 60u * 1000u) / cadence;
        adjusted_ms = g_last_crank_ms + target;
    }

    g_crank_rev = (uint16_t)(g_crank_rev + 2);
    uint16_t time_1024 = (uint16_t)(((uint64_t)adjusted_ms * 1024u / 1000u) & 0xFFFF);
    g_last_crank_ms = adjusted_ms;

    uint16_t flags = 0x0020;  // crank revolution data present
    g_cps_buf[0] = flags & 0xFF;
    g_cps_buf[1] = (flags >> 8) & 0xFF;
    g_cps_buf[2] = power & 0xFF;
    g_cps_buf[3] = (power >> 8) & 0xFF;
    g_cps_buf[4] = g_crank_rev & 0xFF;
    g_cps_buf[5] = (g_crank_rev >> 8) & 0xFF;
    g_cps_buf[6] = time_1024 & 0xFF;
    g_cps_buf[7] = (time_1024 >> 8) & 0xFF;
}

// ---- esp_timer 1s 通知回调 ----
static void notify_tick(void *arg)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    cps_pack();
    struct os_mbuf *om = ble_hs_mbuf_from_flat(g_cps_buf, sizeof(g_cps_buf));
    if (om) {
        ble_gatts_notify_custom(g_conn_handle, g_cps_val_handle, om);
    }
}

// ---- GAP 事件 ----
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "设备已连接 handle=%d", g_conn_handle);
        } else {
            ESP_LOGW(TAG, "连接失败 status=%d，重新广播", event->connect.status);
            start_advertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "设备已断开 reason=%d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;
    default:
        return 0;
    }
}

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    // 广播 CPS + HRS 16-bit UUID（与 MicroPython 广播包一致）
    static ble_uuid16_t adv_uuids[] = {
        BLE_UUID16_INIT(POWER_SERVICE_UUID),
        BLE_UUID16_INIT(HR_SERVICE_UUID),
    };
    fields.uuids16 = adv_uuids;
    fields.num_uuids16 = 2;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "设置广播字段失败 rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    // 广播间隔单位 0.625ms：500ms / 0.625 = 800 = 0x320
    adv_params.itvl_min = BLE_ADVERTISING_INTERVAL_MS * 1000 / 625;
    adv_params.itvl_max = BLE_ADVERTISING_INTERVAL_MS * 1000 / 625;

    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "广播启动失败 rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "开始广播 (间隔%dms)", BLE_ADVERTISING_INTERVAL_MS);
}

static void on_sync(void)
{
    int rc;
    // 随机静态地址（重启换 MAC，客户端识别为新设备）
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr 失败 rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer addr type 失败 rc=%d", rc);
        return;
    }
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE 协议栈复位 reason=%d", reason);
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task 启动");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_bikepower_set_data(uint16_t power_w, uint8_t heartrate_bpm, uint16_t cadence_rpm)
{
    g_power_w = power_w;
    g_hr_bpm = heartrate_bpm;
    g_cadence = cadence_rpm;
}

int ble_bikepower_conn_count(void)
{
    return g_conn_handle == BLE_HS_CONN_HANDLE_NONE ? 0 : 1;
}

void ble_bikepower_start(void)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init 失败 err=%d", err);
        return;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "gatts_count_cfg 失败 rc=%d", rc); return; }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "gatts_add_svcs 失败 rc=%d", rc); return; }

    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) { ESP_LOGW(TAG, "设置设备名失败 rc=%d", rc); }

    // 1s 通知定时器
    const esp_timer_create_args_t targs = {
        .callback = notify_tick,
        .name = "ble_notify",
    };
    esp_timer_handle_t th;
    ESP_ERROR_CHECK(esp_timer_create(&targs, &th));
    ESP_ERROR_CHECK(esp_timer_start_periodic(th, (uint64_t)BLE_NOTIFY_INTERVAL_MS * 1000));

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "蓝牙功率计已启动 设备名=%s", DEVICE_NAME);
}
