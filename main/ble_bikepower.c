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
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "bikepower_config.h"
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
static volatile uint8_t  g_run_cadence = RUN_CADENCE_DEFAULT;
static volatile int16_t  g_run_incline_x10 = RUN_INCLINE_X10_DEFAULT;
static volatile uint16_t g_run_stride_cm = 0;
static volatile uint32_t g_run_distance_m = 0;
static volatile uint32_t g_run_elapsed_s = 0;

// 曲柄累计（CPS/CSC 共用）
static uint16_t g_crank_rev = 0;
static uint32_t g_last_crank_ms = 0;

// 预分配通知缓冲区（运行时零 malloc）
static uint8_t g_cps_buf[8];
static uint8_t g_hr_buf[2];
static uint8_t g_csc_buf[5];
static uint8_t g_rsc_buf[10];
static uint8_t g_rsc_feature_buf[4] = { 0x03, 0x00, 0x00, 0x00 };
static uint8_t g_sensor_location = 0x05;
static uint8_t g_ftms_bike_buf[8];
static size_t g_ftms_bike_len = 8;
static uint8_t g_ftms_treadmill_buf[16];
static size_t g_ftms_treadmill_len = 16;
static uint8_t g_ftms_resp_buf[3];

// FTMS 静态特征值：前 4 字节为 machine feature，后 4 字节为 target setting feature。
static const uint8_t g_ftms_cycling_feature_buf[8] = {
    0x02, 0x40, 0x00, 0x00, // cadence + power measurement
    0x08, 0x00, 0x00, 0x00, // target power setting
};
static const uint8_t g_ftms_running_feature_buf[8] = {
    0x84, 0x05, 0x00, 0x00, // distance + inclination/ramp + heart rate + elapsed time
    0x03, 0x00, 0x00, 0x00, // target speed + target inclination settings
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

/**
 * 启动 BLE 广播。
 * 组装 GAP 广播字段并以通用可连接模式开始对外广播。
 * 无参数。
 * 返回值：0表示启动成功，非0为NimBLE错误码。
 * 特殊行为：广播字段中只声明 CPS 和 HRS 服务 UUID，以兼容旧版发现行为。
 */
static int start_advertising(void);
// 发送单个特征通知/indication（定义在通知模块），FTMS 控制点回调需提前用于推送响应。
static void notify_one(uint16_t handle, const uint8_t *buf, size_t len);
static void indicate_one(uint16_t handle, const uint8_t *buf, size_t len);

// ============================ GATT 访问回调 ============================

/**
 * 把平面缓冲区写入 GATT 读取响应。
 * 将给定字节序列追加到 NimBLE 提供的输出 mbuf 中。
 * ctxt：GATT 访问上下文。
 * buf：待读取返回的数据缓冲区。
 * len：数据长度。
 * 返回值：0 表示成功，否则返回 ATT 错误码。
 * 特殊行为：内存不足时转换为 `BLE_ATT_ERR_INSUFFICIENT_RES`。
 */
static int chr_read_buf(struct ble_gatt_access_ctxt *ctxt, const uint8_t *buf, size_t len)
{
    int rc = os_mbuf_append(ctxt->om, buf, len);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/**
 * 处理 CPS Measurement 特征读访问。
 * 向客户端返回当前打包好的 Cycling Power Measurement 数据。
 * ch：连接句柄，未使用。
 * ah：属性句柄，未使用。
 * ctxt：GATT 访问上下文。
 * arg：自定义参数，未使用。
 * 返回值：0 表示成功，否则返回 ATT 错误码。
 * 特殊行为：仅支持读操作，其他操作返回 `BLE_ATT_ERR_UNLIKELY`。
 */
static int cps_access_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_cps_buf, sizeof(g_cps_buf));
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * 处理 HRS Measurement 特征读访问。
 * 向客户端返回当前打包好的 Heart Rate Measurement 数据。
 * ch：连接句柄，未使用。
 * ah：属性句柄，未使用。
 * ctxt：GATT 访问上下文。
 * arg：自定义参数，未使用。
 * 返回值：0 表示成功，否则返回 ATT 错误码。
 * 特殊行为：仅支持读操作。
 */
static int hr_access_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_hr_buf, sizeof(g_hr_buf));
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * 处理 CSC Measurement 特征读访问。
 * 向客户端返回当前打包好的 Cycling Speed and Cadence 数据。
 * ch：连接句柄，未使用。
 * ah：属性句柄，未使用。
 * ctxt：GATT 访问上下文。
 * arg：自定义参数，未使用。
 * 返回值：0 表示成功，否则返回 ATT 错误码。
 * 特殊行为：仅支持读操作。
 */
