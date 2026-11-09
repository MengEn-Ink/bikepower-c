/**
 * BLE 蓝牙功率计模块（NimBLE）
 * 五服务：Cycling Power(0x1818) + Heart Rate(0x180D) + CSC(0x1816)
 *         + FTMS(0x1826) + Device Information(0x180A)
 *
 * 当前通知数据格式：
 *   CPS  Measurement: flags(2)=0x0020 + power(2) + crankRev(2) + crankTime1024(2)
 *   HR   Measurement: flags(1)=0x00 + bpm(1)
 *   CSC  Measurement: flags(1)=0x02 + crankRev(2) + crankTime1024(2)
 *   FTMS IndoorBike : flags(2)=0x0044 + speed(2)=cad*30 + cadence(2)=cad*2 + power(2)
 *   FTMS ControlPoint: ERG OpCode 状态机
 */
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "ble_hs_priv.h"
#include "host/util/util.h"
#include "store/config/ble_store_config.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "bikepower_config.h"
#include "ble_config_codec.h"
#include "ble_config_service.h"
#include "ble_telemetry_service.h"
#include "ble_payload_codec.h"
#include "domain_config.h"
#include "running_engine.h"
#include "ble_bikepower.h"
#include "board_detect.h"
#include "ble_power_bridge.h"
#include "ble_bond_cache.h"

static const char *TAG = "BLE";

void ble_store_config_init(void);
static atomic_bool s_security_store_failed;
static esp_err_t security_store_sync_value(int obj_type, const union ble_store_value *value);

static int security_store_read(int obj_type, const union ble_store_key *key,
                               union ble_store_value *value)
{
    if (!key || !value) return BLE_HS_EINVAL;
    if (s_security_store_failed) return BLE_HS_ESTORE_FAIL;
    int rc = ble_store_config_read(obj_type, key, value);
#if CONFIG_BIKEPOWER_DIAGNOSTIC_LOGS
    ESP_LOGW(TAG, "DIAG: store_read type=%d rc=%d", obj_type, rc);
#endif
    return rc;
}

static int security_store_write(int obj_type, const union ble_store_value *value)
{
    if (!value) return BLE_HS_EINVAL;
    if (s_security_store_failed) return BLE_HS_ESTORE_FAIL;
    int rc = ble_store_config_write(obj_type, value);
#if CONFIG_BIKEPOWER_DIAGNOSTIC_LOGS
    ESP_LOGW(TAG, "DIAG: store_write type=%d rc=%d", obj_type, rc);
#endif
    if (rc == 0 && security_store_sync_value(obj_type, value) != ESP_OK) {
        rc = BLE_HS_ESTORE_FAIL;
    }
    if (rc != 0 && rc != BLE_HS_ESTORE_CAP && rc != BLE_HS_ENOTSUP) {
        s_security_store_failed = true;
        ESP_LOGE(TAG, "绑定存储写入失败 type=%d rc=%d，拒绝后续加密恢复", obj_type, rc);
    }
    return rc;
}

static int security_store_delete(int obj_type, const union ble_store_key *key)
{
    if (!key) return BLE_HS_EINVAL;
    if (s_security_store_failed) return BLE_HS_ESTORE_FAIL;
    int rc = ble_store_config_delete(obj_type, key);
    if (rc != 0 && rc != BLE_HS_ENOENT && rc != BLE_HS_ENOTSUP) {
        s_security_store_failed = true;
        ESP_LOGE(TAG, "绑定存储删除失败 type=%d rc=%d", obj_type, rc);
    }
    return rc;
}

static int security_store_status(struct ble_store_status_event *event, void *arg)
{
    (void)arg;
    if (!event) return BLE_HS_EINVAL;
    if (event->event_code == BLE_STORE_EVENT_FULL
            || event->event_code == BLE_STORE_EVENT_OVERFLOW) {
        return BLE_HS_ESTORE_CAP;
    }
    return BLE_HS_ENOTSUP;
}

