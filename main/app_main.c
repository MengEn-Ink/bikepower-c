/**
 * BikePower C — 应用入口
 * 运行环境：ESP-IDF v5.5.4 + NimBLE，目标芯片 ESP32-C3
 *
 * 任务结构：
 *   nimble_host   NimBLE 协议栈（ble_bikepower 内部启动）
 *   button_task   GPIO 中断按钮 -> app_queue
 *   led_task      LED 状态指示
 *   app_main_task 本函数主循环：50ms 节拍生成快照 + 消费事件队列 + 喂看门狗
 */
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "bikepower_config.h"
#include "ble_bikepower.h"
#include "ble_config_codec.h"
#include "ble_config_service.h"
#include "host/ble_gap.h"
#include "device_telemetry_runtime.h"
#include "domain_rng.h"
#include "power_engine.h"
#include "nvs_store.h"
#include "persistent_store.h"
#include "app_events.h"
#include "app_state.h"
#include "button.h"
#include "led.h"
#include "radio_ctrl.h"
#include "wifi_portal.h"
#include "board_detect.h"
#include "device_identity_store.h"
#include "bridge_runtime.h"
#include "esp_timer.h"

static const char *TAG = "APP";

static power_engine_t s_engine;
// 主任务发布来源，host 控制点只读原子副本，不直接访问正在更新的引擎。
static atomic_bool s_real_power_source;
static QueueHandle_t s_app_queue;
// WiFi 完成事件队列满时的单比特保底；事件幂等，无需计数。
static bool s_pending_wifi_done = false;
static config_data_t s_pending_ble_config;
static bool s_has_pending_ble_config = false;
static uint32_t s_pending_ble_config_transaction = 0;
static uint32_t s_pending_ble_config_revision = 0;
static bool s_pending_config_wifi;
static bool s_pending_config_identity;
static device_identity_t s_config_identity;
static bool s_processing_config_wifi;
static bool s_ble_config_restart_pending = false;
static uint32_t s_ble_rebuild_wait_ms = 0;  // 布局变更开始等待对端断开的时刻（0=未计时）
// DIS 身份覆盖写入 mailbox：host 任务只复制，主循环负责 NVS 落盘后再让 DIS 读值生效。
static device_identity_t s_pending_identity;
static bool s_has_pending_identity = false;

static void on_bridge_recent(const bridge_device_t *device)
{
    if (!nvs_store_save_bridge_recent(device)) {
        ESP_LOGW(TAG, "最近成功功率计提示保存失败，下次启动回退设备表顺序");
    }
}

static esp_err_t on_wifi_bridge_command(bridge_runtime_command_t command,
                                        const bridge_device_t *device)
{
    return bridge_runtime_request(command, device);
}

/**
 * DIS 身份覆盖写入回调（NimBLE host 任务上下文）。
 * 仅复制到 mailbox；上一笔尚未落盘时拒绝，避免覆盖未确认的数据。
 */
static bool on_ble_identity_write(const device_identity_t *identity)
{
    if (!identity || ble_config_service_busy()) return false;
    bool accepted = false;
    portDISABLE_INTERRUPTS();
    if (!s_has_pending_identity) {
        s_pending_identity = *identity;
        s_has_pending_identity = true;
        accepted = true;
    }
    portENABLE_INTERRUPTS();
    return accepted;
}

/* 主循环消费 DIS 身份覆盖 mailbox：落盘成功后刷新 BLE 侧快照。 */
static void process_pending_identity(void)
{
    device_identity_t requested;
    bool pending = false;
    portDISABLE_INTERRUPTS();
    if (s_has_pending_identity) {
        requested = s_pending_identity;
        pending = true;
    }
    portENABLE_INTERRUPTS();
    if (!pending) return;

    if (nvs_store_save_with_identity(&s_engine, &requested)) {
        ble_bikepower_set_identity(&requested);
        ESP_LOGI(TAG, "DIS 身份覆盖已保存（%s）",
                 device_identity_has_override(&requested) ? "自定义" : "恢复内置");
    } else {
        ESP_LOGW(TAG, "DIS 身份覆盖写入 NVS 失败，保持原值");
    }
    portDISABLE_INTERRUPTS();
    s_has_pending_identity = false;
    portENABLE_INTERRUPTS();
}

