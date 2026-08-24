/**
 * BLE 蓝牙功率计模块（NimBLE）
 * 骑行 profile：CPS + HRS + CSC + FTMS + DIS
 * 跑步 profile：RSC + HRS + FTMS + DIS
 *
 * 标准通知格式：
 *   CPS  Measurement: flags(2)=0x0020 + power(2) + crankRev(2) + crankTime1024(2)
 *   HR   Measurement: flags(1)=0x00 + bpm(1)
 *   CSC  Measurement: flags(1)=0x02 + crankRev(2) + crankTime1024(2)
 *   FTMS IndoorBike : flags(2)=0x0044 + speed(2)=cad*30 + cadence(2)=cad*2 + power(2)
 *   FTMS ControlPoint: ERG OpCode 状态机
 */
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "bikepower_config.h"
#include "ble_payload_codec.h"
#include "domain_config.h"
#include "running_engine.h"
#include "ble_bikepower.h"
#include "board_detect.h"

static const char *TAG = "BLE";

static uint8_t  g_own_addr_type;
// 跨任务访问：host 任务(gap_event_cb/on_reset)写，app_main(conn_count) 与 esp_timer(notify_tick) 读，
// 加 volatile 防止编译器缓存（C3 上 16 位单次访问原子）。
static volatile uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;

static uint16_t g_cps_val_handle;
static uint16_t g_hr_val_handle;
static uint16_t g_csc_val_handle;
static uint16_t g_rsc_val_handle;
static uint16_t g_ftms_bike_handle;
static uint16_t g_ftms_treadmill_handle;
static uint16_t g_ftms_cp_handle;
static uint16_t g_ftms_status_handle;

// 当前通知数据（由领域层写入，notify_tick 读取）
static volatile uint16_t g_power_w   = DEFAULT_POWER;
static volatile uint8_t  g_hr_bpm    = DEFAULT_HEARTRATE;
static volatile uint16_t g_cadence   = DEFAULT_CADENCE;
static volatile uint8_t  g_broadcast_mask = BROADCAST_FIELD_ALL;
static volatile uint16_t g_run_speed = 0;
static volatile uint16_t g_run_speed_x100 = 0;
static volatile uint8_t  g_run_cadence = RUN_CADENCE_DEFAULT;
static volatile uint16_t g_run_stride_cm = 0;
static volatile uint32_t g_run_distance_m = 0;
static volatile uint32_t g_run_distance_x10_m = 0;
static volatile uint32_t g_run_elapsed_s = 0;
static volatile bool g_run_running = true;

// 曲柄累计（CPS/CSC 共用）
static uint16_t g_crank_rev = 0;
static uint32_t g_last_crank_ms = 0;

// 预分配通知缓冲区（运行时零 malloc）
static uint8_t g_cps_buf[8];
static uint8_t g_hr_buf[2];
static uint8_t g_csc_buf[5];
static uint8_t g_rsc_buf[10];
static uint8_t g_cps_feature_buf[4] = { 0x08, 0x00, 0x00, 0x00 };
static uint8_t g_csc_feature_buf[2] = { 0x02, 0x00 };
static uint8_t g_rsc_feature_buf[2] = { 0x07, 0x00 };
static uint8_t g_cycling_sensor_location = 0x05;
static uint8_t g_running_sensor_location = 0x01;
static uint8_t g_ftms_bike_buf[8];
static size_t g_ftms_bike_len = 8;
static uint8_t g_ftms_treadmill_buf[10];
static size_t g_ftms_treadmill_len = 10;
static uint8_t g_ftms_resp_buf[3];