/* 标准初始化吞掉恢复错误，因此逐记录核验格式及恢复后的 RAM 内容。 */
static esp_err_t security_store_validate(void)
{
    static const struct {
        const char *prefix;
        int type;
        size_t size;
        unsigned capacity;
    } records[] = {
        { BLE_BOND_NVS_OUR_SEC_PREFIX, BLE_STORE_OBJ_TYPE_OUR_SEC,
          sizeof(struct ble_store_value_sec), CONFIG_BT_NIMBLE_MAX_BONDS },
        { BLE_BOND_NVS_PEER_SEC_PREFIX, BLE_STORE_OBJ_TYPE_PEER_SEC,
          sizeof(struct ble_store_value_sec), CONFIG_BT_NIMBLE_MAX_BONDS },
        { BLE_BOND_NVS_CCCD_PREFIX, BLE_STORE_OBJ_TYPE_CCCD,
          sizeof(struct ble_store_value_cccd), CONFIG_BT_NIMBLE_MAX_CCCDS },
        { BLE_BOND_NVS_CSFC_PREFIX, BLE_STORE_OBJ_TYPE_CSFC,
          sizeof(struct ble_store_value_csfc), MYNEWT_VAL(BLE_STORE_MAX_CSFCS) },
        { BLE_BOND_NVS_IRK_PREFIX, BLE_STORE_OBJ_TYPE_LOCAL_IRK,
          sizeof(struct ble_store_value_local_irk), CONFIG_BT_NIMBLE_MAX_BONDS },
        { BLE_BOND_NVS_RPA_PREFIX, BLE_STORE_OBJ_TYPE_PEER_ADDR,
          sizeof(struct ble_store_value_rpa_rec), CONFIG_BT_NIMBLE_MAX_BONDS },
    };
    nvs_handle_t handle;
    esp_err_t err = nvs_open(BLE_BOND_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    nvs_iterator_t it = NULL;
    err = nvs_entry_find_in_handle(handle, NVS_TYPE_ANY, &it);
    while (err == ESP_OK) {
        nvs_entry_info_t info;
        err = nvs_entry_info(it, &info);
        if (err != ESP_OK) break;
        size_t record = sizeof(records) / sizeof(records[0]);
        for (size_t i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
            size_t prefix_len = strlen(records[i].prefix);
            if (strncmp(info.key, records[i].prefix, prefix_len) != 0) continue;
            unsigned index = 0;
            const char *digit = info.key + prefix_len;
            if (!*digit) break;
            for (; *digit >= '0' && *digit <= '9'; ++digit) {
                index = index * 10 + (unsigned)(*digit - '0');
                if (index > records[i].capacity) break;
            }
            if (*digit || index == 0 || index > records[i].capacity) break;
            record = i;
            break;
        }
        if (record == sizeof(records) / sizeof(records[0]) || info.type != NVS_TYPE_BLOB) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        union ble_store_value stored = {0};
        size_t size = sizeof(stored);
        err = nvs_get_blob(handle, info.key, &stored, &size);
        if (err != ESP_OK) break;
        if (size != records[record].size) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        bool found = false;
        for (unsigned index = 0; index < records[record].capacity; ++index) {
            union ble_store_key key = {0};
            union ble_store_value restored = {0};
            switch (records[record].type) {
            case BLE_STORE_OBJ_TYPE_OUR_SEC:
            case BLE_STORE_OBJ_TYPE_PEER_SEC:
                key.sec.peer_addr = *BLE_ADDR_ANY;
                key.sec.idx = index;
                break;
            case BLE_STORE_OBJ_TYPE_CCCD:
                key.cccd.peer_addr = *BLE_ADDR_ANY;
                key.cccd.idx = index;
                break;
            case BLE_STORE_OBJ_TYPE_CSFC:
                key.csfc.peer_addr = *BLE_ADDR_ANY;
                key.csfc.idx = index;
                break;
            case BLE_STORE_OBJ_TYPE_LOCAL_IRK:
                key.local_irk.addr = *BLE_ADDR_ANY;
                key.local_irk.idx = index;
                break;
            case BLE_STORE_OBJ_TYPE_PEER_ADDR:
                key.rpa_rec.peer_rpa_addr = stored.rpa_rec.peer_rpa_addr;
                break;
            }
            if (ble_store_config_read(records[record].type, &key, &restored) == 0
                    && memcmp(&stored, &restored, size) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            err = ESP_ERR_INVALID_STATE;
            break;
        }
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    nvs_close(handle);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

/* 上游按记录数量持久化，数量不变的同身份更新需要补充实际落盘与回读。 */
static esp_err_t security_store_sync_value(int obj_type, const union ble_store_value *value)
{
    const char *prefix;
    size_t size;
    unsigned capacity;
    switch (obj_type) {
    case BLE_STORE_OBJ_TYPE_OUR_SEC:
        prefix = BLE_BOND_NVS_OUR_SEC_PREFIX;
        size = sizeof(value->sec);
        capacity = CONFIG_BT_NIMBLE_MAX_BONDS;
        break;
    case BLE_STORE_OBJ_TYPE_PEER_SEC:
        prefix = BLE_BOND_NVS_PEER_SEC_PREFIX;
        size = sizeof(value->sec);
        capacity = CONFIG_BT_NIMBLE_MAX_BONDS;
        break;
    case BLE_STORE_OBJ_TYPE_CCCD:
        prefix = BLE_BOND_NVS_CCCD_PREFIX;
        size = sizeof(value->cccd);
        capacity = CONFIG_BT_NIMBLE_MAX_CCCDS;
        break;
    case BLE_STORE_OBJ_TYPE_CSFC:
        prefix = BLE_BOND_NVS_CSFC_PREFIX;
        size = sizeof(value->csfc);
        capacity = MYNEWT_VAL(BLE_STORE_MAX_CSFCS);
        break;
    case BLE_STORE_OBJ_TYPE_LOCAL_IRK:
        prefix = BLE_BOND_NVS_IRK_PREFIX;
        size = sizeof(value->local_irk);
        capacity = CONFIG_BT_NIMBLE_MAX_BONDS;
        break;
    case BLE_STORE_OBJ_TYPE_PEER_ADDR:
        prefix = BLE_BOND_NVS_RPA_PREFIX;
        size = sizeof(value->rpa_rec);
        capacity = CONFIG_BT_NIMBLE_MAX_BONDS;
        break;
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(BLE_BOND_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    union ble_store_key key = {0};
    union ble_store_value expected = {0};
    ble_store_key_from_value(obj_type, &key, value);
    if (ble_store_config_read(obj_type, &key, &expected) != 0) {
        nvs_close(handle);
        return ESP_ERR_INVALID_STATE;
    }
    char name[NVS_KEY_NAME_MAX_SIZE];
    bool found = false;
    for (unsigned index = 1; index <= capacity; ++index) {
        snprintf(name, sizeof(name), "%s%u", prefix, index);
        union ble_store_value stored = {0};
        size_t length = sizeof(stored);
        err = nvs_get_blob(handle, name, &stored, &length);
        if (err == ESP_ERR_NVS_NOT_FOUND) continue;
        if (err != ESP_OK || length != size) {
            if (err == ESP_OK) err = ESP_ERR_INVALID_SIZE;
            break;
        }
        union ble_store_key stored_key = {0};
        ble_store_key_from_value(obj_type, &stored_key, &stored);
        if (memcmp(&key, &stored_key, sizeof(key)) != 0) continue;
        found = true;
        if (memcmp(&stored, &expected, size) == 0) break;
        err = nvs_set_blob(handle, name, &expected, size);
        if (err == ESP_OK) err = nvs_commit(handle);
        if (err == ESP_OK) {
            length = sizeof(stored);
            err = nvs_get_blob(handle, name, &stored, &length);
            if (err == ESP_OK && (length != size || memcmp(&stored, &expected, size) != 0)) {
                err = ESP_ERR_INVALID_STATE;
            }
        }
        break;
    }
    nvs_close(handle);
    if (!found && (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND)) return ESP_ERR_INVALID_STATE;
    return err;
}

static esp_err_t security_store_activate(void)
{
    if (s_security_store_failed) return ESP_ERR_INVALID_STATE;
    esp_err_t err = security_store_validate();
    if (err != ESP_OK) {
        s_security_store_failed = true;
        return err;
    }
    ble_hs_cfg.store_read_cb = security_store_read;
    ble_hs_cfg.store_write_cb = security_store_write;
    ble_hs_cfg.store_delete_cb = security_store_delete;
    ble_hs_cfg.store_status_cb = security_store_status;
    return ESP_OK;
}

static esp_err_t configure_session_security(void)
{
    s_security_store_failed = false;
    ble_store_config_init();
    esp_err_t err = security_store_validate();
    if (err != ESP_OK) {
        s_security_store_failed = true;
        ESP_LOGE(TAG, "绑定存储恢复校验失败 err=%d，不擦除记录", err);
        return err;
    }
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_read_cb = security_store_read;
    ble_hs_cfg.store_write_cb = security_store_write;
    ble_hs_cfg.store_delete_cb = security_store_delete;
    ble_hs_cfg.store_status_cb = security_store_status;
    return ESP_OK;
}

static uint8_t g_own_addr_type;
static atomic_bool s_rebuild_pending;
static atomic_uint_least16_t g_ftms_owner = BLE_HS_CONN_HANDLE_NONE;
static atomic_uint_least16_t g_ftms_connection = BLE_HS_CONN_HANDLE_NONE;
static atomic_uint_least16_t g_rsc_connection = BLE_HS_CONN_HANDLE_NONE;

static uint16_t g_cps_val_handle;
static uint16_t g_hr_val_handle;
static uint16_t g_csc_val_handle;
static uint16_t g_rsc_val_handle;
static uint16_t g_ftms_bike_handle;
static uint16_t g_ftms_treadmill_handle;
static uint16_t g_ftms_cp_handle;
static uint16_t g_ftms_status_handle;
static uint16_t g_rsc_cp_handle;
static atomic_bool g_rsc_pending;
static atomic_uint_least32_t g_rsc_response_token;
static ble_rsc_distance_cb_t g_rsc_distance_cb;
static atomic_bool g_control_async;
static atomic_uchar g_control_op, g_control_parameter;

// 当前通知数据（由领域层写入，notify_tick 读取）
typedef struct {
    uint16_t power;
    uint8_t heartrate;
    uint16_t cadence;
    running_snapshot_t running;
    bridge_snapshot_t bridge;
    bool real;
    uint8_t mask;
} ble_output_snapshot_t;
static portMUX_TYPE s_data_lock = portMUX_INITIALIZER_UNLOCKED;
static ble_output_snapshot_t s_output = {
    .power = DEFAULT_POWER, .heartrate = DEFAULT_HEARTRATE, .cadence = DEFAULT_CADENCE,
    .running = { .cadence_spm = RUN_CADENCE_DEFAULT, .running = true },
};
static atomic_uchar g_broadcast_mask = BROADCAST_MASK_DEFAULT;
static atomic_bool g_bridge_mode;

// 曲柄累计（CPS/CSC 共用）
static uint16_t g_crank_rev = 0;
static uint32_t g_last_crank_ms = 0;

// 预分配通知缓冲区（运行时零 malloc）
static uint8_t g_cps_buf[BLE_PAYLOAD_CPS_LEN];
static size_t g_cps_len = BLE_PAYLOAD_CPS_LEN;
static bool s_bridge_csc_valid;
static uint8_t g_hr_buf[BLE_PAYLOAD_HRS_LEN];
static uint8_t g_csc_buf[BLE_PAYLOAD_CSC_LEN];
// Feature 仅声明生产通知支持的曲柄转数，不声明轮速、扭矩或多位置能力。
static const uint8_t g_cps_feature_buf[4] = { 0x08, 0x00, 0x10, 0x00 };
static const uint8_t g_csc_feature_buf[2] = { 0x02, 0x00 };
static uint8_t g_rsc_buf[BLE_PAYLOAD_RSC_LEN];
static uint8_t g_rsc_feature_buf[2] = { 0x07, 0x00 };
static uint8_t g_sensor_location = 0x05;
static uint8_t g_ftms_bike_buf[BLE_PAYLOAD_FTMS_BIKE_MAX_LEN];
static size_t g_ftms_bike_len = BLE_PAYLOAD_FTMS_BIKE_MAX_LEN;
static uint8_t g_ftms_treadmill_buf[BLE_PAYLOAD_FTMS_TREADMILL_LEN];
static size_t g_ftms_treadmill_len = BLE_PAYLOAD_FTMS_TREADMILL_LEN;
static uint8_t g_ftms_resp_buf[3];

// FTMS 静态特征值：前 4 字节为 machine feature，后 4 字节为 target setting feature。
static const uint8_t g_ftms_cycling_feature_buf[8] = {
    0x02, 0x44, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x00, // target power setting
};
static const uint8_t g_ftms_running_feature_buf[8] = {
    0x04, 0x14, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
};
static const uint8_t g_ftms_power_range_buf[6] = {
    POWER_MIN & 0xff, (POWER_MIN >> 8) & 0xff,
    POWER_MAX & 0xff, (POWER_MAX >> 8) & 0xff, 1, 0,
};
static const uint8_t g_ftms_speed_range_buf[6] = {
    RUN_SPEED_X100_MIN & 0xff, (RUN_SPEED_X100_MIN >> 8) & 0xff,
    RUN_SPEED_X100_MAX & 0xff, (RUN_SPEED_X100_MAX >> 8) & 0xff, 1, 0,
};
static atomic_bool g_ftms_control = false;
static atomic_bool g_ftms_pending = false;
// Reset/Started 为单字节；Stopped/Paused 带一字节控制信息。
static uint8_t g_ftms_status_buf[2] = { FTMS_STATUS_RESET };
static size_t g_ftms_status_len = 1;

static ble_erg_cb_t g_erg_cb = NULL;
static ble_erg_clear_cb_t g_erg_clear_cb = NULL;
static ble_run_control_cb_t g_run_control_cb = NULL;
static atomic_int g_profile = DEFAULT_SPORT_PROFILE;
static char g_device_name[DEVICE_NAME_MAX] = DEVICE_NAME;
static esp_timer_handle_t g_notify_timer = NULL;
// 生命周期由主任务和 host 回调发布；释放 NimBLE 仍须取得通知互斥锁。
static atomic_bool g_ready = false;
static atomic_bool g_stopping = false;
static atomic_bool g_start_failed = false;
// 通知互斥锁：保证 notify_tick 回调与 ble_bikepower_stop 的 deinit 互斥，
// stop 在释放 NimBLE 前必须先拿到锁，确保没有 notify 回调正在访问协议栈（消除 UAF 窗口）。
static SemaphoreHandle_t g_notify_lock = NULL;
static struct ble_npl_event s_notify_event;
static atomic_bool s_data_notify_pending;
static void host_notify_event(struct ble_npl_event *event);
static void notify_schedule(void);

// 固定客户端槽；连接代次隔离断连后的异步响应。
// BEGIN_INDICATION_STATE
typedef struct {
    uint32_t token;
    uint32_t generation;
    uint16_t connection;
    uint16_t handle;
    uint8_t length;
    bool ready;
    uint8_t payload[BLE_CONFIG_CONTROL_RESPONSE_LEN];
} ble_indication_entry_t;
typedef struct {
    atomic_uint_least16_t connection;
    uint32_t generation;
    atomic_uint_least16_t subscriptions[BLE_NOTIFY_SUBSCRIPTION_MAX];
    atomic_bool ftms_subscribed, rsc_subscribed;
    ble_indication_entry_t indications[BLE_INDICATION_QUEUE_SIZE];
    uint8_t head, count;
    bool active, failed;
} ble_client_t;
static portMUX_TYPE s_indication_lock = portMUX_INITIALIZER_UNLOCKED;
static ble_client_t s_clients[BLE_DOWNSTREAM_MAX] = {
    { .connection = BLE_HS_CONN_HANDLE_NONE },
    { .connection = BLE_HS_CONN_HANDLE_NONE },
};
static uint32_t s_indication_sequence;
static atomic_uint_least32_t s_control_generation;
static atomic_uint_least32_t g_ftms_response_token;
// END_INDICATION_STATE

static ble_client_t * client_find(uint16_t connection)
{
    if (connection == BLE_HS_CONN_HANDLE_NONE) return NULL;
    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i) {
        if (s_clients[i].connection == connection) return &s_clients[i];
    }
    return NULL;
}

static bool client_indicates(uint16_t connection, uint16_t handle)
{
    ble_client_t *client = client_find(connection);
    return client && ((handle == g_ftms_cp_handle && client->ftms_subscribed)
        || (handle == g_rsc_cp_handle && client->rsc_subscribed));
}

// DIS 身份覆盖：由应用层落盘后通过 ble_bikepower_set_identity() 写入，host 任务只读。
static portMUX_TYPE g_identity_lock = portMUX_INITIALIZER_UNLOCKED;
static device_identity_t g_identity;
static ble_identity_write_cb_t g_identity_write_cb = NULL;

/**
 * 启动 BLE 广播。
 * 组装 GAP 广播字段并以通用可连接模式开始对外广播。
 * 无参数。
 * 返回值：0表示启动成功，非0为NimBLE错误码。
 * 特殊行为：主广播声明当前 profile 的关键服务 UUID，完整设备名放不下时写入 scan response。
 */
static int start_advertising(void);
// 发送单个特征通知/indication（定义在通知模块），FTMS 控制点回调需提前用于推送响应。
static void notify_one(uint16_t handle, const uint8_t *buf, size_t len);
static void indicate_one(uint32_t token, const uint8_t *buf, size_t len);
static bool indicate_try(uint16_t handle, const uint8_t *buf, size_t len);
static uint32_t indication_reserve(uint16_t connection, uint16_t handle);
static void indication_reset(void);
static void indication_complete(uint16_t connection, uint16_t handle, int status);
static void indication_subscription_revoked(uint16_t connection, uint16_t handle);
static void indication_pump_nolock(void);
static bool indication_submit(uint32_t token, const uint8_t *buf, size_t len);

static void subscriptions_reset(void)
{
    for (size_t c = 0; c < BLE_DOWNSTREAM_MAX; ++c) {
        for (size_t i = 0; i < BLE_NOTIFY_SUBSCRIPTION_MAX; ++i) s_clients[c].subscriptions[i] = 0;
    }
}

static void subscription_update(uint16_t connection, uint16_t handle, bool enabled)
{
    ble_client_t *client = client_find(connection);
    if (!client || !handle) return;
    for (size_t i = 0; i < BLE_NOTIFY_SUBSCRIPTION_MAX; ++i) {
        if (client->subscriptions[i] == handle) {
            if (!enabled) client->subscriptions[i] = 0;
            return;
        }
    }
    if (enabled) for (size_t i = 0; i < BLE_NOTIFY_SUBSCRIPTION_MAX; ++i) {
        if (!client->subscriptions[i]) { client->subscriptions[i] = handle; return; }
    }
}

static bool notification_subscribed(uint16_t connection, uint16_t handle)
{
    ble_client_t *client = client_find(connection);
    if (!client || !handle) return false;
    for (size_t i = 0; i < BLE_NOTIFY_SUBSCRIPTION_MAX; ++i)
        if (client->subscriptions[i] == handle) return true;
    return false;
}

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
static int chr_read_measurement(struct ble_gatt_access_ctxt *ctxt, const uint8_t *buf,
                                size_t len, const size_t *dynamic_len)
{
    uint8_t value[DEVICE_IDENTITY_WIRE_MAX];
    portENTER_CRITICAL(&s_data_lock);
    if (dynamic_len) len = *dynamic_len;
    if (len > sizeof(value)) {
        portEXIT_CRITICAL(&s_data_lock);
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    memcpy(value, buf, len);
    portEXIT_CRITICAL(&s_data_lock);
    int rc = os_mbuf_append(ctxt->om, value, len);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int chr_read_buf(struct ble_gatt_access_ctxt *ctxt, const uint8_t *buf, size_t len)
{
    return chr_read_measurement(ctxt, buf, len, NULL);
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
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_measurement(ctxt, g_cps_buf, 0, &g_cps_len);
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

static int ftms_power_range_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return chr_read_buf(ctxt, g_ftms_power_range_buf, sizeof(g_ftms_power_range_buf));
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int ftms_speed_range_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return chr_read_buf(ctxt, g_ftms_speed_range_buf, sizeof(g_ftms_speed_range_buf));
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
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_measurement(ctxt, g_ftms_bike_buf, 0, &g_ftms_bike_len);
    return BLE_ATT_ERR_UNLIKELY;
}

static int ftms_treadmill_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_measurement(ctxt, g_ftms_treadmill_buf, 0, &g_ftms_treadmill_len);
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
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return chr_read_measurement(ctxt, g_ftms_status_buf, 0, &g_ftms_status_len);
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
static void ftms_release_owner(uint16_t connection)
{
    portENTER_CRITICAL(&s_indication_lock);
    if (connection != BLE_HS_CONN_HANDLE_NONE && g_ftms_owner == connection) {
        g_ftms_owner = BLE_HS_CONN_HANDLE_NONE;
        g_ftms_control = false;
        ++s_control_generation;
    }
    portEXIT_CRITICAL(&s_indication_lock);
}

static void ftms_send_response(uint8_t request_op, uint8_t result)
{
    g_ftms_resp_buf[0] = FTMS_OP_RESPONSE_CODE;
    g_ftms_resp_buf[1] = request_op;
    g_ftms_resp_buf[2] = result;
    if (!indicate_try(g_ftms_cp_handle, g_ftms_resp_buf, sizeof(g_ftms_resp_buf))) {
        g_ftms_pending = false;
        ftms_release_owner(g_ftms_connection);
        ESP_LOGW(TAG, "FTMS 响应发送失败，控制权已释放");
    }
}

/**
 * 发布 FTMS Fitness Machine Status。
 * status：Fitness Machine Status 特征自己的操作码，不是 Control Point 操作码。
 * parameter：Stopped/Paused 的控制信息，其他状态忽略。
 * 返回值：无。
 * 特殊行为：Reset/Started 为 1 字节，Stopped/Paused 为 2 字节。
 */
static void ftms_send_status(uint8_t status, uint8_t parameter)
{
    portENTER_CRITICAL(&s_data_lock);
    g_ftms_status_buf[0] = status;
    g_ftms_status_buf[1] = parameter;
    g_ftms_status_len = status == FTMS_STATUS_STOPPED_PAUSED_BY_USER ? 2 : 1;
    portEXIT_CRITICAL(&s_data_lock);
    notify_one(g_ftms_status_handle, g_ftms_status_buf, g_ftms_status_len);
}

/**
 * 处理 FTMS Control Point 写访问。
 * 解析客户端下发的控制点指令，并驱动 ERG 回调、状态更新和响应通知。
 * ch：连接句柄，必须为当前下游连接。
 * ah：属性句柄，未使用。
 * ctxt：GATT 访问上下文。
 * arg：自定义参数，未使用。
 * 返回值：0 表示已处理，否则返回 ATT 错误码。
 * 特殊行为：仅支持写操作；未知操作码会返回 Not Supported。
 */
static void ftms_operation_accepted(uint8_t op, uint8_t parameter)
{
    if (g_control_async) return;
    if (op == FTMS_OP_RESET) { ftms_release_owner(g_ftms_connection); ftms_send_status(FTMS_STATUS_RESET, 0); }
    else if (op == FTMS_OP_START_RESUME) ftms_send_status(FTMS_STATUS_STARTED_RESUMED_BY_USER, 0);
    else if (op == FTMS_OP_STOP_PAUSE) ftms_send_status(FTMS_STATUS_STOPPED_PAUSED_BY_USER, parameter);
    ftms_send_response(op, FTMS_RESULT_SUCCESS);
}

static int rsc_cp_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ah; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || !client_find(ch) || !g_ready || g_stopping)
        return BLE_ATT_ERR_UNLIKELY;
    if (!client_indicates(ch, g_rsc_cp_handle)) return RSC_ATT_ERR_CCCD_IMPROPERLY_CONFIGURED;
    if (g_rsc_pending) return RSC_ATT_ERR_PROCEDURE_IN_PROGRESS;
    uint8_t bytes[RSC_CP_REQUEST_MAX]; uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, bytes, sizeof(bytes), &len) || !len)
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint32_t token = indication_reserve(ch, g_rsc_cp_handle);
    if (!token) return BLE_ATT_ERR_INSUFFICIENT_RES;
    g_rsc_pending = true; g_rsc_response_token = token;
    g_rsc_connection = ch;
    uint8_t result = RSC_RESULT_NOT_SUPPORTED;
    if (bytes[0] == RSC_OP_SET_CUMULATIVE_VALUE) {
        result = RSC_RESULT_INVALID_PARAM;
        if (len == RSC_CP_REQUEST_MAX) {
            uint32_t dm = (uint32_t)bytes[1] | ((uint32_t)bytes[2] << 8)
                | ((uint32_t)bytes[3] << 16) | ((uint32_t)bytes[4] << 24);
            if (g_rsc_distance_cb && g_rsc_distance_cb(dm)) return 0;
            result = RSC_RESULT_OPERATION_FAILED;
        }
    }
    uint8_t response[] = { RSC_OP_RESPONSE_CODE, bytes[0], result };
    if (!indication_submit(token, response, sizeof(response))) return BLE_ATT_ERR_INSUFFICIENT_RES;
    return 0;
}

static int ftms_cp_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ah; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    if (!client_find(ch) || g_stopping || !g_ready || s_security_store_failed) return BLE_ATT_ERR_UNLIKELY;
    if (!client_indicates(ch, g_ftms_cp_handle)) return FTMS_ATT_ERR_CCCD_IMPROPERLY_CONFIGURED;

    uint8_t buf[FTMS_CP_BUF_SIZE];
    uint16_t len = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len);
    if (rc != 0 || len < 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    uint8_t op = buf[0];
    if (g_ftms_owner != BLE_HS_CONN_HANDLE_NONE && g_ftms_owner != ch) {
        uint32_t denied = indication_reserve(ch, g_ftms_cp_handle);
        uint8_t response[] = { FTMS_OP_RESPONSE_CODE, op, FTMS_RESULT_CONTROL_NOT_PERMITTED };
        return denied && indication_submit(denied, response, sizeof(response)) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (g_ftms_pending) return FTMS_ATT_ERR_PROCEDURE_IN_PROGRESS;
    g_ftms_response_token = indication_reserve(ch, g_ftms_cp_handle);
    if (!g_ftms_response_token) return BLE_ATT_ERR_INSUFFICIENT_RES;
    g_ftms_connection = ch;
    g_ftms_pending = true;
    g_control_op = op;
    g_control_parameter = op == FTMS_OP_STOP_PAUSE && len >= 2 ? buf[1] : 0;
    if (op != FTMS_OP_REQUEST_CONTROL && !g_ftms_control) {
        ftms_send_response(op, FTMS_RESULT_CONTROL_NOT_PERMITTED);
        return g_ftms_pending ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    size_t expected_len = 0;
    switch (op) {
    case FTMS_OP_REQUEST_CONTROL:
    case FTMS_OP_START_RESUME:
    case FTMS_OP_RESET:
        expected_len = 1;
        break;
    case FTMS_OP_STOP_PAUSE:
        expected_len = 2;
        break;
    case FTMS_OP_SET_TARGET_POWER:
    case FTMS_OP_SET_TARGET_SPEED:
        expected_len = 3;
        break;
    default:
        break;
    }
    if ((expected_len && len != expected_len)
            || (op == FTMS_OP_STOP_PAUSE && buf[1] != 1 && buf[1] != 2)) {
        ftms_send_response(op, FTMS_RESULT_INVALID_PARAM);
        return g_ftms_pending ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    ESP_LOGD(TAG, "FTMS控制点写入 op=0x%02x len=%d", op, len);
    switch (op) {
    case FTMS_OP_REQUEST_CONTROL:
        portENTER_CRITICAL(&s_indication_lock);
        g_ftms_owner = ch;
        g_ftms_control = true;
        portEXIT_CRITICAL(&s_indication_lock);
        ftms_send_response(FTMS_OP_REQUEST_CONTROL, FTMS_RESULT_SUCCESS);
        break;
    case FTMS_OP_SET_TARGET_POWER:
        portENTER_CRITICAL(&s_data_lock);
        bool real = s_output.real;
        portEXIT_CRITICAL(&s_data_lock);
        if (g_profile == SPORT_PROFILE_RUNNING || real) {
            ftms_send_response(op, real && g_profile == SPORT_PROFILE_CYCLING
                ? FTMS_RESULT_OPERATION_FAILED : FTMS_RESULT_NOT_SUPPORTED);
            break;
        }
        if (len < 3) {
            ftms_send_response(op, FTMS_RESULT_INVALID_PARAM);
            break;
        }
        {
            int16_t target_s16 = (int16_t)(buf[1] | ((uint16_t)buf[2] << 8));
            int target = (int)target_s16;
            ESP_LOGI(TAG, "ERG目标功率 %dW", target);
            if (target < POWER_MIN || target > POWER_MAX) {
                ftms_send_response(op, FTMS_RESULT_INVALID_PARAM);
            } else if (!g_erg_cb || !g_erg_cb(target)) {
                ftms_send_response(op, FTMS_RESULT_OPERATION_FAILED);
            } else {
                ftms_operation_accepted(op, 0);
            }
        }
        break;
    case FTMS_OP_SET_TARGET_SPEED:
        if (g_profile != SPORT_PROFILE_RUNNING) {
            ftms_send_response(op, FTMS_RESULT_NOT_SUPPORTED);
            break;
        }
        if (len < 3) {
            ftms_send_response(op, FTMS_RESULT_INVALID_PARAM);
            break;
        }
        {
            uint16_t target_speed = (uint16_t)(buf[1] | ((uint16_t)buf[2] << 8));
            if (target_speed < RUN_SPEED_X100_MIN || target_speed > RUN_SPEED_X100_MAX) {
                ftms_send_response(op, FTMS_RESULT_INVALID_PARAM);
            } else if (!g_run_control_cb
                       || !g_run_control_cb(BLE_RUN_CTRL_SPEED, (int)target_speed)) {
                ftms_send_response(op, FTMS_RESULT_OPERATION_FAILED);
            } else {
                ftms_operation_accepted(op, 0);
            }
        }
        break;
    case FTMS_OP_START_RESUME:
        if (g_profile == SPORT_PROFILE_RUNNING
                && (!g_run_control_cb || !g_run_control_cb(BLE_RUN_CTRL_START, 0))) {
            ftms_send_response(op, FTMS_RESULT_OPERATION_FAILED);
            break;
        }
        if (g_profile == SPORT_PROFILE_RUNNING) ftms_operation_accepted(FTMS_OP_START_RESUME, 0);
        else { ftms_send_status(FTMS_STATUS_STARTED_RESUMED_BY_USER, 0); ftms_send_response(op, FTMS_RESULT_SUCCESS); }
        break;
    case FTMS_OP_STOP_PAUSE:
        if (g_profile == SPORT_PROFILE_RUNNING
                && (!g_run_control_cb || !g_run_control_cb(BLE_RUN_CTRL_STOP, 0))) {
            ftms_send_response(op, FTMS_RESULT_OPERATION_FAILED);
            break;
        }
        if (g_profile != SPORT_PROFILE_RUNNING
                && (!g_erg_clear_cb || !g_erg_clear_cb(FTMS_OP_STOP_PAUSE))) {
            ftms_send_response(op, FTMS_RESULT_OPERATION_FAILED);
            break;
        }
        ftms_operation_accepted(FTMS_OP_STOP_PAUSE, buf[1]);
        break;
    case FTMS_OP_RESET:
        if (g_profile == SPORT_PROFILE_RUNNING
                && (!g_run_control_cb || !g_run_control_cb(BLE_RUN_CTRL_RESET, 0))) {
            ftms_send_response(op, FTMS_RESULT_OPERATION_FAILED);
            break;
        }
        if (g_profile != SPORT_PROFILE_RUNNING
                && (!g_erg_clear_cb || !g_erg_clear_cb(FTMS_OP_RESET))) {
            ftms_send_response(op, FTMS_RESULT_OPERATION_FAILED);
            break;
        }
        ftms_operation_accepted(FTMS_OP_RESET, 0);
        break;
    default:
        ftms_send_response(op, FTMS_RESULT_NOT_SUPPORTED);
        break;
    }
    return g_ftms_pending ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// ============================ DIS 读取（支持身份覆盖） ============================

static char s_default_serial[DEVICE_IDENTITY_TEXT_MAX];

static esp_err_t prepare_default_serial(void)
{
    if (s_default_serial[0]) return ESP_OK;
    uint8_t mac[DEVICE_SERIAL_MAC_LEN];
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) return err;
    int length = snprintf(s_default_serial, sizeof(s_default_serial), DEVICE_SERIAL_FORMAT,
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (length <= 0 || (size_t)length >= sizeof(s_default_serial)) {
        s_default_serial[0] = '\0';
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

/**
 * 以「覆盖值优先、否则内置值」返回某个 DIS 字符串字段。
 * ctxt：GATT 访问上下文。
 * field：身份字段。
 * fallback：未覆盖时返回的固件内置值。
 * 返回值：0 表示成功，否则返回 ATT 错误码。
 */
static int dis_read_field(struct ble_gatt_access_ctxt *ctxt, device_identity_field_t field,
                          const char *fallback)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
    char value[DEVICE_IDENTITY_TEXT_MAX];
    portENTER_CRITICAL(&g_identity_lock);
    const char *src = device_identity_effective(&g_identity, field, NULL);
    if (src) {
        strncpy(value, src, sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';
    }
    portEXIT_CRITICAL(&g_identity_lock);
    if (!src) {
        return chr_read_buf(ctxt, (const uint8_t *)fallback, strlen(fallback));
    }
    return chr_read_buf(ctxt, (const uint8_t *)value, strlen(value));
}

/* DIS 厂商名（0x2A29）：覆盖值或编译期常量。 */
static int dis_manuf_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    return dis_read_field(ctxt, DEVICE_IDENTITY_MANUFACTURER, DEVICE_MANUFACTURER);
}

/* DIS 型号（0x2A24）：覆盖值或编译期常量。 */
static int dis_model_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    return dis_read_field(ctxt, DEVICE_IDENTITY_MODEL, DEVICE_MODEL);
}

/* DIS 固件版本（0x2A26）：覆盖值或板型固件包名。 */
static int dis_fw_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    char fw_name[96];
    board_firmware_name(fw_name, sizeof(fw_name));
    return dis_read_field(ctxt, DEVICE_IDENTITY_FIRMWARE, fw_name);
}

/* DIS 序列号（0x2A25）：覆盖值或芯片出厂标识生成的稳定非空值。 */
static int dis_serial_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    return dis_read_field(ctxt, DEVICE_IDENTITY_SERIAL, s_default_serial);
}

/* DIS 硬件版本（0x2A27）：覆盖值或板型名称。 */
static int dis_hw_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    const board_desc_t *desc = board_get_desc();
    return dis_read_field(ctxt, DEVICE_IDENTITY_HARDWARE, (desc && desc->name) ? desc->name : DEVICE_MODEL);
}

/* DIS 软件版本（0x2A28）：覆盖值或固件语义版本号。 */
static int dis_sw_cb(uint16_t ch, uint16_t ah, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    return dis_read_field(ctxt, DEVICE_IDENTITY_SOFTWARE, FIRMWARE_VERSION);
}

/**
 * DIS 身份覆盖特征（7a5a0008-…）读写。
 * 读：返回当前已落盘覆盖值的线格式（空字段表示使用内置值）。
 * 写：校验线格式后交给应用层 mailbox 落盘；落盘成功后读值才会变化，
 *     客户端应回读确认（网页工具以回读一致作为写入成功依据）。
 * 特殊行为：host 任务内不写 NVS；应用层忙时返回 Write Not Permitted。
 */
static int config_dis_profile_access_cb(uint16_t ch, uint16_t ah,
                                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)ch; (void)ah; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        device_identity_t snapshot;
        portENTER_CRITICAL(&g_identity_lock);
        snapshot = g_identity;
        portEXIT_CRITICAL(&g_identity_lock);
        uint8_t payload[DEVICE_IDENTITY_WIRE_MAX];
        size_t len = device_identity_encode(&snapshot, payload, sizeof(payload));
        return len > 0 ? chr_read_buf(ctxt, payload, len) : BLE_ATT_ERR_UNLIKELY;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    if (!client_find(ch) || ble_config_service_busy()) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    uint8_t payload[DEVICE_IDENTITY_WIRE_MAX];
    uint16_t len = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, payload, sizeof(payload), &len);
    if (rc != 0 || len == 0) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    device_identity_t requested;
    if (!device_identity_decode(payload, len, &requested)) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    if (!g_identity_write_cb || !g_identity_write_cb(&requested)) {
        ESP_LOGW(TAG, "DIS 身份覆盖写入被拒绝：应用层忙");
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    ESP_LOGI(TAG, "收到 DIS 身份覆盖写入 len=%u，等待应用层落盘", (unsigned)len);
    return 0;
}

// ============================ GATT 服务表 ============================

static const struct ble_gatt_svc_def cycling_gatt_svcs[] = {
    {   // Cycling Power
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(POWER_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = BLE_UUID16_DECLARE(POWER_CHAR_UUID),
              .access_cb = cps_access_cb,
              .flags = BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_cps_val_handle },
            { .uuid = BLE_UUID16_DECLARE(POWER_FEATURE_CHAR_UUID),
              .access_cb = cps_feature_cb,
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
              .flags = BLE_GATT_CHR_F_NOTIFY,
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
              .flags = BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_csc_val_handle },
            { .uuid = BLE_UUID16_DECLARE(CSC_FEATURE_CHAR_UUID),
              .access_cb = csc_feature_cb,
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
            { .uuid = BLE_UUID16_DECLARE(FTMS_POWER_RANGE_CHAR_UUID),
              .access_cb = ftms_power_range_cb,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(FTMS_POWER_CHAR_UUID),
              .access_cb = ftms_bike_cb,
              .flags = BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_ftms_bike_handle },
            { .uuid = BLE_UUID16_DECLARE(FTMS_CONTROL_POINT_CHAR_UUID),
              .access_cb = ftms_cp_cb,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_WRITE_ENC,
              .val_handle = &g_ftms_cp_handle },
            { .uuid = BLE_UUID16_DECLARE(FTMS_STATUS_CHAR_UUID),
              .access_cb = ftms_status_cb,
              .flags = BLE_GATT_CHR_F_NOTIFY,
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
            { .uuid = BLE_UUID16_DECLARE(DIS_SERIAL_CHAR_UUID),
              .access_cb = dis_serial_cb, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(DIS_HARDWARE_CHAR_UUID),
              .access_cb = dis_hw_cb, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(DIS_SOFTWARE_CHAR_UUID),
              .access_cb = dis_sw_cb, .flags = BLE_GATT_CHR_F_READ },
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
              .flags = BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_rsc_val_handle },
            { .uuid = BLE_UUID16_DECLARE(RSC_FEATURE_CHAR_UUID),
              .access_cb = rsc_feature_cb,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(SENSOR_LOCATION_CHAR_UUID),
              .access_cb = sensor_location_cb,
              .flags = BLE_GATT_CHR_F_READ },
            {
                .uuid = BLE_UUID16_DECLARE(RSC_CONTROL_POINT_CHAR_UUID),
                .access_cb = rsc_cp_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE,
                .val_handle = &g_rsc_cp_handle,
            },
            { 0 }
        }
    },
    {   // Heart Rate
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(HR_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = BLE_UUID16_DECLARE(HR_CHAR_UUID),
              .access_cb = hr_access_cb,
              .flags = BLE_GATT_CHR_F_NOTIFY,
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
            { .uuid = BLE_UUID16_DECLARE(FTMS_SPEED_RANGE_CHAR_UUID),
              .access_cb = ftms_speed_range_cb,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(FTMS_TREADMILL_CHAR_UUID),
              .access_cb = ftms_treadmill_cb,
              .flags = BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_ftms_treadmill_handle },
            { .uuid = BLE_UUID16_DECLARE(FTMS_CONTROL_POINT_CHAR_UUID),
              .access_cb = ftms_cp_cb,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_WRITE_ENC,
              .val_handle = &g_ftms_cp_handle },
            { .uuid = BLE_UUID16_DECLARE(FTMS_STATUS_CHAR_UUID),
              .access_cb = ftms_status_cb,
              .flags = BLE_GATT_CHR_F_NOTIFY,
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
            { .uuid = BLE_UUID16_DECLARE(DIS_SERIAL_CHAR_UUID),
              .access_cb = dis_serial_cb, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(DIS_HARDWARE_CHAR_UUID),
              .access_cb = dis_hw_cb, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(DIS_SOFTWARE_CHAR_UUID),
              .access_cb = dis_sw_cb, .flags = BLE_GATT_CHR_F_READ },
            { 0 }
        }
    },
    { 0 }
};

// 按当前 g_broadcast_mask 动态构建 GATT 服务表：仅注册被启用的服务，
// 未启用的服务从 GATT 表中移除（例如只开心率时蓝牙只暴露 HRS，类似心率带）。
// 最大 5 个运动/DIS 服务 + 1 个配置服务 + 1 个终止符，预分配静态缓冲避免运行时 malloc。
static struct ble_gatt_svc_def g_active_svcs[7];

static void build_active_gatt_svcs(void)
{
    int idx = 0;
    const bool power_on = (g_broadcast_mask & BROADCAST_FIELD_POWER) != 0;
    const bool cadence_on = (g_broadcast_mask & BROADCAST_FIELD_CADENCE) != 0;
    const bool hr_on = (g_broadcast_mask & BROADCAST_FIELD_HEARTRATE) != 0;
    // 兼容模式：踏频由 CPS 曲柄字段承载，不注册独立 CSC，避免码表按 CSC 优先分类。
    const bool csc_on = cadence_on
        && !(power_on && (g_broadcast_mask & BROADCAST_FIELD_CADENCE_WITH_POWER));

    if (g_profile == SPORT_PROFILE_RUNNING) {
        if (power_on || cadence_on) g_active_svcs[idx++] = running_gatt_svcs[0]; // RSC
        if (hr_on)                  g_active_svcs[idx++] = running_gatt_svcs[1]; // HRS
        if (power_on || cadence_on) g_active_svcs[idx++] = running_gatt_svcs[2]; // FTMS
        g_active_svcs[idx++] = running_gatt_svcs[3]; // DIS（始终）
    } else {
        if (power_on)               g_active_svcs[idx++] = cycling_gatt_svcs[0]; // CPS
        if (hr_on)                  g_active_svcs[idx++] = cycling_gatt_svcs[1]; // HRS
        if (csc_on)                 g_active_svcs[idx++] = cycling_gatt_svcs[2]; // CSC
        if ((power_on || cadence_on) && !g_bridge_mode) g_active_svcs[idx++] = cycling_gatt_svcs[3]; // FTMS
        g_active_svcs[idx++] = cycling_gatt_svcs[4]; // DIS（始终）
    }
    g_active_svcs[idx++] = *ble_config_service_definition();
    memset(&g_active_svcs[idx], 0, sizeof(g_active_svcs[idx])); // 终止符
}

// 重置所有特征值句柄，确保未注册服务的通知被 notify 守卫跳过。
static void reset_svc_handles(void)
{
    indication_reset();
    g_ftms_control = false;
    g_ftms_pending = false;
    g_cps_val_handle = 0;
    g_hr_val_handle = 0;
    g_csc_val_handle = 0;
    g_rsc_val_handle = 0;
    g_ftms_bike_handle = 0;
    g_ftms_treadmill_handle = 0;
    g_ftms_cp_handle = 0;
    g_ftms_status_handle = 0;
    g_rsc_cp_handle = 0;
}

// ============================ 数据打包 ============================

/**
 * 打包当前功率、心率和踏频到各服务通知缓冲区。
 * 根据当前运行时快照生成 CPS、HRS、CSC 和 FTMS 的二进制负载。
 * 无参数。
 * 返回值：无。
 * 特殊行为：会推进曲柄累计圈数和时间戳。
 */
static ble_output_snapshot_t output_snapshot(void)
{
    portENTER_CRITICAL(&s_data_lock);
    ble_output_snapshot_t value = s_output;
    value.mask = g_broadcast_mask;
    portEXIT_CRITICAL(&s_data_lock);
    return value;
}

// 调用者用 s_data_lock 保护静态编码结果；编码器只做有界纯 C 运算。
static void pack_all(const ble_output_snapshot_t *data)
{
    bridge_snapshot_t sample = data->bridge;
    if (data->real) {
        bool crank = sample.valid && sample.has_crank;
        s_bridge_csc_valid = crank;
        g_cps_len = ble_payload_encode_cps_bridge(g_cps_buf, sizeof(g_cps_buf), crank,
            (int16_t)sample.power_w, sample.crank_revolutions, sample.crank_event_time_1024);
        ble_payload_encode_hrs(g_hr_buf, sizeof(g_hr_buf), data->heartrate);
        ble_payload_encode_csc(g_csc_buf, sizeof(g_csc_buf),
            sample.crank_revolutions, sample.crank_event_time_1024);
        g_ftms_bike_len = 0;
        return;
    }
    g_cps_len = BLE_PAYLOAD_CPS_LEN;
    s_bridge_csc_valid = true;
    uint16_t power = data->power;
    uint16_t cadence = data->cadence;
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
    (void)ble_payload_encode_cps(g_cps_buf, sizeof(g_cps_buf),
                                 (int16_t)power, rev, time_1024);
    (void)ble_payload_encode_hrs(g_hr_buf, sizeof(g_hr_buf), data->heartrate);
    (void)ble_payload_encode_csc(g_csc_buf, sizeof(g_csc_buf), rev, time_1024);

    bool cadence_enabled = (data->mask & BROADCAST_FIELD_CADENCE) != 0;
    bool power_enabled = (data->mask & BROADCAST_FIELD_POWER) != 0;
    g_ftms_bike_len = ble_payload_encode_ftms_bike(
        g_ftms_bike_buf, sizeof(g_ftms_bike_buf), cadence_enabled, power_enabled,
        (uint16_t)(cadence * 30u), (uint16_t)(cadence * 2u), (int16_t)power);
}

static void pack_running(const ble_output_snapshot_t *data)
{
    uint16_t speed = data->running.rsc_speed_1_256_mps;
    uint16_t stride = data->running.stride_cm;
    uint32_t distance = data->running.total_distance_m;
    uint32_t elapsed = data->running.elapsed_time_s;
    bool running = data->running.running;

    (void)ble_payload_encode_hrs(g_hr_buf, sizeof(g_hr_buf), data->heartrate);
    uint32_t distance_dm = data->running.total_distance_dm;
    (void)ble_payload_encode_rsc(g_rsc_buf, sizeof(g_rsc_buf), running,
                                 speed, (uint8_t)((data->running.cadence_spm + 1) / 2), stride, distance_dm);

    uint16_t ftms_speed = (uint16_t)(((uint32_t)speed * 360u + 128u) / 256u);
    uint16_t elapsed16 = elapsed > 0xFFFFu ? 0xFFFFu : (uint16_t)elapsed;
    g_ftms_treadmill_len = ble_payload_encode_ftms_treadmill(
        g_ftms_treadmill_buf, sizeof(g_ftms_treadmill_buf),
        ftms_speed, distance, data->heartrate, elapsed16);
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
static void notify_client_nolock(uint16_t connection, uint16_t handle, const uint8_t *buf, size_t len)
{
    if (g_stopping || !g_ready || !handle || !len || !notification_subscribed(connection, handle)) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
    if (om) ble_telemetry_service_tx(ble_gatts_notify_custom(connection, handle, om), false);
    else ble_telemetry_service_tx(0, true);
}

static void notify_one_nolock(uint16_t handle, const uint8_t *buf, size_t len)
{
    if (g_stopping || !g_ready || !handle || !len) return;
    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i) {
        uint16_t connection = s_clients[i].connection;
        if (!notification_subscribed(connection, handle)) continue;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
        if (om) ble_telemetry_service_tx(ble_gatts_notify_custom(connection, handle, om), false);
        else ble_telemetry_service_tx(0, true);
    }
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

static void indication_reset(void)
{
    portENTER_CRITICAL(&s_indication_lock);
    ftms_release_owner(g_ftms_owner);
    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i) {
        s_clients[i].head = s_clients[i].count = 0;
        s_clients[i].active = s_clients[i].failed = false;
        s_clients[i].ftms_subscribed = s_clients[i].rsc_subscribed = false;
    }
    g_ftms_response_token = g_rsc_response_token = 0;
    g_ftms_pending = g_rsc_pending = false;
    g_ftms_owner = g_ftms_connection = g_rsc_connection = BLE_HS_CONN_HANDLE_NONE;
    subscriptions_reset();
    portEXIT_CRITICAL(&s_indication_lock);
}

static uint32_t indication_reserve(uint16_t connection, uint16_t handle)
{
    uint32_t token = 0;
    portENTER_CRITICAL(&s_indication_lock);
    ble_client_t *client = client_find(connection);
    if (!g_stopping && g_ready && handle && client && !client->failed
            && client->count < BLE_INDICATION_QUEUE_SIZE) {
        if (++s_indication_sequence == 0) ++s_indication_sequence;
        token = s_indication_sequence;
        uint8_t tail = (client->head + client->count++) % BLE_INDICATION_QUEUE_SIZE;
        client->indications[tail] = (ble_indication_entry_t){
            .token = token, .generation = client->generation,
            .connection = connection, .handle = handle,
        };
    }
    portEXIT_CRITICAL(&s_indication_lock);
    return token;
}

static void indication_subscription_revoked(uint16_t connection, uint16_t handle)
{
    portENTER_CRITICAL(&s_indication_lock);
    ble_client_t *client = client_find(connection);
    if (!client) { portEXIT_CRITICAL(&s_indication_lock); return; }
    bool in_flight = client->active && client->count
        && client->indications[client->head].handle == handle;
    if (handle == g_ftms_cp_handle) {
        ftms_release_owner(connection);
        if (g_ftms_connection == connection) {
            g_ftms_response_token = 0;
            g_ftms_pending = in_flight;
            g_ftms_control = false;
        }
    } else if (handle == g_rsc_cp_handle && g_rsc_connection == connection) {
        g_rsc_response_token = 0;
        g_rsc_pending = in_flight;
    }
    uint8_t kept = 0;
    for (uint8_t i = 0; i < client->count; ++i) {
        uint8_t index = (client->head + i) % BLE_INDICATION_QUEUE_SIZE;
        if (client->indications[index].handle == handle && !(i == 0 && client->active)) continue;
        uint8_t target = (client->head + kept++) % BLE_INDICATION_QUEUE_SIZE;
        client->indications[target] = client->indications[index];
    }
    client->count = kept;
    portEXIT_CRITICAL(&s_indication_lock);
}

static void indication_complete(uint16_t connection, uint16_t handle, int status)
{
    portENTER_CRITICAL(&s_indication_lock);
    ble_client_t *client = client_find(connection);
    if (status && client && client->active && client->count
            && client->indications[client->head].handle == handle) {
        if (status == BLE_HS_EBUSY) {
            client->active = false;
            portEXIT_CRITICAL(&s_indication_lock);
            return;
        }
        uint32_t token = client->indications[client->head].token;
        if (handle == g_ftms_cp_handle && g_ftms_connection == connection
                && (token == g_ftms_response_token || !g_ftms_response_token)) {
            g_ftms_pending = false;
            if (status != BLE_HS_EDONE) {
                ftms_release_owner(connection);
            }
        }
        if (handle == g_rsc_cp_handle && g_rsc_connection == connection
                && (token == g_rsc_response_token || !g_rsc_response_token)) g_rsc_pending = false;
        if (status == BLE_HS_EDONE) {
            client->head = (client->head + 1) % BLE_INDICATION_QUEUE_SIZE;
            --client->count;
            client->active = false;
        } else client->failed = true;
    }
    portEXIT_CRITICAL(&s_indication_lock);
}

static void indication_pump_nolock(void)
{
    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i) {
        ble_indication_entry_t entry = {0};
        bool failed;
        uint16_t connection;
        ble_client_t *client = &s_clients[i];
        portENTER_CRITICAL(&s_indication_lock);
        connection = client->connection;
        failed = client->failed;
        if (!failed && !client->active && client->count
                && client->indications[client->head].ready && !g_stopping && g_ready
                && connection != BLE_HS_CONN_HANDLE_NONE
                && client->indications[client->head].generation == client->generation) {
            entry = client->indications[client->head];
        }
        portEXIT_CRITICAL(&s_indication_lock);
        if (failed) {
            if (connection != BLE_HS_CONN_HANDLE_NONE && !g_stopping)
                ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
            continue;
        }
        if (!entry.token) continue;
        ble_hs_lock();
        struct ble_hs_conn *host_connection = ble_hs_conn_find(entry.connection);
        bool busy = host_connection && host_connection->bhc_gatt_svr.indicate_val_handle != 0;
        ble_hs_unlock();
        if (!host_connection || busy) continue;
        portENTER_CRITICAL(&s_indication_lock);
        bool valid = client->connection == entry.connection && client->generation == entry.generation
            && client->count && !client->active && !client->failed
            && client->indications[client->head].token == entry.token;
        if (valid) client->active = true;
        portEXIT_CRITICAL(&s_indication_lock);
        if (!valid) continue;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(entry.payload, entry.length);
        int rc = om ? ble_gatts_indicate_custom(entry.connection, entry.handle, om) : BLE_HS_ENOMEM;
        if (rc) {
            portENTER_CRITICAL(&s_indication_lock);
            if (client->count && client->active && client->connection == entry.connection
                    && client->indications[client->head].token == entry.token) {
                if (rc == BLE_HS_EBUSY) client->active = false;
                else {
                    client->failed = true;
                    if (g_ftms_connection == entry.connection && entry.handle == g_ftms_cp_handle) {
                        g_ftms_pending = false;
                        ftms_release_owner(entry.connection);
                    }
                }
            }
            portEXIT_CRITICAL(&s_indication_lock);
        }
    }
}

static bool indication_submit(uint32_t token, const uint8_t *buf, size_t len)
{
    bool accepted = false;
    if (!token || !buf || !len || len > BLE_CONFIG_CONTROL_RESPONSE_LEN) return false;
    portENTER_CRITICAL(&s_indication_lock);
    for (size_t c = 0; c < BLE_DOWNSTREAM_MAX && !accepted; ++c) {
        ble_client_t *client = &s_clients[c];
        for (uint8_t i = 0; i < client->count && !client->failed; ++i) {
            ble_indication_entry_t *entry = &client->indications[(client->head + i) % BLE_INDICATION_QUEUE_SIZE];
            if (entry->token == token && !entry->ready && entry->connection == client->connection
                    && entry->generation == client->generation) {
                memcpy(entry->payload, buf, len);
                entry->length = (uint8_t)len;
                entry->ready = accepted = true;
                break;
            }
        }
    }
    portEXIT_CRITICAL(&s_indication_lock);
    if (accepted) notify_schedule();
    return accepted;
}

static bool indicate_try(uint16_t handle, const uint8_t *buf, size_t len)
{
    if (handle != g_ftms_cp_handle) return false;
    return indication_submit(g_ftms_response_token, buf, len);
}

static void indicate_one(uint32_t token, const uint8_t *buf, size_t len)
{
    (void)indication_submit(token, buf, len);
}

/**
 * BLE 周期通知定时器回调。
 * 在有连接时打包当前数据并通知 CPS、HRS、CSC、FTMS 四个动态特征。
 * arg：未使用的定时器参数。
 * 返回值：无。
 * 特殊行为：无连接时直接返回，不做任何打包和发送。
 */
static void notify_schedule(void)
{
    if (!g_notify_lock || xSemaphoreTake(g_notify_lock, 0) != pdTRUE) return;
    if (g_ready && !g_stopping)
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_notify_event);
    xSemaphoreGive(g_notify_lock);
}

static void notify_tick(void *arg)
{
    (void)arg;
    s_data_notify_pending = true;
    notify_schedule();
}

static void host_notify_event(struct ble_npl_event *event)
{
    (void)event;
    // 持锁后再检查标志并访问协议栈：stop 会先拿同一把锁才 deinit NimBLE，
    // 保证本回调要么完整在 deinit 前执行、要么 stop 等待本回调结束，消除 UAF 窗口。
    if (!g_notify_lock || xSemaphoreTake(g_notify_lock, 0) != pdTRUE) {
        ble_telemetry_service_lock_skipped();
        return;
    }
    if (g_stopping || !g_ready || ble_bikepower_conn_count() == 0) {
        xSemaphoreGive(g_notify_lock);
        return;
    }
    indication_pump_nolock();
    if (!atomic_exchange(&s_data_notify_pending, false)) {
        xSemaphoreGive(g_notify_lock);
        return;
    }
    ble_output_snapshot_t data = output_snapshot();
    uint8_t mask = data.mask;
    portENTER_CRITICAL(&s_data_lock);
    if (g_profile == SPORT_PROFILE_RUNNING) pack_running(&data);
    else pack_all(&data);
    portEXIT_CRITICAL(&s_data_lock);
    if (g_profile == SPORT_PROFILE_RUNNING) {
        if (mask & (BROADCAST_FIELD_POWER | BROADCAST_FIELD_CADENCE)) {
            notify_one_nolock(g_rsc_val_handle, g_rsc_buf, sizeof(g_rsc_buf));
        }
        if (mask & BROADCAST_FIELD_HEARTRATE) notify_one_nolock(g_hr_val_handle, g_hr_buf, sizeof(g_hr_buf));
        if (mask & (BROADCAST_FIELD_POWER | BROADCAST_FIELD_CADENCE | BROADCAST_FIELD_HEARTRATE)) {
            notify_one_nolock(g_ftms_treadmill_handle, g_ftms_treadmill_buf, g_ftms_treadmill_len);
        }
    } else {
        if (mask & BROADCAST_FIELD_POWER) notify_one_nolock(g_cps_val_handle, g_cps_buf, g_cps_len);
        if (mask & BROADCAST_FIELD_HEARTRATE) notify_one_nolock(g_hr_val_handle, g_hr_buf, sizeof(g_hr_buf));
        if ((mask & BROADCAST_FIELD_CADENCE) && s_bridge_csc_valid) notify_one_nolock(g_csc_val_handle, g_csc_buf, sizeof(g_csc_buf));
        if (mask & (BROADCAST_FIELD_POWER | BROADCAST_FIELD_CADENCE)) notify_one_nolock(g_ftms_bike_handle, g_ftms_bike_buf, g_ftms_bike_len);
    }
    ble_telemetry_service_notify(notify_client_nolock);
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
 * 在 Host 确认真实外设连接后发布应用连接状态。
 * 重复确认不清空已登记的订阅和控制事务。
 */
static void restore_bond_encryption(uint16_t connection, const struct ble_gap_conn_desc *desc)
{
    if (s_security_store_failed || desc->sec_state.encrypted) return;
    struct ble_store_key_sec key = { .peer_addr = desc->peer_id_addr };
    struct ble_store_value_sec bond = {0};
    if (ble_store_read_peer_sec(&key, &bond) != 0 || !bond.ltk_present
            || bond.key_size < BLE_SM_PAIR_KEY_SZ_MIN || bond.key_size > BLE_SM_PAIR_KEY_SZ_MAX) return;
    int rc = ble_gap_security_initiate(connection);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(TAG, "已绑定下游恢复加密请求失败 handle=%u rc=%d", connection, rc);
    }
}

static bool connection_accept(uint16_t connection)
{
    if (g_stopping || !g_ready || connection == BLE_HS_CONN_HANDLE_NONE) return false;
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(connection, &desc) != 0 || desc.role != BLE_GAP_ROLE_SLAVE) return false;
    if (client_find(connection)) return true;
    if (s_rebuild_pending) return false;
    portENTER_CRITICAL(&s_indication_lock);
    ble_client_t *client = NULL;
    if (!s_rebuild_pending) for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i) {
        if (s_clients[i].connection == BLE_HS_CONN_HANDLE_NONE) { client = &s_clients[i]; break; }
    }
    if (client) {
        ++client->generation;
        client->head = client->count = 0;
        client->active = client->failed = false;
        client->ftms_subscribed = client->rsc_subscribed = false;
        for (size_t i = 0; i < BLE_NOTIFY_SUBSCRIPTION_MAX; ++i) client->subscriptions[i] = 0;
        client->connection = connection;
    }
    portEXIT_CRITICAL(&s_indication_lock);
    if (!client) return false;
    ble_config_service_connect(connection);
    ble_telemetry_service_connect(connection);
    restore_bond_encryption(connection, &desc);
    ESP_LOGI(TAG, "下游已连接 handle=%u count=%d", connection, ble_bikepower_conn_count());
    return true;
}