/** 控制点有界 mailbox：host 只接收，主循环按票据应用后确认；满槽拒绝不覆盖。 */
typedef struct {
    bool pending, rsc;
    uint32_t ticket, distance_dm, generation;
    app_evt_t event;
} control_mailbox_t;
static control_mailbox_t s_control_mailbox;
static uint32_t s_applied_control_generation;

static bool queue_control(app_evt_t event, bool rsc, uint32_t distance_dm)
{
    portDISABLE_INTERRUPTS();
    bool accepted = !s_control_mailbox.pending;
    if (accepted) s_control_mailbox = (control_mailbox_t){
        .pending = true, .rsc = rsc, .distance_dm = distance_dm,
        .ticket = ble_bikepower_control_ticket(rsc), .event = event,
        .generation = ble_bikepower_control_generation(),
    };
    portENABLE_INTERRUPTS();
    return accepted;
}

static bool on_erg_target(int target_power)
{
    if (s_real_power_source || target_power < POWER_MIN || target_power > POWER_MAX) return false;
    return queue_control((app_evt_t){ .type = APP_EVT_ERG_TARGET, .data = target_power }, false, 0);
}

static bool on_erg_clear(int reason)
{
    return queue_control((app_evt_t){ .type = APP_EVT_ERG_CLEAR, .data = reason }, false, 0);
}

static bool on_run_control(ble_run_control_type_t type, int value)
{
    app_evt_t event = {0};
    switch (type) {
    case BLE_RUN_CTRL_SPEED:
        if (value < RUN_SPEED_X100_MIN || value > RUN_SPEED_X100_MAX) return false;
        event.type = APP_EVT_RUN_TARGET_SPEED; event.data = value; break;
    case BLE_RUN_CTRL_START: event.type = APP_EVT_RUN_START; break;
    case BLE_RUN_CTRL_STOP: event.type = APP_EVT_RUN_STOP; break;
    case BLE_RUN_CTRL_RESET: event.type = APP_EVT_RUN_RESET; break;
    default: return false;
    }
    return queue_control(event, false, 0);
}

static bool on_rsc_distance(uint32_t distance_dm)
{
    return queue_control((app_evt_t){0}, true, distance_dm);
}

static void process_control_mailbox(void)
{
    uint32_t generation = ble_bikepower_control_generation();
    if (generation != s_applied_control_generation) {
        app_state_handle_event(&(app_evt_t){ .type = APP_EVT_ERG_CLEAR });
        s_applied_control_generation = generation;
    }
    portDISABLE_INTERRUPTS();
    control_mailbox_t command = s_control_mailbox;
    s_control_mailbox.pending = false;
    portENABLE_INTERRUPTS();
    if (!command.pending || (!command.rsc && command.generation != ble_bikepower_control_generation())
            || !ble_bikepower_control_current(command.ticket, command.rsc)) return;
    if (command.rsc) {
        s_engine.running.distance_x1000_m = (uint64_t)command.distance_dm * 100u;
        s_engine.running.snapshot.total_distance_m = command.distance_dm / 10u;
        s_engine.running.snapshot.total_distance_dm = command.distance_dm;
    } else {
        app_state_handle_event(&command.event);
        if (command.event.type == APP_EVT_RUN_RESET) power_engine_set_run_speed(&s_engine, RUN_SPEED_X100_DEFAULT);
        s_applied_control_generation = command.generation;
    }
    ble_bikepower_control_complete(command.ticket, command.rsc, true);
}