// FTMS 静态特征值：前 4 字节为 machine feature，后 4 字节为 target setting feature。
static const uint8_t g_ftms_cycling_feature_buf[8] = {
    0x02, 0x40, 0x00, 0x00, // cadence + power measurement
    0x08, 0x00, 0x00, 0x00, // target power setting
};
static const uint8_t g_ftms_running_feature_buf[8] = {
    0x04, 0x05, 0x00, 0x00, // distance + heart rate + elapsed time
    0x02, 0x00, 0x00, 0x00, // target speed setting
};
static uint8_t g_ftms_status_buf[7]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static ble_erg_cb_t g_erg_cb = NULL;
static ble_erg_clear_cb_t g_erg_clear_cb = NULL;
static ble_run_control_cb_t g_run_control_cb = NULL;
static sport_profile_t g_profile = DEFAULT_SPORT_PROFILE;
static char g_device_name[DEVICE_NAME_MAX] = DEVICE_NAME;
static esp_timer_handle_t g_notify_timer = NULL;
// 跨任务访问：app_main 写(start/stop)，esp_timer(notify_tick) 读，加 volatile 防缓存。
static volatile bool g_ready = false;
static volatile bool g_stopping = false;
static volatile bool g_start_failed = false;
// 通知互斥锁：保证 notify_tick 回调与 ble_bikepower_stop 的 deinit 互斥，
// stop 在释放 NimBLE 前必须先拿到锁，确保没有 notify 回调正在访问协议栈（消除 UAF 窗口）。
static SemaphoreHandle_t g_notify_lock = NULL;

static int start_advertising(void);
// 发送单个特征通知/indication（定义在通知模块），FTMS 控制点回调需提前用于推送响应。
static void notify_one(uint16_t handle, const uint8_t *buf, size_t len);
static void indicate_one(uint16_t handle, const uint8_t *buf, size_t len);

// GATT 访问回调

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

static int cps_feature_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return chr_read_buf(ctxt, g_cps_feature_buf, sizeof(g_cps_feature_buf));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int csc_feature_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return chr_read_buf(ctxt, g_csc_feature_buf, sizeof(g_csc_feature_buf));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int rsc_measurement_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_rsc_buf, sizeof(g_rsc_buf));
    return BLE_ATT_ERR_UNLIKELY;
}

static int rsc_feature_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_rsc_feature_buf, sizeof(g_rsc_feature_buf));
    return BLE_ATT_ERR_UNLIKELY;
}

static int sensor_location_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const uint8_t *location = arg ? (const uint8_t *)arg : &g_cycling_sensor_location;
        return chr_read_buf(ctxt, location, sizeof(*location));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int ftms_feat_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const uint8_t *buf = (g_profile == SPORT_PROFILE_RUNNING)
            ? g_ftms_running_feature_buf
            : g_ftms_cycling_feature_buf;
        return chr_read_buf(ctxt, buf, sizeof(g_ftms_cycling_feature_buf));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int ftms_bike_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_ftms_bike_buf, g_ftms_bike_len);
    return BLE_ATT_ERR_UNLIKELY;
}

static int ftms_treadmill_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_ftms_treadmill_buf, g_ftms_treadmill_len);
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
    g_ftms_resp_buf[0] = FTMS_OP_RESPONSE_CODE;
    g_ftms_resp_buf[1] = request_op;
    g_ftms_resp_buf[2] = result;
    indicate_one(g_ftms_cp_handle, g_ftms_resp_buf, sizeof(g_ftms_resp_buf));
}