static int csc_access_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_csc_buf, sizeof(g_csc_buf));
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
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, &g_sensor_location, sizeof(g_sensor_location));
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * 处理 FTMS Feature 特征读访问。
 * 返回预定义的 Fitness Machine 特性位图。
 * ch：连接句柄，未使用。
 * ah：属性句柄，未使用。
 * ctxt：GATT 访问上下文。
 * arg：自定义参数，未使用。
 * 返回值：0 表示成功，否则返回 ATT 错误码。
 * 特殊行为：特征值为静态常量，不随运行时变化。
 */
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

/**
 * 处理 FTMS Indoor Bike Data 特征读访问。
 * 返回当前打包好的 FTMS 室内单车数据。
 * ch：连接句柄，未使用。
 * ah：属性句柄，未使用。
 * ctxt：GATT 访问上下文。
 * arg：自定义参数，未使用。
 * 返回值：0 表示成功，否则返回 ATT 错误码。
 * 特殊行为：仅支持读操作。
 */
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

/**
 * 处理 FTMS Status 特征读访问。
 * 返回当前 FTMS 状态字节缓冲区。
 * ch：连接句柄，未使用。
 * ah：属性句柄，未使用。
 * ctxt：GATT 访问上下文。
 * arg：自定义参数，未使用。
 * 返回值：0 表示成功，否则返回 ATT 错误码。
 * 特殊行为：状态主要由控制点命令更新。
 */
static int ftms_status_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_buf(ctxt, g_ftms_status_buf, sizeof(g_ftms_status_buf));
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * 发送 FTMS 控制点响应通知。
 * 把请求操作码和处理结果编码成 FTMS Response Code 并通知给当前连接。
 * request_op：原始请求操作码。
 * result：FTMS 结果码。
 * 返回值：无。
 * 特殊行为：无连接时直接丢弃，不缓存待发送响应。
 */
static void ftms_send_response(uint8_t request_op, uint8_t result)
{
    g_ftms_resp_buf[0] = FTMS_OP_RESPONSE_CODE;
    g_ftms_resp_buf[1] = request_op;
    g_ftms_resp_buf[2] = result;
    indicate_one(g_ftms_cp_handle, g_ftms_resp_buf, sizeof(g_ftms_resp_buf));
}