static void connection_remove(uint16_t connection)
{
    portENTER_CRITICAL(&s_indication_lock);
    ble_client_t *client = client_find(connection);
    if (client) {
        client->connection = BLE_HS_CONN_HANDLE_NONE;
        ++client->generation;
        client->head = client->count = 0;
        client->active = client->failed = false;
        client->ftms_subscribed = client->rsc_subscribed = false;
        for (size_t i = 0; i < BLE_NOTIFY_SUBSCRIPTION_MAX; ++i) client->subscriptions[i] = 0;
        ftms_release_owner(connection);
        if (g_ftms_connection == connection) { g_ftms_response_token = 0; g_ftms_pending = false; }
        if (g_rsc_connection == connection) { g_rsc_response_token = 0; g_rsc_pending = false; }
    }
    portEXIT_CRITICAL(&s_indication_lock);
    if (client) {
        ble_config_service_disconnect(connection);
        ble_telemetry_service_disconnect(connection);
    }
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
    case BLE_GAP_EVENT_CONNECT: {
        uint16_t connection = event->connect.conn_handle;
        int status = event->connect.status;
        bool recoverable = status == 0 || status == BLE_ERR_UNSUPP_REM_FEATURE;
        if (recoverable && connection_accept(connection)) {
            if (status != 0) ESP_LOGW(TAG, "远端特征状态=%d，Host连接仍有效 handle=%u", status, connection);
        } else if (recoverable && connection != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        }
        if (!g_stopping) {
            rc = start_advertising();
            if (rc != 0) ESP_LOGW(TAG, "连接事件后广播暂未恢复 rc=%d", rc);
        }
        return 0;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        connection_remove(event->disconnect.conn.conn_handle);
        rc = start_advertising();
        if (rc != 0 && !g_stopping) ESP_LOGW(TAG, "断开后广播暂未恢复 rc=%d", rc);
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        rc = start_advertising();
        if (rc != 0 && !g_stopping) ESP_LOGW(TAG, "广播完成后暂未恢复 rc=%d", rc);
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
#if CONFIG_BIKEPOWER_DIAGNOSTIC_LOGS
        ESP_LOGW(TAG, "DIAG: subscribe handle=%u cur_notify=%u cur_indicate=%u reason=%u",
                 event->subscribe.attr_handle, event->subscribe.cur_notify,
                 event->subscribe.cur_indicate, event->subscribe.reason);
#endif
        if (!client_find(event->subscribe.conn_handle)
                && (event->subscribe.cur_notify || event->subscribe.cur_indicate)) {
            (void)connection_accept(event->subscribe.conn_handle);
        }
        if (g_stopping || !g_ready) return 0;
        ble_client_t *sub_client = client_find(event->subscribe.conn_handle);
        if (!sub_client) return 0;
        subscription_update(event->subscribe.conn_handle, event->subscribe.attr_handle, event->subscribe.cur_notify != 0);
        ble_telemetry_service_subscribe(event->subscribe.conn_handle, event->subscribe.attr_handle, event->subscribe.cur_notify != 0);
        if (event->subscribe.attr_handle == g_rsc_cp_handle) {
            sub_client->rsc_subscribed = event->subscribe.cur_indicate != 0;
            if (!sub_client->rsc_subscribed) indication_subscription_revoked(event->subscribe.conn_handle, g_rsc_cp_handle);
        }
        if (event->subscribe.attr_handle == g_ftms_cp_handle) {
            sub_client->ftms_subscribed = event->subscribe.cur_indicate != 0;
            if (!sub_client->ftms_subscribed) indication_subscription_revoked(event->subscribe.conn_handle, g_ftms_cp_handle);
        }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_TX:
#if CONFIG_BIKEPOWER_DIAGNOSTIC_LOGS
        /* 诊断：打印所有 indication TX/ACK 状态（不硬编码 SC 句柄） */
        if (event->notify_tx.indication) {
            ESP_LOGW(TAG, "DIAG: notify_tx handle=%u indication=%u status=%d",
                     event->notify_tx.attr_handle, event->notify_tx.indication,
                     event->notify_tx.status);
        }
#endif
        if (client_find(event->notify_tx.conn_handle) && event->notify_tx.indication) {
            indication_complete(event->notify_tx.conn_handle,
                                event->notify_tx.attr_handle, event->notify_tx.status);
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
#if CONFIG_BIKEPOWER_DIAGNOSTIC_LOGS
        {
            /* NimBLE enc_change 事件只有 status/conn_handle，encrypted 需查连接 */
            struct ble_gap_conn_desc desc;
            int enc_rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
            uint8_t encrypted = (enc_rc == 0) ? desc.sec_state.encrypted : 0;
            ESP_LOGW(TAG, "DIAG: enc_change conn=%u status=%d encrypted=%d",
                     event->enc_change.conn_handle, event->enc_change.status, encrypted);
        }
#endif
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
    if (g_stopping || s_rebuild_pending || ble_bikepower_conn_count() >= BLE_DOWNSTREAM_MAX) return 0;
    if (ble_gap_adv_active()) return 0;
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;
    char device_name[DEVICE_NAME_MAX];
    portENTER_CRITICAL(&s_data_lock);
    memcpy(device_name, g_device_name, sizeof(device_name));
    portEXIT_CRITICAL(&s_data_lock);

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // 广播包 UUID 仅包含当前已注册的服务，避免声明不存在的服务。
    const bool power_on = (g_broadcast_mask & BROADCAST_FIELD_POWER) != 0;
    const bool cadence_on = (g_broadcast_mask & BROADCAST_FIELD_CADENCE) != 0;
    const bool hr_on = (g_broadcast_mask & BROADCAST_FIELD_HEARTRATE) != 0;
    // 与 GATT 注册保持一致：兼容模式下 CSC 不出现，踏频仍随 CPS/FTMS 数据输出。
    ble_uuid16_t adv_uuids[3];
    const bool csc_on = cadence_on
        && !(power_on && (g_broadcast_mask & BROADCAST_FIELD_CADENCE_WITH_POWER));
    int adv_uuid_count = 0;
    if (g_profile == SPORT_PROFILE_RUNNING) {
        if (power_on || cadence_on) adv_uuids[adv_uuid_count++] = (ble_uuid16_t)BLE_UUID16_INIT(RSC_SERVICE_UUID);
        if (power_on || cadence_on) adv_uuids[adv_uuid_count++] = (ble_uuid16_t)BLE_UUID16_INIT(FTMS_SERVICE_UUID);
    } else {
        if (power_on)               adv_uuids[adv_uuid_count++] = (ble_uuid16_t)BLE_UUID16_INIT(POWER_SERVICE_UUID);
        if (csc_on)                 adv_uuids[adv_uuid_count++] = (ble_uuid16_t)BLE_UUID16_INIT(CSC_SERVICE_UUID);
        if ((power_on || cadence_on) && !g_bridge_mode) adv_uuids[adv_uuid_count++] = (ble_uuid16_t)BLE_UUID16_INIT(FTMS_SERVICE_UUID);
    }
    static ble_uuid16_t scan_rsp_uuids[] = {
        BLE_UUID16_INIT(HR_SERVICE_UUID),
    };
    // 广播包空间有限：主广播放 App/码表按设备类型过滤时最关键的服务，完整能力由连接后 GATT 发现获取。
    fields.uuids16 = (adv_uuid_count > 0) ? adv_uuids : NULL;
    fields.num_uuids16 = adv_uuid_count;
    fields.uuids16_is_complete = 0;
    uint8_t ftms_service_data[5] = { 0x26, 0x18, 0x01, 0x00, 0x00 };
    if ((power_on || cadence_on) && (g_profile == SPORT_PROFILE_RUNNING || !g_bridge_mode)) {
        ftms_service_data[3] = g_profile == SPORT_PROFILE_RUNNING ? 0x01 : 0x20;
        fields.svc_data_uuid16 = ftms_service_data;
        fields.svc_data_uuid16_len = sizeof(ftms_service_data);
    }
    // 31 字节预算：flags(3) + 部分 UUID 列表(最多8) + FTMS Service Data(7)。
    // 名字含 AD 头最多13字节，完整名放扫描响应，避免广播设置因 EMSGSIZE 失败。
    size_t name_len = strlen(device_name);
    fields.name = (uint8_t *)device_name;
    if (name_len > BLE_ADV_NAME_MAX_LEN) {
        fields.name_len = BLE_ADV_NAME_MAX_LEN;
        while (fields.name_len && (device_name[fields.name_len] & 0xc0) == 0x80) {
            fields.name_len--;
        }
        fields.name_is_complete = 0;
    } else {
        fields.name_len = name_len;
        fields.name_is_complete = 1;
    }

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) { ESP_LOGE(TAG, "设置广播字段失败 rc=%d", rc); return rc; }

    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    if (hr_on) {
        rsp_fields.uuids16 = scan_rsp_uuids;
        rsp_fields.num_uuids16 = 1;
        rsp_fields.uuids16_is_complete = 0;
    }
    rsp_fields.name = (uint8_t *)device_name;
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
             device_name, BLE_ADVERTISING_INTERVAL_MS);
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
    esp_err_t store_err = security_store_activate();
    if (store_err != ESP_OK) {
        mark_start_failed("同步后绑定存储激活失败", store_err);
        return;
    }
#if CONFIG_BIKEPOWER_DIAGNOSTIC_LOGS
    ESP_LOGW(TAG, "DIAG: store_activate err=%d", store_err);
#endif
    bool layout_changed = false;
    esp_err_t cache_err = ble_bond_cache_sync(&layout_changed);
    if (cache_err != ESP_OK) {
        s_security_store_failed = true;
        mark_start_failed("订阅缓存布局同步失败", cache_err);
        return;
    }
#if CONFIG_BIKEPOWER_DIAGNOSTIC_LOGS
    ESP_LOGW(TAG, "DIAG: bond_cache_sync err=%d layout_changed=%d", cache_err, layout_changed);
#endif
    if (layout_changed) {
#if CONFIG_BIKEPOWER_DIAGNOSTIC_LOGS
        ESP_LOGW(TAG, "DIAG: 触发 ble_svc_gatt_changed SC indication");
#endif
        ble_svc_gatt_changed(BLE_GATT_CHANGED_START_HANDLE, BLE_GATT_CHANGED_END_HANDLE);
    }
    rc = start_advertising();
    if (rc != 0) {
        mark_start_failed("启动广播失败", rc);
        return;
    }
    // 广播成功启动后才标记为就绪状态
    g_ready = true;
    ble_telemetry_service_sync();
    g_start_failed = false;
    ESP_LOGI(TAG, "BLE 就绪，profile=%s",
             g_profile == SPORT_PROFILE_RUNNING ? "RSC+HRS+FTMS+DIS" : g_bridge_mode ? "CPS+HRS+CSC+DIS" : "CPS+HRS+CSC+FTMS+DIS");
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
    g_ready = false;
    ble_power_bridge_host_reset();
    ESP_LOGE(TAG, "BLE 协议栈复位 reason=%d", reason);
    // 复位后旧连接句柄已失效且不会有断开事件，清理以免 conn_count 误报已连接、
    // notify_tick 持续向失效句柄发通知。
    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i) connection_remove(s_clients[i].connection);
    ble_telemetry_service_disconnect(BLE_HS_CONN_HANDLE_NONE);
    indication_reset();
    s_rebuild_pending = false;
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
    portENTER_CRITICAL(&s_data_lock);
    s_output.real = false;
    s_output.power = power_w;
    s_output.heartrate = heartrate_bpm;
    s_output.cadence = cadence_rpm;
    portEXIT_CRITICAL(&s_data_lock);
}