static bool on_ble_config_commit(const config_data_t *config,
                                 uint32_t transaction_id,
                                 uint32_t base_revision)
{
    if (!config || transaction_id == 0) return false;
    bool accepted = false;
    portDISABLE_INTERRUPTS();
    if (!s_has_pending_ble_config) {
        s_pending_ble_config = *config;
        s_pending_ble_config_transaction = transaction_id;
        s_pending_ble_config_revision = base_revision;
        s_has_pending_ble_config = true;
        s_pending_config_wifi = false;
        s_pending_config_identity = false;
        accepted = true;
    }
    portENABLE_INTERRUPTS();
    return accepted;
}

static bool on_wifi_config_commit(const config_data_t *config, uint32_t transaction,
                                  uint32_t revision, bool close_wifi,
                                  const device_identity_t *identity)
{
    (void)close_wifi;
    if (!config || !transaction || ble_config_service_busy()) return false;
    portDISABLE_INTERRUPTS();
    bool accepted = !s_has_pending_ble_config;
    if (accepted) {
        s_pending_ble_config = *config;
        s_pending_ble_config_transaction = transaction;
        s_pending_ble_config_revision = revision;
        s_pending_config_wifi = true;
        s_pending_config_identity = identity != NULL;
        if (identity) s_config_identity = *identity;
        s_has_pending_ble_config = true;
    }
    portENABLE_INTERRUPTS();
    return accepted;
}

static void config_commit_result(uint32_t transaction, uint8_t result,
                                  uint8_t restart, const config_data_t *applied)
{
    if (s_processing_config_wifi) wifi_portal_config_result(transaction, result, restart, applied);
    else ble_bikepower_config_commit_result(transaction, result, restart, applied);
}