/**
 * 处理 FTMS Control Point 写访问。
 * 解析客户端下发的控制点指令，并驱动 ERG 回调、状态更新和响应通知。
 * ch：连接句柄，未使用。
 * ah：属性句柄，未使用。
 * ctxt：GATT 访问上下文。
 * arg：自定义参数，未使用。
 * 返回值：0 表示已处理，否则返回 ATT 错误码。
 * 特殊行为：仅支持写操作；未知操作码会返回 Not Supported。
 */
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
            bool ok = false;
            if (g_run_control_cb) ok = g_run_control_cb(BLE_RUN_CTRL_SPEED, (int)target_speed);
            ftms_send_response(op, ok ? FTMS_RESULT_SUCCESS : FTMS_RESULT_INVALID_PARAM);
        }
        break;
    case FTMS_OP_SET_TARGET_INCLINATION:
        if (g_profile != SPORT_PROFILE_RUNNING || len < 3) {
            ftms_send_response(op, FTMS_RESULT_NOT_SUPPORTED);
            break;
        }
        {
            int16_t target_incline = (int16_t)(buf[1] | ((uint16_t)buf[2] << 8));
            bool ok = false;
            if (g_run_control_cb) ok = g_run_control_cb(BLE_RUN_CTRL_INCLINE, (int)target_incline);
            ftms_send_response(op, ok ? FTMS_RESULT_SUCCESS : FTMS_RESULT_INVALID_PARAM);
        }
        break;
    case FTMS_OP_START_RESUME:
        g_ftms_status_buf[0] = 0x01;
        notify_one(g_ftms_status_handle, g_ftms_status_buf, sizeof(g_ftms_status_buf));
        if (g_profile == SPORT_PROFILE_RUNNING && g_run_control_cb) {
            (void)g_run_control_cb(BLE_RUN_CTRL_START, 0);
        }
        ftms_send_response(FTMS_OP_START_RESUME, FTMS_RESULT_SUCCESS);
        break;
    case FTMS_OP_STOP_PAUSE:
        g_ftms_status_buf[0] = 0x02;
        notify_one(g_ftms_status_handle, g_ftms_status_buf, sizeof(g_ftms_status_buf));
        if (g_profile == SPORT_PROFILE_RUNNING) {
            if (g_run_control_cb) (void)g_run_control_cb(BLE_RUN_CTRL_STOP, 0);
        } else if (g_erg_clear_cb) {
            g_erg_clear_cb(FTMS_OP_STOP_PAUSE);
        }
        ftms_send_response(FTMS_OP_STOP_PAUSE, FTMS_RESULT_SUCCESS);
        break;
    case FTMS_OP_RESET:
        g_ftms_status_buf[0] = 0x00;
        notify_one(g_ftms_status_handle, g_ftms_status_buf, sizeof(g_ftms_status_buf));
        if (g_profile == SPORT_PROFILE_RUNNING) {
            if (g_run_control_cb) (void)g_run_control_cb(BLE_RUN_CTRL_RESET, 0);
        } else if (g_erg_clear_cb) {
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

/**
 * 处理 DIS 厂商名特征读访问。
 * 返回设备厂商字符串。
 * ch：连接句柄，未使用。
 * ah：属性句柄，未使用。
 * ctxt：GATT 访问上下文。
 * arg：自定义参数，未使用。
 * 返回值：0 表示成功，否则返回 ATT 错误码。
 * 特殊行为：内容来自编译期常量。
 */
static int dis_manuf_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
        return chr_read_buf(ctxt, (const uint8_t *)DEVICE_MANUFACTURER, strlen(DEVICE_MANUFACTURER));
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * 处理 DIS 型号特征读访问。
 * 返回设备型号字符串。
 * ch：连接句柄，未使用。
 * ah：属性句柄，未使用。
 * ctxt：GATT 访问上下文。
 * arg：自定义参数，未使用。
 * 返回值：0 表示成功，否则返回 ATT 错误码。
 * 特殊行为：内容来自编译期常量。
 */
static int dis_model_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
        return chr_read_buf(ctxt, (const uint8_t *)DEVICE_MODEL, strlen(DEVICE_MODEL));
    return BLE_ATT_ERR_UNLIKELY;
}

/**
 * 处理 DIS 固件版本特征读访问。
 * 返回设备固件版本字符串。
 * ch：连接句柄，未使用。
 * ah：属性句柄，未使用。
 * ctxt：GATT 访问上下文。
 * arg：自定义参数，未使用。
 * 返回值：0 表示成功，否则返回 ATT 错误码。
 * 特殊行为：内容来自编译期常量。
 */
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