void ble_bikepower_set_bridge_data(const bridge_snapshot_t *snapshot, uint8_t heartrate_bpm)
{
    if (!snapshot) return;
    portENTER_CRITICAL(&s_data_lock);
    s_output.real = true;
    s_output.bridge = *snapshot;
    s_output.heartrate = heartrate_bpm;
    portEXIT_CRITICAL(&s_data_lock);
}

void ble_bikepower_set_broadcast_mask(int mask)
{
    int normalized = mask & BROADCAST_FIELD_VALID;
    if ((normalized & BROADCAST_FIELD_ALL) == 0) normalized = BROADCAST_MASK_DEFAULT;
    g_broadcast_mask = (uint8_t)normalized;
}

void ble_bikepower_set_bridge_mode(bool real)
{
    g_bridge_mode = real;
}

void ble_bikepower_set_profile(sport_profile_t profile)
{
    if (profile < 0 || profile >= SPORT_PROFILE_COUNT) return;
    g_profile = profile;
}

void ble_bikepower_set_running_data(running_snapshot_t snapshot, uint8_t heartrate_bpm)
{
    portENTER_CRITICAL(&s_data_lock);
    s_output.running = snapshot;
    s_output.heartrate = heartrate_bpm;
    portEXIT_CRITICAL(&s_data_lock);
}