static void process_pending_ble_config(void)
{
    /*
     * 仅由主任务串行调用。事务工作副本静态预分配，避免内联后叠加
     * 到 app_main 栈帧并挤占启动配置校验和 NVS 调用的栈空间。
     */
    static config_data_t checked;
    static config_data_t previous;
    static config_data_t applied;
    static power_engine_t next;
    static device_identity_t identity;
    static device_identity_t old_identity;
    uint32_t transaction_id = 0;
    uint32_t base_revision = 0;
    bool pending = false;
    bool copy_identity = false;

    portDISABLE_INTERRUPTS();
    if (s_has_pending_ble_config) {
        checked = s_pending_ble_config;
        transaction_id = s_pending_ble_config_transaction;
        base_revision = s_pending_ble_config_revision;
        s_processing_config_wifi = s_pending_config_wifi;
        copy_identity = s_pending_config_identity;
        if (copy_identity) identity = s_config_identity;
        s_has_pending_ble_config = false;
        pending = true;
    }
    portENABLE_INTERRUPTS();
    if (!pending) return;

    if (!(s_processing_config_wifi ? wifi_portal_revision_matches(base_revision)
                                   : ble_bikepower_config_revision_matches(base_revision))) {
        ESP_LOGW(TAG, "拒绝 BLE 配置事务 id=%lu rev=%lu：提交前配置已变化",
                 (unsigned long)transaction_id, (unsigned long)base_revision);
        config_commit_result(transaction_id, BLE_CONFIG_RESULT_STALE_REVISION,
                                           0, NULL);
        return;
    }

    if (checked.schema_ver != CONFIG_VERSION || config_store_validate(&checked)) {
        ESP_LOGW(TAG, "拒绝 BLE 配置事务 id=%lu rev=%lu：字段越界或 schema 不匹配",
                 (unsigned long)transaction_id, (unsigned long)base_revision);
        config_commit_result(transaction_id, BLE_CONFIG_RESULT_INVALID_VALUE,
                                           0, NULL);
        return;
    }

    config_store_from_engine(&s_engine, &previous);
    next = s_engine;
    if (!config_store_apply_to_engine(&checked, &next)) {
        config_commit_result(transaction_id, BLE_CONFIG_RESULT_INVALID_VALUE,
                                           0, NULL);
        return;
    }
    if (copy_identity) {
        device_identity_store_load(&old_identity);
        for (size_t i = 0; i < DEVICE_IDENTITY_FIELD_COUNT; i++) {
            if (!identity.fields[i][0]) {
                memcpy(identity.fields[i], old_identity.fields[i], DEVICE_IDENTITY_TEXT_MAX);
            }
        }
    }
    bool rebuild = previous.sport_profile != checked.sport_profile
        || previous.broadcast_mask != checked.broadcast_mask
        || previous.bridge.source != checked.bridge.source;
    if (rebuild) {
        uint16_t owner = s_processing_config_wifi ? BLE_HS_CONN_HANDLE_NONE
            : ble_config_service_owner(transaction_id);
        if (!ble_bikepower_begin_rebuild(owner, s_processing_config_wifi)) {
            config_commit_result(transaction_id, BLE_CONFIG_RESULT_BUSY, 0, NULL);
            return;
        }
    }
    if (!nvs_store_save_with_identity(&next, copy_identity ? &identity : NULL)) {
        if (rebuild) ble_bikepower_cancel_rebuild();
        ESP_LOGW(TAG, "BLE 配置事务 id=%lu 写入 NVS 失败", (unsigned long)transaction_id);
        config_commit_result(transaction_id, BLE_CONFIG_RESULT_SAVE_FAILED,
                                           0, NULL);
        return;
    }

    config_store_from_engine(&next, &applied);
    uint8_t restart_flags = 0;
    if (previous.sport_profile != applied.sport_profile
            || previous.broadcast_mask != applied.broadcast_mask
            || previous.bridge.source != applied.bridge.source) {
        restart_flags |= BLE_CONFIG_RESTART_GATT;
    }
    if (strncmp(previous.device_name, applied.device_name, DEVICE_NAME_MAX) != 0) {
        restart_flags |= BLE_CONFIG_RESTART_NAME;
    }

    s_engine = next;
    s_real_power_source = s_engine.bridge.source == BRIDGE_SOURCE_REAL;
    if (copy_identity) ble_bikepower_set_identity(&identity);
    bridge_runtime_apply_config(&applied.bridge, applied.sport_profile,
        (uint32_t)(esp_timer_get_time() / 1000));
    power_engine_clear_dirty(&s_engine);
    app_state_refresh_led();
    if (restart_flags & BLE_CONFIG_RESTART_NAME) {
        ble_bikepower_set_device_name(s_engine.device_name);
        if ((restart_flags & BLE_CONFIG_RESTART_GATT) == 0) {
            ble_bikepower_refresh_device_name();
        }
    }
    config_commit_result(transaction_id, BLE_CONFIG_RESULT_SUCCESS,
                                       restart_flags, &applied);
    if (restart_flags & BLE_CONFIG_RESTART_GATT) {
        s_ble_config_restart_pending = true;
    }
    ESP_LOGI(TAG, "BLE 配置事务已保存 id=%lu rev=%lu restart=0x%02x",
             (unsigned long)transaction_id, (unsigned long)base_revision,
             restart_flags);
}

/**
 * 处理进入 WiFi 配网模式的触发回调。
 * 由状态机在用户完成二次确认后调用，切换到 WiFi/BLE 共存的 WiFi 配网流程。
 * 无参数。
 * 返回值：无。
 * 特殊行为：BLE 保持在线继续通知，WiFi 由 coex 时间片仲裁；极端失败时降级为停 BLE + 重启。
 */
static void on_wifi_trigger(void)
{
    radio_ctrl_enter_wifi(&s_engine);
}

/**
 * 处理退出 WiFi 配网模式的回调。
 * 由状态机在配网结束（保存成功或超时）后调用，关闭 HTTP 门户 + SoftAP，射频归还 BLE。
 * 无参数。
 * 返回值：无。
 * 特殊行为：BLE 全程未停，无需重激活；不重启设备。
 */
static void on_wifi_exit(void)
{
    radio_ctrl_exit_wifi();
}

/**
 * WiFi 配网结束回调（保存成功或超时）。
 * 由 wifi_portal 在 esp_timer 任务上下文调用，仅做轻量操作：投递事件给主循环，
 * 实际的 wifi_portal_stop / radio_ctrl_exit_wifi / 状态切换在 app_main 任务上下文串行执行。
 * 无参数。
 * 返回值：true 表示事件已入队或已记入 pending latch。
 * 特殊行为：队列满时仅设置单比特 latch，不在 esp_timer 任务中阻塞等待。
 */