/**
 * 打包当前功率、心率和踏频到各服务通知缓冲区。
 * 根据当前运行时快照生成 CPS、HRS、CSC 和 FTMS 的二进制负载。
 * 无参数。
 * 返回值：无。
 * 特殊行为：会推进曲柄累计圈数和时间戳，保持与旧版协议输出兼容。
 */
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

    // 基于踏频推演曲柄累计量，统一驱动 CPS 和 CSC 的 rev/time 字段。
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

    bool cadence_enabled = (g_broadcast_mask & BROADCAST_FIELD_CADENCE) != 0;
    bool power_enabled = (g_broadcast_mask & BROADCAST_FIELD_POWER) != 0;
    uint16_t ftms_flags = cadence_enabled ? 0x0000 : 0x0001; // More Data=1 omits instantaneous speed.
    uint8_t off = 2;
    if (cadence_enabled) {
        ftms_flags |= 0x0004;
        uint16_t speed = cadence * 30;
        g_ftms_bike_buf[off++] = speed & 0xFF;
        g_ftms_bike_buf[off++] = (speed >> 8) & 0xFF;
        ftms_flags |= 0x0040;
        uint16_t ftms_cad = cadence * 2;
        g_ftms_bike_buf[off++] = ftms_cad & 0xFF;
        g_ftms_bike_buf[off++] = (ftms_cad >> 8) & 0xFF;
    }
    if (power_enabled) {
        ftms_flags |= 0x0040;
        g_ftms_bike_buf[off++] = power & 0xFF;
        g_ftms_bike_buf[off++] = (power >> 8) & 0xFF;
    }
    g_ftms_bike_buf[0] = ftms_flags & 0xFF;
    g_ftms_bike_buf[1] = (ftms_flags >> 8) & 0xFF;
    g_ftms_bike_len = off;
}

static void pack_running(void)
{
    uint16_t speed = g_run_speed;
    uint16_t stride = g_run_stride_cm;
    uint32_t distance = g_run_distance_m;
    int16_t incline = g_run_incline_x10;
    uint32_t elapsed = g_run_elapsed_s;

    g_hr_buf[0] = 0x00;
    g_hr_buf[1] = g_hr_bpm;

    g_rsc_buf[0] = 0x07;
    g_rsc_buf[1] = speed & 0xFF;
    g_rsc_buf[2] = (speed >> 8) & 0xFF;
    g_rsc_buf[3] = g_run_cadence;
    g_rsc_buf[4] = stride & 0xFF;
    g_rsc_buf[5] = (stride >> 8) & 0xFF;
    g_rsc_buf[6] = distance & 0xFF;
    g_rsc_buf[7] = (distance >> 8) & 0xFF;
    g_rsc_buf[8] = (distance >> 16) & 0xFF;
    g_rsc_buf[9] = (distance >> 24) & 0xFF;

    uint16_t ftms_speed = (uint16_t)(((uint32_t)speed * 360u + 128u) / 256u);
    uint16_t flags = 0x0386; // distance + inclination/ramp + heart rate + elapsed time
    uint8_t off = 2;
    g_ftms_treadmill_buf[off++] = ftms_speed & 0xFF;
    g_ftms_treadmill_buf[off++] = (ftms_speed >> 8) & 0xFF;
    g_ftms_treadmill_buf[off++] = distance & 0xFF;
    g_ftms_treadmill_buf[off++] = (distance >> 8) & 0xFF;
    g_ftms_treadmill_buf[off++] = (distance >> 16) & 0xFF;
    g_ftms_treadmill_buf[off++] = incline & 0xFF;
    g_ftms_treadmill_buf[off++] = (incline >> 8) & 0xFF;
    g_ftms_treadmill_buf[off++] = 0x00;
    g_ftms_treadmill_buf[off++] = 0x00;
    g_ftms_treadmill_buf[off++] = g_hr_bpm;
    uint16_t elapsed16 = elapsed > 0xFFFFu ? 0xFFFFu : (uint16_t)elapsed;
    g_ftms_treadmill_buf[off++] = elapsed16 & 0xFF;
    g_ftms_treadmill_buf[off++] = (elapsed16 >> 8) & 0xFF;
    g_ftms_treadmill_buf[0] = flags & 0xFF;
    g_ftms_treadmill_buf[1] = (flags >> 8) & 0xFF;
    g_ftms_treadmill_len = off;
}