/**
 * 注册 ERG 目标功率回调。
 * 保存应用层提供的 FTMS ERG 回调。
 * cb：回调函数指针，可为 NULL。
 * 返回值：无。
 * 特殊行为：新的回调会直接替换旧回调。
 */
void ble_bikepower_enable_async_control(ble_rsc_distance_cb_t cb)
{
    g_control_async = true;
    g_rsc_distance_cb = cb;
}

uint32_t ble_bikepower_control_ticket(bool rsc)
{
    return rsc ? g_rsc_response_token : g_ftms_response_token;
}

uint32_t ble_bikepower_control_generation(void)
{
    return s_control_generation;
}

bool ble_bikepower_control_current(uint32_t ticket, bool rsc)
{
    uint16_t connection = rsc ? g_rsc_connection : g_ftms_connection;
    return ticket && ticket == ble_bikepower_control_ticket(rsc) && g_ready && !g_stopping
        && client_indicates(connection, rsc ? g_rsc_cp_handle : g_ftms_cp_handle)
        && (rsc ? g_rsc_pending : g_ftms_pending);
}

void ble_bikepower_control_complete(uint32_t ticket, bool rsc, bool success)
{
    if (!ble_bikepower_control_current(ticket, rsc)) return;
    if (rsc) {
        uint8_t response[] = {RSC_OP_RESPONSE_CODE, RSC_OP_SET_CUMULATIVE_VALUE,
            success ? RSC_RESULT_SUCCESS : RSC_RESULT_OPERATION_FAILED};
        (void)indication_submit(ticket, response, sizeof(response));
    } else {
        if (success) {
            if (g_control_op == FTMS_OP_RESET) { ftms_release_owner(g_ftms_connection); ftms_send_status(FTMS_STATUS_RESET, 0); }
            else if (g_control_op == FTMS_OP_START_RESUME) ftms_send_status(FTMS_STATUS_STARTED_RESUMED_BY_USER, 0);
            else if (g_control_op == FTMS_OP_STOP_PAUSE) ftms_send_status(FTMS_STATUS_STOPPED_PAUSED_BY_USER, g_control_parameter);
            ftms_send_response(g_control_op, FTMS_RESULT_SUCCESS);
        } else ftms_send_response(g_control_op, FTMS_RESULT_OPERATION_FAILED);
    }
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

void ble_bikepower_set_config_commit_callback(ble_config_commit_cb_t cb)
{
    ble_config_service_set_commit_callback(cb);
}

void ble_bikepower_set_identity_write_callback(ble_identity_write_cb_t cb)
{
    g_identity_write_cb = cb;
}

void ble_bikepower_get_identity(device_identity_t *identity)
{
    if (!identity) return;
    portENTER_CRITICAL(&g_identity_lock);
    *identity = g_identity;
    portEXIT_CRITICAL(&g_identity_lock);
}

void ble_bikepower_set_identity(const device_identity_t *identity)
{
    device_identity_t next;
    if (identity) next = *identity;
    else device_identity_defaults(&next);
    portENTER_CRITICAL(&g_identity_lock);
    g_identity = next;
    portEXIT_CRITICAL(&g_identity_lock);
}

void ble_bikepower_set_config_snapshot(const power_engine_t *engine)
{
    ble_config_service_set_snapshot(engine);
}

bool ble_bikepower_config_revision_matches(uint32_t revision)
{
    return ble_config_service_revision_matches(revision);
}

void ble_bikepower_config_commit_result(uint32_t transaction_id, uint8_t result,
                                        uint8_t restart_flags,
                                        const config_data_t *applied_config)
{
    ble_config_service_commit_result(transaction_id, result, restart_flags, applied_config);
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
    portENTER_CRITICAL(&s_data_lock);
    strncpy(g_device_name, name, sizeof(g_device_name) - 1);
    g_device_name[sizeof(g_device_name) - 1] = '\0';
    portEXIT_CRITICAL(&s_data_lock);
}

void ble_bikepower_refresh_device_name(void)
{
    // 未启动时无法访问 GAP 服务，跳过（启动流程会自动用 g_device_name 注册）
    if (!ble_bikepower_is_ready()) {
        ESP_LOGW(TAG, "BLE 未启动，跳过设备名刷新");
        return;
    }
    char device_name[DEVICE_NAME_MAX];
    portENTER_CRITICAL(&s_data_lock);
    memcpy(device_name, g_device_name, sizeof(device_name));
    portEXIT_CRITICAL(&s_data_lock);
    int rc = ble_svc_gap_device_name_set(device_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "GAP 设备名设置失败 rc=%d", rc);
    }
    // 已连接时不重启广播：避免 ble_gap_adv_stop 影响现有连接；
    // 连接内通过 GAP Device Name 服务读取即可拿到新名，断连回调会用新名重广播。
    if (ble_bikepower_conn_count() < BLE_DOWNSTREAM_MAX && !s_rebuild_pending) {
        ble_gap_adv_stop();
        rc = start_advertising();
        if (rc != 0 && !g_stopping) {
            ESP_LOGW(TAG, "刷新设备名后重广播失败 rc=%d", rc);
        }
    }
    ESP_LOGI(TAG, "BLE 设备名已刷新: %s", device_name);
}

