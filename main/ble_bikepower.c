/**
 * BLE 蓝牙功率计模块（NimBLE）
 * 五服务：Cycling Power(0x1818) + Heart Rate(0x180D) + CSC(0x1816)
 *         + FTMS(0x1826) + Device Information(0x180A)
 *
 * 数据格式与 MicroPython ble_service.py 位级兼容：
 *   CPS  Measurement: flags(2)=0x0020 + power(2) + crankRev(2) + crankTime1024(2)
 *   HR   Measurement: flags(1)=0x00 + bpm(1)
 *   CSC  Measurement: flags(1)=0x02 + crankRev(2) + crankTime1024(2)
 *   FTMS IndoorBike : flags(2)=0x0044 + speed(2)=cad*30 + cadence(2)=cad*2 + power(2)
 *   FTMS ControlPoint: ERG OpCode 状态机
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

// ---- FTMS 控制点 OpCode ----
#define FTMS_OP_REQUEST_CONTROL   0x00
#define FTMS_OP_RESET             0x01
#define FTMS_OP_SET_TARGET_POWER  0x05
#define FTMS_OP_START_RESUME      0x07
#define FTMS_OP_STOP_PAUSE        0x08
#define FTMS_OP_RESPONSE_CODE     0x80
#define FTMS_RESULT_SUCCESS       0x01
#define FTMS_RESULT_NOT_SUPPORTED 0x02

static uint8_t  g_own_addr_type;
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;

static uint16_t g_cps_val_handle;
static uint16_t g_hr_val_handle;
static uint16_t g_csc_val_handle;
static uint16_t g_ftms_bike_handle;
static uint16_t g_ftms_cp_handle;
static uint16_t g_ftms_status_handle;

// 当前通知数据（由领域层写入，notify_tick 读取）
static volatile uint16_t g_power_w   = DEFAULT_POWER;
static volatile uint8_t  g_hr_bpm    = DEFAULT_HEARTRATE;
static volatile uint16_t g_cadence   = DEFAULT_CADENCE;

// 曲柄累计（CPS/CSC 共用）
static uint16_t g_crank_rev = 0;
static uint32_t g_last_crank_ms = 0;

// 预分配通知缓冲区（运行时零 malloc）
static uint8_t g_cps_buf[8];
static uint8_t g_hr_buf[2];
static uint8_t g_csc_buf[5];
static uint8_t g_ftms_bike_buf[8];
static uint8_t g_ftms_resp_buf[3];

// FTMS 静态特征值
static uint8_t g_ftms_feature_buf[8] = { 0x02, 0x40, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00 };
static uint8_t g_ftms_status_buf[7]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static ble_erg_cb_t g_erg_cb = NULL;
static char g_device_name[24] = DEVICE_NAME;

static void start_advertising(void);

// ============================ GATT 访问回调 ============================

static int chr_read_buf(struct ble_gatt_access_ctxt *ctxt, const uint8_t *buf, size_t len)
{
    int rc = os_mbuf_append(ctxt->om, buf, len);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int cps_access_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_cps_buf, sizeof(g_cps_buf));
    return BLE_ATT_ERR_UNLIKELY;
}

static int hr_access_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_hr_buf, sizeof(g_hr_buf));
    return BLE_ATT_ERR_UNLIKELY;
}

static int csc_access_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_csc_buf, sizeof(g_csc_buf));
    return BLE_ATT_ERR_UNLIKELY;
}

static int ftms_feat_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_ftms_feature_buf, sizeof(g_ftms_feature_buf));
    return BLE_ATT_ERR_UNLIKELY;
}

static int ftms_bike_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_ftms_bike_buf, sizeof(g_ftms_bike_buf));
    return BLE_ATT_ERR_UNLIKELY;
}

static int ftms_status_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_ftms_status_buf, sizeof(g_ftms_status_buf));
    return BLE_ATT_ERR_UNLIKELY;
}

static void ftms_send_response(uint8_t request_op, uint8_t result)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    g_ftms_resp_buf[0] = FTMS_OP_RESPONSE_CODE;
    g_ftms_resp_buf[1] = request_op;
    g_ftms_resp_buf[2] = result;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(g_ftms_resp_buf, sizeof(g_ftms_resp_buf));
    if (om) ble_gatts_notify_custom(g_conn_handle, g_ftms_cp_handle, om);
}

static int ftms_cp_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    uint8_t buf[20];
    uint16_t len = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len);
    if (rc != 0 || len < 1) return 0;

    uint8_t op = buf[0];
    ESP_LOGI(TAG, "FTMS控制点写入 op=0x%02x len=%d", op, len);
    switch (op) {
    case FTMS_OP_REQUEST_CONTROL:
        ftms_send_response(FTMS_OP_REQUEST_CONTROL, FTMS_RESULT_SUCCESS);
        break;
    case FTMS_OP_SET_TARGET_POWER:
        if (len >= 3) {
            int target = buf[1] | (buf[2] << 8);
            ESP_LOGI(TAG, "ERG目标功率 %dW", target);
            if (g_erg_cb) g_erg_cb(target);
            ftms_send_response(FTMS_OP_SET_TARGET_POWER, FTMS_RESULT_SUCCESS);
        } else {
            ftms_send_response(op, FTMS_RESULT_NOT_SUPPORTED);
        }
        break;
    case FTMS_OP_START_RESUME:
        g_ftms_status_buf[0] = 0x01;
        ftms_send_response(FTMS_OP_START_RESUME, FTMS_RESULT_SUCCESS);
        break;
    case FTMS_OP_STOP_PAUSE:
        g_ftms_status_buf[0] = 0x02;
        ftms_send_response(FTMS_OP_STOP_PAUSE, FTMS_RESULT_SUCCESS);
        break;
    case FTMS_OP_RESET:
        g_ftms_status_buf[0] = 0x00;
        ftms_send_response(FTMS_OP_RESET, FTMS_RESULT_SUCCESS);
        break;
    default:
        ftms_send_response(op, FTMS_RESULT_NOT_SUPPORTED);
        break;
    }
    return 0;
}

// ============================ DIS 静态读取 ============================

static int dis_manuf_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
        return chr_read_buf(ctxt, (const uint8_t *)DEVICE_MANUFACTURER, strlen(DEVICE_MANUFACTURER));
    return BLE_ATT_ERR_UNLIKELY;
}

static int dis_model_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
        return chr_read_buf(ctxt, (const uint8_t *)DEVICE_MODEL, strlen(DEVICE_MODEL));
    return BLE_ATT_ERR_UNLIKELY;
}

static int dis_fw_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
        return chr_read_buf(ctxt, (const uint8_t *)FIRMWARE_VERSION, strlen(FIRMWARE_VERSION));
    return BLE_ATT_ERR_UNLIKELY;
}

// ============================ GATT 服务表 ============================

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {   // Cycling Power
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(POWER_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = BLE_UUID16_DECLARE(POWER_CHAR_UUID),
              .access_cb = cps_access_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_cps_val_handle },
            { 0 }
        }
    },
    {   // Heart Rate
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(HR_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = BLE_UUID16_DECLARE(HR_CHAR_UUID),
              .access_cb = hr_access_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_hr_val_handle },
            { 0 }
        }
    },
    {   // Cycling Speed and Cadence
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(CSC_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = BLE_UUID16_DECLARE(CSC_CHAR_UUID),
              .access_cb = csc_access_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_csc_val_handle },
            { 0 }
        }
    },
    {   // Fitness Machine Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(FTMS_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = BLE_UUID16_DECLARE(FTMS_FEATURE_CHAR_UUID),
              .access_cb = ftms_feat_cb,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(FTMS_POWER_CHAR_UUID),
              .access_cb = ftms_bike_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_ftms_bike_handle },
            { .uuid = BLE_UUID16_DECLARE(FTMS_CONTROL_POINT_CHAR_UUID),
              .access_cb = ftms_cp_cb,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_ftms_cp_handle },
            { .uuid = BLE_UUID16_DECLARE(FTMS_STATUS_CHAR_UUID),
              .access_cb = ftms_status_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_ftms_status_handle },
            { 0 }
        }
    },
    {   // Device Information Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(DIS_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = BLE_UUID16_DECLARE(DIS_MANUFACTURER_CHAR_UUID),
              .access_cb = dis_manuf_cb, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(DIS_MODEL_CHAR_UUID),
              .access_cb = dis_model_cb, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(DIS_FIRMWARE_CHAR_UUID),
              .access_cb = dis_fw_cb, .flags = BLE_GATT_CHR_F_READ },
            { 0 }
        }
    },
    { 0 }
};

// ============================ 数据打包 ============================

static void pack_all(void)
{
    uint16_t power = g_power_w;
    uint16_t cadence = g_cadence;
    if (cadence < 1) cadence = 1;

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t adjusted_ms;
    if (g_last_crank_ms == 0) {
        adjusted_ms = now_ms;
    } else {
        uint32_t target = (2u * 60u * 1000u) / cadence;
        adjusted_ms = g_last_crank_ms + target;
    }
    g_crank_rev = (uint16_t)(g_crank_rev + 2);
    uint16_t rev = g_crank_rev;
    uint16_t time_1024 = (uint16_t)(((uint64_t)adjusted_ms * 1024u / 1000u) & 0xFFFF);
    g_last_crank_ms = adjusted_ms;

    // CPS: flags=0x0020 + power + rev + time
    g_cps_buf[0] = 0x20; g_cps_buf[1] = 0x00;
    g_cps_buf[2] = power & 0xFF;       g_cps_buf[3] = (power >> 8) & 0xFF;
    g_cps_buf[4] = rev & 0xFF;         g_cps_buf[5] = (rev >> 8) & 0xFF;
    g_cps_buf[6] = time_1024 & 0xFF;   g_cps_buf[7] = (time_1024 >> 8) & 0xFF;

    // HR: flags=0x00 + bpm
    g_hr_buf[0] = 0x00;
    g_hr_buf[1] = g_hr_bpm;

    // CSC: flags=0x02 + rev + time
    g_csc_buf[0] = 0x02;
    g_csc_buf[1] = rev & 0xFF;         g_csc_buf[2] = (rev >> 8) & 0xFF;
    g_csc_buf[3] = time_1024 & 0xFF;   g_csc_buf[4] = (time_1024 >> 8) & 0xFF;

    // FTMS IndoorBike: flags=0x0044 + speed(cad*30) + cadence(cad*2) + power
    uint16_t ftms_flags = 0x0044;
    uint16_t speed = cadence * 30;
    uint16_t ftms_cad = cadence * 2;
    g_ftms_bike_buf[0] = ftms_flags & 0xFF;  g_ftms_bike_buf[1] = (ftms_flags >> 8) & 0xFF;
    g_ftms_bike_buf[2] = speed & 0xFF;       g_ftms_bike_buf[3] = (speed >> 8) & 0xFF;
    g_ftms_bike_buf[4] = ftms_cad & 0xFF;    g_ftms_bike_buf[5] = (ftms_cad >> 8) & 0xFF;
    g_ftms_bike_buf[6] = power & 0xFF;       g_ftms_bike_buf[7] = (power >> 8) & 0xFF;
}

static void notify_one(uint16_t handle, const uint8_t *buf, size_t len)
{
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    if (om) ble_gatts_notify_custom(g_conn_handle, handle, om);
}

static void notify_tick(void *arg)
{
    (void)arg;
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    pack_all();
    notify_one(g_cps_val_handle, g_cps_buf, sizeof(g_cps_buf));
    notify_one(g_hr_val_handle, g_hr_buf, sizeof(g_hr_buf));
    notify_one(g_csc_val_handle, g_csc_buf, sizeof(g_csc_buf));
    notify_one(g_ftms_bike_handle, g_ftms_bike_buf, sizeof(g_ftms_bike_buf));
}

// ============================ GAP / 广播 ============================

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
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
    fields.name = (uint8_t *)g_device_name;
    fields.name_len = strlen(g_device_name);
    fields.name_is_complete = 1;
    static ble_uuid16_t adv_uuids[] = {
        BLE_UUID16_INIT(POWER_SERVICE_UUID),
        BLE_UUID16_INIT(HR_SERVICE_UUID),
    };
    fields.uuids16 = adv_uuids;
    fields.num_uuids16 = 2;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) { ESP_LOGE(TAG, "设置广播字段失败 rc=%d", rc); return; }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_ADVERTISING_INTERVAL_MS * 1000 / 625;
    adv_params.itvl_max = BLE_ADVERTISING_INTERVAL_MS * 1000 / 625;

    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0) { ESP_LOGE(TAG, "广播启动失败 rc=%d", rc); return; }
    ESP_LOGI(TAG, "开始广播 name=%s (间隔%dms)", g_device_name, BLE_ADVERTISING_INTERVAL_MS);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) { ESP_LOGE(TAG, "ensure_addr 失败 rc=%d", rc); return; }
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) { ESP_LOGE(TAG, "infer addr type 失败 rc=%d", rc); return; }
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE 协议栈复位 reason=%d", reason);
}

static void ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "NimBLE host task 启动");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ============================ 公共接口 ============================

void ble_bikepower_set_data(uint16_t power_w, uint8_t heartrate_bpm, uint16_t cadence_rpm)
{
    g_power_w = power_w;
    g_hr_bpm = heartrate_bpm;
    g_cadence = cadence_rpm;
}

void ble_bikepower_set_erg_callback(ble_erg_cb_t cb)
{
    g_erg_cb = cb;
}

void ble_bikepower_set_device_name(const char *name)
{
    if (!name || !name[0]) return;
    strncpy(g_device_name, name, sizeof(g_device_name) - 1);
    g_device_name[sizeof(g_device_name) - 1] = '\0';
}

int ble_bikepower_conn_count(void)
{
    return g_conn_handle == BLE_HS_CONN_HANDLE_NONE ? 0 : 1;
}

void ble_bikepower_start(void)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "nimble_port_init 失败 err=%d", err); return; }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "gatts_count_cfg 失败 rc=%d", rc); return; }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) { ESP_LOGE(TAG, "gatts_add_svcs 失败 rc=%d", rc); return; }

    rc = ble_svc_gap_device_name_set(g_device_name);
    if (rc != 0) { ESP_LOGW(TAG, "设置设备名失败 rc=%d", rc); }

    const esp_timer_create_args_t targs = { .callback = notify_tick, .name = "ble_notify" };
    esp_timer_handle_t th;
    ESP_ERROR_CHECK(esp_timer_create(&targs, &th));
    ESP_ERROR_CHECK(esp_timer_start_periodic(th, (uint64_t)BLE_NOTIFY_INTERVAL_MS * 1000));

    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "蓝牙功率计已启动 五服务 CPS+HRS+CSC+FTMS+DIS 设备名=%s", g_device_name);
}