/**
 * 发送单个特征通知（已持锁版本）。
 * 调用者必须已持有g_notify_lock，直接将平面缓冲区转换为 mbuf 后发送通知。
 * handle：特征值句柄。
 * buf：待发送数据缓冲区。
 * len：数据长度。
 * 返回值：无。
 * 特殊行为：状态异常或mbuf分配失败则静默跳过。
 */
static void notify_one_nolock(uint16_t handle, const uint8_t *buf, size_t len)
{
    if (g_stopping || !g_ready || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    if (om) ble_gatts_notify_custom(g_conn_handle, handle, om);
}

/**
 * 发送单个特征通知（线程安全版本）。
 * 自动拿互斥锁、检查BLE状态，将平面缓冲区转换为 mbuf 后通过 NimBLE 自定义通知发送。
 * handle：特征值句柄。
 * buf：待发送数据缓冲区。
 * len：数据长度。
 * 返回值：无。
 * 特殊行为：锁获取失败、状态异常或mbuf分配失败则静默跳过本次通知。
 */
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

/**
 * BLE 周期通知定时器回调。
 * 在有连接时打包当前数据并通知 CPS、HRS、CSC、FTMS 四个动态特征。
 * arg：未使用的定时器参数。
 * 返回值：无。
 * 特殊行为：无连接时直接返回，不做任何打包和发送。
 */
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

/**
 * 标记 BLE 启动/重广播失败，统一日志与状态更新。
 * reason：失败场景描述，用于日志定位。
 * rc：NimBLE 错误码。
 * 返回值：无。
 * 特殊行为：仅清 g_ready、置 g_start_failed，交由 app_main 主任务上下文清理重试；
 *   绝不在 NimBLE 回调上下文里直接调用 stop/deinit，避免在协议栈回调中重入释放。
 */
static void mark_start_failed(const char *reason, int rc)
{
    ESP_LOGE(TAG, "%s rc=%d", reason, rc);
    g_ready = false;
    g_start_failed = true;
}

/**
 * 处理 GAP 连接与广播事件。
 * 维护当前连接句柄，并在连接失败、断开或广播结束时重新启动广播。
 * event：GAP 事件对象。
 * arg：自定义参数，未使用。
 * 返回值：0。
 * 特殊行为：当前实现默认保持单连接模式，断开后自动回到可发现状态。
 */
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

/**
 * 启动 BLE 广播。
 * 配置广播字段、设备名和服务 UUID，并开始无限时长广播。
 * 无参数。
 * 返回值：0表示启动成功，非0为NimBLE错误码。
 * 特殊行为：广播间隔从配置常量换算为 0.625ms 单位写入 NimBLE。
 */
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
        BLE_UUID16_INIT(FTMS_SERVICE_UUID),
    };
    static ble_uuid16_t running_adv_uuids[] = {
        BLE_UUID16_INIT(RSC_SERVICE_UUID),
        BLE_UUID16_INIT(FTMS_SERVICE_UUID),
    };
    static ble_uuid16_t scan_rsp_uuids[] = {
        BLE_UUID16_INIT(HR_SERVICE_UUID),
    };
    // 广播包空间有限：主广播放 App 设备类型发现最关键的服务，完整能力由连接后 GATT 发现获取。
    fields.uuids16 = (g_profile == SPORT_PROFILE_RUNNING) ? running_adv_uuids : cycling_adv_uuids;
    fields.num_uuids16 = 2;
    fields.uuids16_is_complete = 1;
    // 31 字节广播包预算：flags(3) + 完整 16 位 UUID 列表(2+2×2=6) 占 9 字节，
    // 留给名字 AD 22 字节即最多 BLE_ADV_NAME_MAX_LEN 个字符。超出部分（完整名）放 scan response，
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

/**
 * NimBLE 协议栈同步完成回调。
 * 在地址准备好后推断地址类型并启动首次广播。
 * 无参数。
 * 返回值：无。
 * 特殊行为：若地址准备或广播启动失败则自动停止BLE，触发主逻辑重试。
 */
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