static int ftms_cp_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    uint8_t buf[FTMS_CP_BUF_SIZE];
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
        if (g_profile == SPORT_PROFILE_RUNNING) {
            ftms_send_response(op, FTMS_RESULT_NOT_SUPPORTED);
            break;
        }
        if (len >= 3) {
            // Set Target Power 的负载为 signed 16-bit 目标功率（小端）
            int16_t target_s16 = (int16_t)(buf[1] | ((uint16_t)buf[2] << 8));
            int target = (int)target_s16;
            ESP_LOGI(TAG, "ERG目标功率 %dW", target);
            bool ok = false;
            if (g_erg_cb) ok = g_erg_cb(target);
            if (ok) {
                ftms_send_response(FTMS_OP_SET_TARGET_POWER, FTMS_RESULT_SUCCESS);
            } else {
                ftms_send_response(FTMS_OP_SET_TARGET_POWER, FTMS_RESULT_INVALID_PARAM);
            }
        } else {
            ftms_send_response(op, FTMS_RESULT_NOT_SUPPORTED);
        }
        break;
    case FTMS_OP_SET_TARGET_SPEED:
        if (g_profile != SPORT_PROFILE_RUNNING || len < 3) {
            ftms_send_response(op, FTMS_RESULT_NOT_SUPPORTED);
            break;
        }
        {
            uint16_t target_speed = (uint16_t)(buf[1] | ((uint16_t)buf[2] << 8));
            if (target_speed < RUN_SPEED_X100_MIN || target_speed > RUN_SPEED_X100_MAX) {
                ftms_send_response(op, FTMS_RESULT_INVALID_PARAM);
                break;
            }
            bool ok = false;
            if (g_run_control_cb) ok = g_run_control_cb(BLE_RUN_CTRL_SPEED, (int)target_speed);
            ftms_send_response(op, ok ? FTMS_RESULT_SUCCESS : FTMS_RESULT_OPERATION_FAILED);
        }
        break;
    case FTMS_OP_START_RESUME:
        if (g_profile == SPORT_PROFILE_RUNNING) {
            bool ok = g_run_control_cb && g_run_control_cb(BLE_RUN_CTRL_START, 0);
            if (!ok) {
                ftms_send_response(op, FTMS_RESULT_OPERATION_FAILED);
                break;
            }
        }
        g_ftms_status_buf[0] = 0x01;
        notify_one(g_ftms_status_handle, g_ftms_status_buf, sizeof(g_ftms_status_buf));
        ftms_send_response(FTMS_OP_START_RESUME, FTMS_RESULT_SUCCESS);
        break;
    case FTMS_OP_STOP_PAUSE:
        if (g_profile == SPORT_PROFILE_RUNNING) {
            bool ok = g_run_control_cb && g_run_control_cb(BLE_RUN_CTRL_STOP, 0);
            if (!ok) {
                ftms_send_response(op, FTMS_RESULT_OPERATION_FAILED);
                break;
            }
        }
        g_ftms_status_buf[0] = 0x02;
        notify_one(g_ftms_status_handle, g_ftms_status_buf, sizeof(g_ftms_status_buf));
        if (g_profile != SPORT_PROFILE_RUNNING && g_erg_clear_cb) {
            g_erg_clear_cb(FTMS_OP_STOP_PAUSE);
        }
        ftms_send_response(FTMS_OP_STOP_PAUSE, FTMS_RESULT_SUCCESS);
        break;
    case FTMS_OP_RESET:
        if (g_profile == SPORT_PROFILE_RUNNING) {
            bool ok = g_run_control_cb && g_run_control_cb(BLE_RUN_CTRL_RESET, 0);
            if (!ok) {
                ftms_send_response(op, FTMS_RESULT_OPERATION_FAILED);
                break;
            }
        }
        g_ftms_status_buf[0] = 0x00;
        notify_one(g_ftms_status_handle, g_ftms_status_buf, sizeof(g_ftms_status_buf));
        if (g_profile != SPORT_PROFILE_RUNNING && g_erg_clear_cb) {
            g_erg_clear_cb(FTMS_OP_RESET);
        }
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
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char fw_name[96];
        board_firmware_name(fw_name, sizeof(fw_name));
        return chr_read_buf(ctxt, (const uint8_t *)fw_name, strlen(fw_name));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// ============================ GATT 服务表 ============================