/**
 * 获取当前 BLE 连接数。
 * 根据连接句柄是否有效返回单连接状态。
 * 无参数。
 * 返回值：当前下游连接数，范围0到BLE_DOWNSTREAM_MAX。
 * 特殊行为：上游功率计不计入下游客户端数量。
 */
int ble_bikepower_conn_count(void)
{
    int count = 0;
    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i)
        if (s_clients[i].connection != BLE_HS_CONN_HANDLE_NONE) ++count;
    return count;
}

/**
 * 启动 BLE 蓝牙功率计模块。
 * 初始化 NimBLE、注册 GATT 服务、设置设备名、启动周期通知定时器并创建 host 线程。
 * 无参数。
 * 返回值：`ESP_OK` 表示启动成功，否则为对应错误码。
 * 特殊行为：任一步失败都会回滚已分配资源，保持模块可再次尝试启动。
 */
bool ble_bikepower_begin_rebuild(uint16_t owner)
{
    portENTER_CRITICAL(&s_indication_lock);
    bool allowed = !s_rebuild_pending;
    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i) {
        uint16_t connection = s_clients[i].connection;
        if (connection != BLE_HS_CONN_HANDLE_NONE && connection != owner) allowed = false;
    }
    if (allowed) s_rebuild_pending = true;
    portEXIT_CRITICAL(&s_indication_lock);
    if (allowed && g_ready) ble_gap_adv_stop();
    return allowed;
}