/**
 * NimBLE 协议栈复位回调。
 * 在协议栈发生复位时记录原因并清理连接状态，便于诊断并恢复 BLE。
 * reason：复位原因码。
 * 返回值：无。
 * 特殊行为：复位不会产生 DISCONNECT 事件，需在此手动清连接句柄；
 *           复位后 NimBLE 会重新回调 sync_cb 触发重新广播。
 */
static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE 协议栈复位 reason=%d", reason);
    // 复位后旧连接句柄已失效且不会有断开事件，清理以免 conn_count 误报已连接、
    // notify_tick 持续向失效句柄发通知。
    g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

/**
 * NimBLE host 线程入口。
 * 运行 NimBLE 事件循环并在退出后执行 FreeRTOS 端清理。
 * param：未使用的线程参数。
 * 返回值：无。
 * 特殊行为：`nimble_port_run()` 会阻塞直到 host 停止。
 */
static void ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "NimBLE host task 启动");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// ============================ 公共接口 ============================

/**
 * 更新 BLE 要广播和通知的数据快照。
 * 写入当前功率、心率和踏频，供周期打包逻辑读取。
 * power_w：功率，单位瓦特。
 * heartrate_bpm：心率，单位 BPM。
 * cadence_rpm：踏频，单位 RPM。
 * 返回值：无。
 * 特殊行为：只更新共享变量，不立即触发打包或发送。
 */
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
    g_hr_bpm = heartrate_bpm;
    g_run_cadence = (uint8_t)snapshot.cadence_spm;
    g_run_incline_x10 = (int16_t)snapshot.incline_x10;
    g_run_stride_cm = snapshot.stride_cm;
    g_run_distance_m = snapshot.total_distance_m;
    g_run_elapsed_s = snapshot.elapsed_time_s;
}

/**
 * 注册 ERG 目标功率回调。
 * 保存应用层提供的 FTMS ERG 回调。
 * cb：回调函数指针，可为 NULL。
 * 返回值：无。
 * 特殊行为：新的回调会直接替换旧回调。
 */
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

/**
 * 设置 BLE 设备名。
 * 更新内部设备名缓冲区，供广播和 GAP 设备名设置使用。
 * name：新的设备名字符串。
 * 返回值：无。
 * 特殊行为：空指针或空串时保持现有设备名不变。
 */
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

/**
 * 获取当前 BLE 连接数。
 * 根据连接句柄是否有效返回单连接状态。
 * 无参数。
 * 返回值：0 表示未连接，1 表示已连接。
 * 特殊行为：当前模块只支持单连接，因此不会返回更大的值。
 */
int ble_bikepower_conn_count(void)
{
    return g_conn_handle == BLE_HS_CONN_HANDLE_NONE ? 0 : 1;
}

/**
 * 启动 BLE 蓝牙功率计模块。
 * 初始化 NimBLE、注册 GATT 服务、设置设备名、启动周期通知定时器并创建 host 线程。
 * 无参数。
 * 返回值：`ESP_OK` 表示启动成功，否则为对应错误码。
 * 特殊行为：任一步失败都会回滚已分配资源，保持模块可再次尝试启动。
 */
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

/**
 * 查询 BLE 模块是否已成功启动并就绪。
 * 无参数。
 * 返回值：`true` 表示已启动就绪，`false` 表示未启动或启动失败。
 * 特殊行为：仅返回内部就绪标志快照。
 */
bool ble_bikepower_is_ready(void)
{
    return g_ready;
}

bool ble_bikepower_start_failed(void)
{
    return g_start_failed;
}

/**
 * 停止 BLE 蓝牙功率计模块。
 * 停止周期通知、终止连接、停止广播并关闭 NimBLE host。
 * 无参数。
 * 返回值：`true` 表示射频已成功释放，`false` 表示 nimble_port_stop 失败、射频可能仍被占用。
 * 特殊行为：供 WiFi 配网前释放射频资源使用，结束后会清空当前连接句柄。
 */
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
    if (g_notify_lock) xSemaphoreTake(g_notify_lock, portMAX_DELAY);

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

    if (g_notify_lock) xSemaphoreGive(g_notify_lock);
    return released;
}