static const struct ble_gatt_svc_def cycling_gatt_svcs[] = {
    {   // Cycling Power
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(POWER_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = BLE_UUID16_DECLARE(POWER_CHAR_UUID),
              .access_cb = cps_access_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_cps_val_handle },
            { .uuid = BLE_UUID16_DECLARE(CYCLING_POWER_FEATURE_CHAR_UUID),
              .access_cb = cps_feature_cb,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(SENSOR_LOCATION_CHAR_UUID),
              .access_cb = sensor_location_cb,
              .arg = &g_cycling_sensor_location,
              .flags = BLE_GATT_CHR_F_READ },
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
            { .uuid = BLE_UUID16_DECLARE(CSC_FEATURE_CHAR_UUID),
              .access_cb = csc_feature_cb,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(SENSOR_LOCATION_CHAR_UUID),
              .access_cb = sensor_location_cb,
              .arg = &g_cycling_sensor_location,
              .flags = BLE_GATT_CHR_F_READ },
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
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_INDICATE,
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

static const struct ble_gatt_svc_def running_gatt_svcs[] = {
    {   // Running Speed and Cadence
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(RSC_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = BLE_UUID16_DECLARE(RSC_MEASUREMENT_CHAR_UUID),
              .access_cb = rsc_measurement_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_rsc_val_handle },
            { .uuid = BLE_UUID16_DECLARE(RSC_FEATURE_CHAR_UUID),
              .access_cb = rsc_feature_cb,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(SENSOR_LOCATION_CHAR_UUID),
              .access_cb = sensor_location_cb,
              .arg = &g_running_sensor_location,
              .flags = BLE_GATT_CHR_F_READ },
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
    {   // Fitness Machine Service (Treadmill)
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(FTMS_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = BLE_UUID16_DECLARE(FTMS_FEATURE_CHAR_UUID),
              .access_cb = ftms_feat_cb,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(FTMS_TREADMILL_CHAR_UUID),
              .access_cb = ftms_treadmill_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_ftms_treadmill_handle },
            { .uuid = BLE_UUID16_DECLARE(FTMS_CONTROL_POINT_CHAR_UUID),
              .access_cb = ftms_cp_cb,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_INDICATE,
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

static const struct ble_gatt_svc_def *active_gatt_svcs(void)
{
    return g_profile == SPORT_PROFILE_RUNNING ? running_gatt_svcs : cycling_gatt_svcs;
}

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

    ble_payload_encode_cps(g_cps_buf, sizeof(g_cps_buf),
                           (int16_t)power, rev, time_1024);
    ble_payload_encode_hrs(g_hr_buf, sizeof(g_hr_buf), g_hr_bpm);
    ble_payload_encode_csc(g_csc_buf, sizeof(g_csc_buf), rev, time_1024);

    bool cadence_enabled = (g_broadcast_mask & BROADCAST_FIELD_CADENCE) != 0;
    bool power_enabled = (g_broadcast_mask & BROADCAST_FIELD_POWER) != 0;
    g_ftms_bike_len = ble_payload_encode_indoor_bike(
        g_ftms_bike_buf, sizeof(g_ftms_bike_buf),
        cadence_enabled, power_enabled,
        (uint16_t)(cadence * 30u), cadence, (int16_t)power);
}

static void pack_running(void)
{
    uint16_t speed = g_run_speed;
    uint16_t stride = g_run_stride_cm;
    uint32_t distance = g_run_distance_m;
    uint32_t distance_x10_m = g_run_distance_x10_m;
    uint32_t elapsed = g_run_elapsed_s;

    ble_payload_encode_hrs(g_hr_buf, sizeof(g_hr_buf), g_hr_bpm);
    ble_payload_encode_rsc(g_rsc_buf, sizeof(g_rsc_buf), g_run_running,
                           speed, g_run_cadence, stride, distance_x10_m);
    uint16_t elapsed16 = elapsed > 0xFFFFu ? 0xFFFFu : (uint16_t)elapsed;
    g_ftms_treadmill_len = ble_payload_encode_treadmill(
        g_ftms_treadmill_buf, sizeof(g_ftms_treadmill_buf),
        g_run_speed_x100, distance, g_hr_bpm, elapsed16);
}

static void notify_one_nolock(uint16_t handle, const uint8_t *buf, size_t len)
{
    if (g_stopping || !g_ready || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    if (om) ble_gatts_notify_custom(g_conn_handle, handle, om);
}

static void notify_one(uint16_t handle, const uint8_t *buf, size_t len)
{
    // 短等待拿锁：锁持有时间极短（微秒级），不会阻塞NimBLE任务太久，同时保证FTMS关键响应不丢
    if (!g_notify_lock || xSemaphoreTake(g_notify_lock, pdMS_TO_TICKS(BLE_NOTIFY_LOCK_WAIT_MS)) != pdTRUE) return;
    notify_one_nolock(handle, buf, len);
    xSemaphoreGive(g_notify_lock);
}

static void indicate_one(uint16_t handle, const uint8_t *buf, size_t len)
{
    if (!g_notify_lock || xSemaphoreTake(g_notify_lock, pdMS_TO_TICKS(BLE_NOTIFY_LOCK_WAIT_MS)) != pdTRUE) return;
    if (!g_stopping && g_ready && g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
        if (om) ble_gatts_indicate_custom(g_conn_handle, handle, om);
    }
    xSemaphoreGive(g_notify_lock);
}

static void notify_tick(void *arg)
{
    (void)arg;
    // 持锁后再检查标志并访问协议栈：stop 会先拿同一把锁才 deinit NimBLE，
    // 保证本回调要么完整在 deinit 前执行、要么 stop 等待本回调结束，消除 UAF 窗口。
    if (!g_notify_lock || xSemaphoreTake(g_notify_lock, 0) != pdTRUE) return;
    if (g_stopping || !g_ready || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        xSemaphoreGive(g_notify_lock);
        return;
    }
    if (g_profile == SPORT_PROFILE_RUNNING) {
        pack_running();
        if ((g_broadcast_mask & (BROADCAST_FIELD_POWER | BROADCAST_FIELD_CADENCE)) == (BROADCAST_FIELD_POWER | BROADCAST_FIELD_CADENCE)) {
            notify_one_nolock(g_rsc_val_handle, g_rsc_buf, sizeof(g_rsc_buf));
        }
        if (g_broadcast_mask & BROADCAST_FIELD_HEARTRATE) notify_one_nolock(g_hr_val_handle, g_hr_buf, sizeof(g_hr_buf));
        if (g_broadcast_mask & (BROADCAST_FIELD_POWER | BROADCAST_FIELD_CADENCE | BROADCAST_FIELD_HEARTRATE)) {
            notify_one_nolock(g_ftms_treadmill_handle, g_ftms_treadmill_buf, g_ftms_treadmill_len);
        }
    } else {
        pack_all();
        if (g_broadcast_mask & BROADCAST_FIELD_POWER) notify_one_nolock(g_cps_val_handle, g_cps_buf, sizeof(g_cps_buf));
        if (g_broadcast_mask & BROADCAST_FIELD_HEARTRATE) notify_one_nolock(g_hr_val_handle, g_hr_buf, sizeof(g_hr_buf));
        if (g_broadcast_mask & BROADCAST_FIELD_CADENCE) notify_one_nolock(g_csc_val_handle, g_csc_buf, sizeof(g_csc_buf));
        if (g_broadcast_mask & (BROADCAST_FIELD_POWER | BROADCAST_FIELD_CADENCE)) notify_one_nolock(g_ftms_bike_handle, g_ftms_bike_buf, g_ftms_bike_len);
    }
    xSemaphoreGive(g_notify_lock);
}

// ============================ GAP / 广播 ============================

static void mark_start_failed(const char *reason, int rc)
{
    ESP_LOGE(TAG, "%s rc=%d", reason, rc);
    g_ready = false;
    g_start_failed = true;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    int rc;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "设备已连接 handle=%d", g_conn_handle);
        } else {
            ESP_LOGW(TAG, "连接失败 status=%d，重新广播", event->connect.status);
            rc = start_advertising();
            if (rc != 0 && !g_stopping) mark_start_failed("连接失败后重广播失败", rc);
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "设备已断开 reason=%d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        rc = start_advertising();
        if (rc != 0 && !g_stopping) mark_start_failed("断开后重广播失败", rc);
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        rc = start_advertising();
        if (rc != 0 && !g_stopping) mark_start_failed("广播完成后重广播失败", rc);
        return 0;
    default:
        return 0;
    }
}

static int start_advertising(void)
{
    if (g_stopping) {
        ESP_LOGI(TAG, "停止过程中，跳过广播重启");
        return 0;
    }
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    static ble_uuid16_t cycling_adv_uuids[] = {
        BLE_UUID16_INIT(POWER_SERVICE_UUID),
        BLE_UUID16_INIT(CSC_SERVICE_UUID),
        BLE_UUID16_INIT(FTMS_SERVICE_UUID),
    };
    static ble_uuid16_t running_adv_uuids[] = {
        BLE_UUID16_INIT(RSC_SERVICE_UUID),
        BLE_UUID16_INIT(FTMS_SERVICE_UUID),
    };
    static ble_uuid16_t scan_rsp_uuids[] = {
        BLE_UUID16_INIT(HR_SERVICE_UUID),
    };
    // 广播包空间有限：主广播放 App/码表按设备类型过滤时最关键的服务，完整能力由连接后 GATT 发现获取。
    fields.uuids16 = (g_profile == SPORT_PROFILE_RUNNING) ? running_adv_uuids : cycling_adv_uuids;
    fields.num_uuids16 = (g_profile == SPORT_PROFILE_RUNNING) ? 2 : 3;
    fields.uuids16_is_complete = 1;
    // 31 字节广播包预算：flags(3) + 完整 16 位 UUID 列表(2+3×2=8) 占 11 字节，
    // 留给名字 AD 20 字节即最多 BLE_ADV_NAME_MAX_LEN 个字符。超出部分（完整名）放 scan response，
    // 避免 ble_gap_adv_set_fields 因 EMSGSIZE 静默失败导致设备搜不到。
    size_t name_len = strlen(g_device_name);
    fields.name = (uint8_t *)g_device_name;
    if (name_len > BLE_ADV_NAME_MAX_LEN) {
        fields.name_len = BLE_ADV_NAME_MAX_LEN;
        fields.name_is_complete = 0;
    } else {
        fields.name_len = name_len;
        fields.name_is_complete = 1;
    }

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) { ESP_LOGE(TAG, "设置广播字段失败 rc=%d", rc); return rc; }

    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.uuids16 = scan_rsp_uuids;
    rsp_fields.num_uuids16 = 1;
    rsp_fields.uuids16_is_complete = 0;
    rsp_fields.name = (uint8_t *)g_device_name;
    rsp_fields.name_len = name_len;
    rsp_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) { ESP_LOGE(TAG, "设置 scan response 失败 rc=%d", rc); return rc; }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_ADVERTISING_INTERVAL_MS * 1000 / 625;
    adv_params.itvl_max = BLE_ADVERTISING_INTERVAL_MS * 1000 / 625;

    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0) { ESP_LOGE(TAG, "广播启动失败 rc=%d", rc); return rc; }
    ESP_LOGI(TAG, "开始广播 profile=%s name=%s (间隔%dms)",
             g_profile == SPORT_PROFILE_RUNNING ? "running" : "cycling",
             g_device_name, BLE_ADVERTISING_INTERVAL_MS);
    return 0;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        mark_start_failed("ensure_addr 失败", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        mark_start_failed("infer addr type 失败", rc);
        return;
    }
    rc = start_advertising();
    if (rc != 0) {
        mark_start_failed("启动广播失败", rc);
        return;
    }
    // 广播成功启动后才标记为就绪状态
    g_ready = true;
    g_start_failed = false;
    ESP_LOGI(TAG, "BLE 就绪，profile=%s 设备名=%s",
             g_profile == SPORT_PROFILE_RUNNING ? "RSC+HRS+DIS" : "CPS+HRS+CSC+FTMS+DIS",
             g_device_name);
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE 协议栈复位 reason=%d", reason);
    // 复位后旧连接句柄已失效且不会有断开事件，清理以免 conn_count 误报已连接、
    // notify_tick 持续向失效句柄发通知。
    g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
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

void ble_bikepower_set_broadcast_mask(int mask)
{
    int normalized = mask & BROADCAST_FIELD_ALL;
    if (normalized == 0) normalized = BROADCAST_FIELD_ALL;
    g_broadcast_mask = (uint8_t)normalized;
}

void ble_bikepower_set_profile(sport_profile_t profile)
{
    if (profile < 0 || profile >= SPORT_PROFILE_COUNT) return;
    g_profile = profile;
}

void ble_bikepower_set_running_data(running_snapshot_t snapshot, uint8_t heartrate_bpm)
{
    g_run_speed = snapshot.rsc_speed_1_256_mps;
    g_run_speed_x100 = (uint16_t)snapshot.speed_x100_kmh;
    g_run_running = snapshot.running;
    g_hr_bpm = heartrate_bpm;
    g_run_cadence = (uint8_t)snapshot.cadence_spm;
    g_run_stride_cm = snapshot.stride_cm;
    g_run_distance_m = snapshot.total_distance_m;
    g_run_distance_x10_m = snapshot.total_distance_x10_m;
    g_run_elapsed_s = snapshot.elapsed_time_s;
}

void ble_bikepower_set_erg_callback(ble_erg_cb_t cb)
{
    g_erg_cb = cb;
}

void ble_bikepower_set_erg_clear_callback(ble_erg_clear_cb_t cb)
{
    g_erg_clear_cb = cb;
}

void ble_bikepower_set_run_control_callback(ble_run_control_cb_t cb)
{
    g_run_control_cb = cb;
}

void ble_bikepower_set_device_name(const char *name)
{
    if (!name || !name[0]) return;
    strncpy(g_device_name, name, sizeof(g_device_name) - 1);
    g_device_name[sizeof(g_device_name) - 1] = '\0';
}

void ble_bikepower_refresh_device_name(void)
{
    // 未启动时无法访问 GAP 服务，跳过（启动流程会自动用 g_device_name 注册）
    if (!ble_bikepower_is_ready()) {
        ESP_LOGW(TAG, "BLE 未启动，跳过设备名刷新");
        return;
    }
    int rc = ble_svc_gap_device_name_set(g_device_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "GAP 设备名设置失败 rc=%d", rc);
    }
    // 已连接时不重启广播：避免 ble_gap_adv_stop 影响现有连接；
    // 连接内通过 GAP Device Name 服务读取即可拿到新名，断连回调会用新名重广播。
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_adv_stop();
        rc = start_advertising();
        if (rc != 0 && !g_stopping) {
            ESP_LOGW(TAG, "刷新设备名后重广播失败 rc=%d", rc);
        }
    }
    ESP_LOGI(TAG, "BLE 设备名已刷新: %s", g_device_name);
}