static bool on_wifi_done(void)
{
    app_evt_t e = { .type = APP_EVT_WIFI_DONE, .data = 0 };
    if (s_app_queue && xQueueSend(s_app_queue, &e, 0) == pdTRUE) {
        ESP_LOGI(TAG, "APP_EVT_WIFI_DONE 已投递");
        return true;
    }

    portDISABLE_INTERRUPTS();
    s_pending_wifi_done = true;
    portENABLE_INTERRUPTS();
    ESP_LOGW(TAG, "APP_EVT_WIFI_DONE 队列满，已记入 pending latch");
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "BikePower C 启动 v%s (target=esp32c3, idf=v5.5.4)", FIRMWARE_VERSION);

    esp_err_t ret = nvs_flash_init();
    bool storage_ready = ret == ESP_OK;
    if (!storage_ready) {
        ESP_LOGE(TAG, "NVS 初始化失败 ret=%d，保留分区，使用只读默认配置", ret);
        persistent_store_readonly_defaults();
    } else {
        ESP_LOGI(TAG, "NVS 初始化完成");
    }

    power_engine_init(&s_engine);
    if (storage_ready) nvs_store_load(&s_engine);
    static bridge_device_t bridge_recent;
    bool have_bridge_recent = storage_ready && nvs_store_load_bridge_recent(&bridge_recent);
    power_engine_clear_dirty(&s_engine);
    ESP_LOGI(TAG, "功率引擎初始化 p=%dW c=%dRPM hr=%d mode=%d",
             s_engine.base_power, s_engine.cadence_base,
             s_engine.last_heartrate, s_engine.mode);

    s_app_queue = xQueueCreate(APP_EVENT_QUEUE_LEN, sizeof(app_evt_t));
    if (!s_app_queue) {
        ESP_LOGE(TAG, "xQueueCreate 失败，无法继续");
        esp_restart();
    }

    board_detect();
    char fw_name[96];
    board_firmware_name(fw_name, sizeof(fw_name));
    ESP_LOGI(TAG, "固件名称：%s", fw_name);

    ret = led_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED 初始化失败 ret=%d", ret);
        esp_restart();
    }

    app_state_init(&s_engine);
    app_state_set_wifi_trigger(on_wifi_trigger);
    app_state_set_wifi_exit(on_wifi_exit);
    wifi_portal_set_done_callback(on_wifi_done);  // 注册配网结束回调，由 wifi_portal 在保存/超时后调用
    wifi_portal_set_config_callback(on_wifi_config_commit);
    wifi_portal_set_bridge_callback(on_wifi_bridge_command);
    wifi_portal_sync(&s_engine);

    ret = button_init(s_app_queue);
    if (ret != ESP_OK) {
        // 按钮失败属局部降级：设备仍可作为纯 BLE 功率计正常服务，不进入 ERROR_RECOVERY。
        // ERROR_RECOVERY 专用于 BLE 核心故障（仅 BLE 重试成功才 clear_error），
        // 按钮失败无法靠 BLE 重试恢复，若置该态会让状态机永卡、LED 永久误报快闪。
        ESP_LOGE(TAG, "按钮初始化失败 ret=%d，按钮功能不可用，设备降级为纯 BLE 功率计", ret);
    }

    ble_bikepower_set_erg_callback(on_erg_target);
    s_real_power_source = s_engine.bridge.source == BRIDGE_SOURCE_REAL;
    ble_bikepower_set_erg_clear_callback(on_erg_clear);
    ble_bikepower_set_run_control_callback(on_run_control);
    ble_bikepower_enable_async_control(on_rsc_distance);
    ble_bikepower_set_config_commit_callback(on_ble_config_commit);
    ble_bikepower_set_identity_write_callback(on_ble_identity_write);
    {
        device_identity_t identity;
        device_identity_store_load(&identity);
        ble_bikepower_set_identity(&identity);
    }
    ble_bikepower_set_profile(s_engine.sport_profile);
    ble_bikepower_set_bridge_mode(s_engine.bridge.source == BRIDGE_SOURCE_REAL);
    ble_bikepower_set_broadcast_mask(s_engine.broadcast_mask);
    ble_bikepower_set_device_name(s_engine.device_name);
    ble_bikepower_set_config_snapshot(&s_engine);
    esp_err_t ble_ret = ble_bikepower_start();
    bool bridge_runtime_started = false;
    if (ble_ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE 启动失败 ret=%d，进入错误恢复态", ble_ret);
        app_state_set_error();
    } else {
        bridge_runtime_set_recent_callback(on_bridge_recent);
        bridge_runtime_init(&s_engine.bridge, s_engine.sport_profile,
                            have_bridge_recent ? &bridge_recent : NULL,
                            (uint32_t)(esp_timer_get_time() / 1000));
        bridge_runtime_started = true;
    }
    domain_rng_seed(esp_random());

    // 看门狗：主循环每拍喂狗（idf 默认已为 IDLE 任务注册 TWDT，这里订阅当前任务）
    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err != ESP_OK) {
        ESP_LOGW(TAG, "TWDT 订阅失败(%s)，主循环看门狗保护未生效", esp_err_to_name(wdt_err));
    }

    int tick = 0;
    int last_conn = 0;
    int ble_retry_tick = 0;
    int hb_count = 0;  // 心跳序号（10s 递增，用于遥测与内存基线）
    app_evt_t e;

    while (1) {
        // 消费按钮/事件队列（非阻塞批量）
        while (xQueueReceive(s_app_queue, &e, 0) == pdTRUE) {
            app_state_handle_event(&e);
        }
        // 队列满时的 WiFi 完成保底必须在任何 BLE-not-ready continue 之前消费，
        // 否则独占降级路径停掉 BLE 后会同时阻断 WiFi 资源回收。
        bool has_wifi_done = false;
        portDISABLE_INTERRUPTS();
        if (s_pending_wifi_done) {
            has_wifi_done = true;
            s_pending_wifi_done = false;
        }
        portENABLE_INTERRUPTS();
        if (has_wifi_done) {
            app_evt_t wifi_done = { .type = APP_EVT_WIFI_DONE, .data = 0 };
            app_state_handle_event(&wifi_done);
        }
        ble_bikepower_set_config_snapshot(&s_engine);
        device_telemetry_sample((uint8_t)app_state_get(), (uint8_t)s_engine.sport_profile);
        wifi_portal_sync(&s_engine);
        process_pending_ble_config();
        ble_bikepower_set_config_snapshot(&s_engine);
        process_pending_identity();
        process_control_mailbox();

        // WiFi 配网模式（共存）：BLE 保持在线继续通知，主循环继续生成快照推 BLE；
        // APP_EVT_WIFI_DONE 已在上方队列或 pending latch 消费中处理。
        // 不再 continue 跳过 BLE 通知，确保配网期间骑行 App 仍能收到实时数据。

        // BLE 未就绪：周期重试，跳过 BLE 调用，避免假运行
        if (!ble_bikepower_is_ready()) {
            if (radio_ctrl_ble_suspended()) {
                esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(HR_LOOP_INTERVAL_MS));
                continue;
            }
            if (++ble_retry_tick >= BLE_RETRY_TICKS) {
                ble_retry_tick = 0;
                // 如果是启动过程中异步失败，先清理资源再重试
                if (ble_bikepower_start_failed()) {
                    ESP_LOGW(TAG, "BLE 启动失败，清理资源后重试");
                    if (!ble_bikepower_stop()) {
                        ESP_LOGE(TAG, "BLE 清理未完成，禁止重新初始化 Host");
                        esp_task_wdt_reset();
                        vTaskDelay(pdMS_TO_TICKS(HR_LOOP_INTERVAL_MS));
                        continue;
                    }
                }
                ESP_LOGW(TAG, "重试 BLE 启动...");
                esp_err_t r = ble_bikepower_start();
                if (r == ESP_OK) {
                    ESP_LOGI(TAG, "BLE 重试启动成功");
                    if (!bridge_runtime_started) {
                        bridge_runtime_set_recent_callback(on_bridge_recent);
                        bridge_runtime_init(&s_engine.bridge,
                            s_engine.sport_profile,
                            have_bridge_recent ? &bridge_recent : NULL,
                            (uint32_t)(esp_timer_get_time() / 1000));
                        bridge_runtime_started = true;
                    } else {
                        bridge_runtime_request(BRIDGE_RUNTIME_RETRY_NOW, NULL);
                    }
                    app_state_clear_error();
                } else {
                    ESP_LOGE(TAG, "BLE 重试启动仍失败 ret=%d", r);
                }
            }
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(HR_LOOP_INTERVAL_MS));
            continue;
        }