void ble_bikepower_cancel_rebuild(void)
{
    s_rebuild_pending = false;
    if (g_ready && !g_stopping) (void)start_advertising();
}

uint16_t ble_bikepower_response_connection(uint32_t token)
{
    uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
    portENTER_CRITICAL(&s_indication_lock);
    for (size_t c = 0; c < BLE_DOWNSTREAM_MAX; ++c) {
        ble_client_t *client = &s_clients[c];
        for (uint8_t i = 0; i < client->count; ++i) {
            ble_indication_entry_t *entry = &client->indications[(client->head + i) % BLE_INDICATION_QUEUE_SIZE];
            if (entry->token == token && entry->generation == client->generation
                    && entry->connection == client->connection) connection = entry->connection;
        }
    }
    portEXIT_CRITICAL(&s_indication_lock);
    return connection;
}

esp_err_t ble_bikepower_start(void)
{
    _Static_assert(CONFIG_BT_NIMBLE_MAX_CONNECTIONS >= BLE_TOTAL_CONNECTIONS, "BLE connection capacity");
    _Static_assert(CONFIG_BT_NIMBLE_MAX_CCCDS >= CONFIG_BT_NIMBLE_MAX_BONDS * BLE_CCCDS_PER_BOND, "BLE CCCD capacity");
    g_stopping = false;
    g_start_failed = false;
    esp_err_t err = prepare_default_serial();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "初始化默认序列号失败 err=%d", err);
        return err;
    }
    ble_config_service_reset(indication_reserve, indicate_one, config_dis_profile_access_cb);
    if (!g_notify_lock) {
        g_notify_lock = xSemaphoreCreateMutex();
        if (!g_notify_lock) {
            ESP_LOGE(TAG, "创建通知互斥锁失败");
            return ESP_ERR_NO_MEM;
        }
    }
    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init 失败 err=%d", err);
        return err;
    }

    s_data_notify_pending = false;
    ble_npl_event_init(&s_notify_event, host_notify_event, NULL);
    ble_bond_cache_begin();
    ble_hs_cfg.gatts_register_cb = ble_bond_cache_register;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    err = configure_session_security();
    if (err != ESP_OK) {
        nimble_port_deinit();
        return err;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    reset_svc_handles();
    build_active_gatt_svcs();
    int rc = ble_gatts_count_cfg(g_active_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts_count_cfg 失败 rc=%d", rc);
        nimble_port_deinit();
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(g_active_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts_add_svcs 失败 rc=%d", rc);
        nimble_port_deinit();
        return ESP_FAIL;
    }
    rc = ble_telemetry_service_register();
    if (rc != 0) {
        ESP_LOGE(TAG, "遥测 GATT 注册失败 rc=%d", rc);
        ble_telemetry_service_deinit();
        nimble_port_deinit();
        return ESP_FAIL;
    }

    char device_name[DEVICE_NAME_MAX];
    portENTER_CRITICAL(&s_data_lock);
    memcpy(device_name, g_device_name, sizeof(device_name));
    portEXIT_CRITICAL(&s_data_lock);
    rc = ble_svc_gap_device_name_set(device_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "设置设备名失败 rc=%d", rc);
    }

    const esp_timer_create_args_t targs = { .callback = notify_tick, .name = "ble_notify" };
    err = esp_timer_create(&targs, &g_notify_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "创建通知定时器失败 err=%d", err);
        ble_telemetry_service_deinit();
        nimble_port_deinit();
        return err;
    }
    err = esp_timer_start_periodic(g_notify_timer, (uint64_t)BLE_NOTIFY_INTERVAL_MS * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "启动通知定时器失败 err=%d", err);
        esp_timer_delete(g_notify_timer);
        g_notify_timer = NULL;
        ble_telemetry_service_deinit();
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
    bool have_notify_lock = false;
    if (g_notify_lock) {
        if (xSemaphoreTake(g_notify_lock, pdMS_TO_TICKS(BLE_STOP_LOCK_WAIT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "等待通知锁超时，保持原 BLE 运行态");
            return false;
        }
        have_notify_lock = true;
    }
    g_stopping = true;
    ble_power_bridge_radio_stop();
    if (g_notify_timer) {
        esp_timer_stop(g_notify_timer);
        esp_timer_delete(g_notify_timer);
        g_notify_timer = NULL;
    }
    ble_telemetry_service_stop();

    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i) {
        uint16_t connection = s_clients[i].connection;
        if (connection != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    }
    ble_gap_adv_stop();

    int rc = nimble_port_stop();
    bool released = (rc == 0);
    if (released) {
        ble_telemetry_service_deinit();
        nimble_port_deinit();
        ESP_LOGI(TAG, "蓝牙已停用 释放射频");
    } else {
        ESP_LOGE(TAG, "nimble_port_stop 失败 rc=%d 射频可能仍被占用", rc);
    }
    for (size_t i = 0; i < BLE_DOWNSTREAM_MAX; ++i) connection_remove(s_clients[i].connection);
    indication_reset();
    s_rebuild_pending = false;
    g_ready = false;
    g_start_failed = !released;

    if (have_notify_lock) xSemaphoreGive(g_notify_lock);
    return released;
}