int ble_bikepower_conn_count(void)
{
    return g_conn_handle == BLE_HS_CONN_HANDLE_NONE ? 0 : 1;
}

esp_err_t ble_bikepower_start(void)
{
    g_stopping = false;
    g_start_failed = false;
    if (!g_notify_lock) {
        g_notify_lock = xSemaphoreCreateMutex();
        if (!g_notify_lock) {
            ESP_LOGE(TAG, "创建通知互斥锁失败");
            return ESP_ERR_NO_MEM;
        }
    }
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init 失败 err=%d", err);
        return err;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    const struct ble_gatt_svc_def *gatt_svcs = active_gatt_svcs();
    int rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts_count_cfg 失败 rc=%d", rc);
        nimble_port_deinit();
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts_add_svcs 失败 rc=%d", rc);
        nimble_port_deinit();
        return ESP_FAIL;
    }

    rc = ble_svc_gap_device_name_set(g_device_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "设置设备名失败 rc=%d", rc);
    }

    const esp_timer_create_args_t targs = { .callback = notify_tick, .name = "ble_notify" };
    err = esp_timer_create(&targs, &g_notify_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "创建通知定时器失败 err=%d", err);
        nimble_port_deinit();
        return err;
    }
    err = esp_timer_start_periodic(g_notify_timer, (uint64_t)BLE_NOTIFY_INTERVAL_MS * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "启动通知定时器失败 err=%d", err);
        esp_timer_delete(g_notify_timer);
        g_notify_timer = NULL;
        nimble_port_deinit();
        return err;
    }

    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE host 已启动，等待协议栈同步...");
    return ESP_OK;
}