#ifdef PORTAL_TEST_AUTO_CONFIG
        // 测试固件：BLE 首次就绪后延迟 3s（60×50ms）自动触发配网，便于配网门户自动化测试
        {
            static int s_auto_cfg_tick = 0;
            if (s_auto_cfg_tick >= 0 && ++s_auto_cfg_tick == 60) {
                s_auto_cfg_tick = -1;
                ESP_LOGW(TAG, "[配网测试] BLE 就绪 3s，自动触发配网");
                app_state_enter_wifi_config_for_test();
            }
        }
#endif

        // 轮询 BLE 连接变化并投递事件
        int conn = ble_bikepower_conn_count();
        if (conn != last_conn) {
            last_conn = conn;
            app_evt_t ce = { .type = APP_EVT_CONN_CHANGED, .data = conn };
            app_state_handle_event(&ce);
        }

        bool wifi_restart_requested = wifi_portal_ble_restart_requested();
        if (wifi_restart_requested || s_ble_config_restart_pending) {
            // 布局变更：先给对端一小段自行断开的宽限，超时后由 stop 主动终止链路重建，
            // 避免 Windows 等主机保留 ACL 时广播永久停摆。
            uint32_t rebuild_now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            if (s_ble_rebuild_wait_ms == 0) s_ble_rebuild_wait_ms = rebuild_now_ms ? rebuild_now_ms : 1;
            bool grace_expired =
                (uint32_t)(rebuild_now_ms - s_ble_rebuild_wait_ms) >= RADIO_BLE_REBUILD_GRACE_MS;
            if (conn == 0 || grace_expired) {
                if (conn != 0) ESP_LOGW(TAG, "等待对端断开超时，主动断开并重建 GATT 服务表 conn=%d", conn);
                ESP_LOGI(TAG, "运动 profile 或服务开关已变化，重启 BLE 以重建 GATT 服务表");
                if (ble_bikepower_stop()) {
                    vTaskDelay(pdMS_TO_TICKS(RADIO_BLE_RELEASE_WAIT_MS));
                    ble_bikepower_set_profile(s_engine.sport_profile);
                    ble_bikepower_set_bridge_mode(s_engine.bridge.source == BRIDGE_SOURCE_REAL);
                    ble_bikepower_set_broadcast_mask(s_engine.broadcast_mask);
                    ble_bikepower_set_device_name(s_engine.device_name);
                    esp_err_t r = ble_bikepower_start();
                    if (r == ESP_OK) {
                        if (wifi_restart_requested) wifi_portal_consume_ble_restart_requested();
                        s_ble_config_restart_pending = false;
                        s_ble_rebuild_wait_ms = 0;
                        last_conn = 0;
                        app_state_clear_error();
                        bridge_runtime_request(BRIDGE_RUNTIME_RETRY_NOW, NULL);
                    } else {
                        ESP_LOGE(TAG, "BLE 重启失败 ret=%d", r);
                        app_state_set_error();
                    }
                } else {
                    ESP_LOGE(TAG, "BLE 停用失败，等待下轮重试");
                    app_state_set_error();
                }
            } else if (tick % 20 == 0) {
                ESP_LOGW(TAG, "运动 profile 或服务开关已变化，等待 BLE 断开后重建 GATT 服务表 conn=%d", conn);
            }
        } else {
            s_ble_rebuild_wait_ms = 0;
        }

        app_state_tick();
        if (!ble_bikepower_is_ready()) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(HR_LOOP_INTERVAL_MS));
            continue;
        }

        snapshot_t s = s_engine.snapshot;
        uint32_t bridge_now = (uint32_t)(esp_timer_get_time() / 1000);
        bridge_runtime_tick(&s_engine.bridge, s_engine.sport_profile,
                            bridge_now);
        bridge_runtime_snapshot_t bridge =
            bridge_runtime_snapshot(&s_engine.bridge, bridge_now);
        wifi_portal_sync_bridge(&bridge);
        ble_bikepower_set_broadcast_mask(s_engine.broadcast_mask);
        if (s_engine.sport_profile == SPORT_PROFILE_RUNNING) {
            running_snapshot_t rs = running_engine_snapshot(&s_engine.running, HR_LOOP_INTERVAL_MS);
            int running_hr = power_engine_running_heartrate(&s_engine, rs.speed_x100_kmh);
            s.heartrate = running_hr;
            s_engine.snapshot.heartrate = running_hr;
            ble_bikepower_set_running_data(rs, (uint8_t)running_hr);
        } else if (s_engine.bridge.source == BRIDGE_SOURCE_REAL
                   && !bridge_runtime_uses_simulation()) {
            s = power_engine_bridge_snapshot(&s_engine,
                                             &bridge.session.sample);
            ble_bikepower_set_bridge_data(&bridge.session.sample,
                                          (uint8_t)s.heartrate);
        } else {
            s = power_engine_snapshot(&s_engine);
            ble_bikepower_set_data((uint16_t)s.power, (uint8_t)s.heartrate, (uint16_t)s.cadence);
        }

        wifi_portal_sync(&s_engine);
        if (++tick >= MAIN_HEARTBEAT_INTERVAL_TICKS) {  // 每 10s 一次心跳日志
            tick = 0;
            hb_count++;
            uint32_t heap = esp_get_free_heap_size();
            uint32_t min_heap = esp_get_minimum_free_heap_size();
            const char *mode_str = (s.mode == RIDE_MODE_STEADY) ? "steady"
                                : (s.mode == RIDE_MODE_ROAD) ? "road"
                                : (s.mode == RIDE_MODE_ERG) ? "erg" : "?";

            ESP_LOGI(TAG, "遥测 #%d 堆=%lu/%lu p=%dW(%d) c=%dRPM hr=%dBPM m=%s step=%d conn=%d state=%d",
                     hb_count,
                     (unsigned long)heap, (unsigned long)min_heap,
                     s.power, s_engine.base_power,
                     s.cadence, s.heartrate,
                     mode_str, s_engine.power_step,
                     conn, app_state_get());

            // 每 30 个心跳（300s）输出内存基线
            if (hb_count % 30 == 0) {
                ESP_LOGI(TAG, "内存基线 #%d 空闲=%lu 最低=%lu",
                         hb_count, (unsigned long)heap, (unsigned long)min_heap);
            }

            if (heap < HEAP_CRIT_BYTES) {
                ESP_LOGE(TAG, "空闲堆严重不足 %lu < %d，存在崩溃风险",
                         (unsigned long)heap, HEAP_CRIT_BYTES);
            } else if (heap < HEAP_WARN_BYTES) {
                ESP_LOGW(TAG, "空闲堆偏低 %lu < %d",
                         (unsigned long)heap, HEAP_WARN_BYTES);
            }

            // 配置脏标记则周期落盘（调功/改配置后断电可恢复）
            if (power_engine_is_dirty(&s_engine)) {
                ESP_LOGI(TAG, "心跳触发 NVS 落盘");
                if (nvs_store_save(&s_engine)) {
                    power_engine_clear_dirty(&s_engine);
                }
            }
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(HR_LOOP_INTERVAL_MS));
    }
}