bool ble_bikepower_is_ready(void)
{
    return g_ready;
}

bool ble_bikepower_start_failed(void)
{
    return g_start_failed;
}

bool ble_bikepower_stop(void)
{
    g_stopping = true;
    if (g_notify_timer) {
        esp_timer_stop(g_notify_timer);
        esp_timer_delete(g_notify_timer);
        g_notify_timer = NULL;
    }
    // 拿锁等待可能正在执行的 notify_tick 回调结束（esp_timer_delete 不保证等待飞行中的回调），
    // 之后再释放协议栈，确保回调不会访问已 deinit 的 NimBLE 资源。
    bool have_notify_lock = false;
    if (g_notify_lock) {
        if (xSemaphoreTake(g_notify_lock, pdMS_TO_TICKS(BLE_STOP_LOCK_WAIT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "等待通知锁超时，取消停用 BLE");
            g_stopping = false;
            return false;
        }
        have_notify_lock = true;
    }

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(g_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    ble_gap_adv_stop();

    int rc = nimble_port_stop();
    bool released = (rc == 0);
    if (released) {
        nimble_port_deinit();
        ESP_LOGI(TAG, "蓝牙已停用 释放射频");
    } else {
        ESP_LOGE(TAG, "nimble_port_stop 失败 rc=%d 射频可能仍被占用", rc);
    }
    g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    g_ready = false;
    g_start_failed = false;

    if (have_notify_lock) xSemaphoreGive(g_notify_lock);
    return released;
}
