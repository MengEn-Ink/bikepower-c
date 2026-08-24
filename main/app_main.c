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
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "bikepower_config.h"
#include "ble_bikepower.h"
#include "power_engine.h"
#include "nvs_store.h"
#include "app_events.h"
#include "app_state.h"
#include "button.h"
#include "led.h"
#include "radio_ctrl.h"
#include "wifi_portal.h"
#include "board_detect.h"

static const char *TAG = "APP";

static power_engine_t s_engine;
static QueueHandle_t s_app_queue;
// ERG 控制点队列满时的单槽邮箱：保留最后一条 target/clear 命令，避免两个独立槽反转顺序。
static volatile app_evt_t s_pending_erg_event;
static volatile bool s_has_pending_erg_event = false;
static volatile bool s_pending_wifi_done = false;

static bool on_erg_target(int target_power)
{
    // 范围校验：与领域层 POWER_MIN/POWER_MAX 一致
    if (target_power < POWER_MIN || target_power > POWER_MAX) return false;
    app_evt_t e = { .type = APP_EVT_ERG_TARGET, .data = target_power };
    if (s_app_queue && xQueueSend(s_app_queue, &e, 0) == pdTRUE) {
        return true;
    }
    // 队列满时保留最后一条 ERG 命令，后续 clear/target 可按真实到达顺序覆盖。
    portDISABLE_INTERRUPTS();
    s_pending_erg_event = e;
    s_has_pending_erg_event = true;
    portENABLE_INTERRUPTS();
    return true;
}

static void on_erg_clear(int reason)
{
    app_evt_t e = { .type = APP_EVT_ERG_CLEAR, .data = reason };
    if (s_app_queue && xQueueSend(s_app_queue, &e, 0) == pdTRUE) {
        return;
    }
    portDISABLE_INTERRUPTS();
    s_pending_erg_event = e;
    s_has_pending_erg_event = true;
    portENABLE_INTERRUPTS();
}

static bool on_run_control(ble_run_control_type_t type, int value)
{
    app_evt_t e = { 0 };
    switch (type) {
    case BLE_RUN_CTRL_SPEED:
        if (value < RUN_SPEED_X100_MIN || value > RUN_SPEED_X100_MAX) return false;
        e.type = APP_EVT_RUN_TARGET_SPEED;
        e.data = value;
        break;
    case BLE_RUN_CTRL_START:
        e.type = APP_EVT_RUN_START;
        break;
    case BLE_RUN_CTRL_STOP:
        e.type = APP_EVT_RUN_STOP;
        break;
    case BLE_RUN_CTRL_RESET:
        e.type = APP_EVT_RUN_RESET;
        break;
    default:
        return false;
    }
    return s_app_queue && xQueueSend(s_app_queue, &e, 0) == pdTRUE;
}

static void on_wifi_trigger(void)
{
    radio_ctrl_enter_wifi(&s_engine);
}

static void on_wifi_exit(void)
{
    radio_ctrl_exit_wifi();
}

static bool on_wifi_done(void)
{
    app_evt_t e = { .type = APP_EVT_WIFI_DONE, .data = 0 };
    // esp_timer 任务上下文投递事件给主循环。队列满时短暂重试，避免事件丢失导致
    // 永久卡在 ST_WIFI_CONFIG（portal_done 的 s_closing 守卫会阻止后续重试）。
    for (int i = 0; i < 5; i++) {
        if (xQueueSend(s_app_queue, &e, pdMS_TO_TICKS(10)) == pdTRUE) {
            ESP_LOGI(TAG, "APP_EVT_WIFI_DONE 已投递（尝试 %d）", i + 1);
            return true;
        }
    }
    portDISABLE_INTERRUPTS();
    s_pending_wifi_done = true;
    portENABLE_INTERRUPTS();
    ESP_LOGW(TAG, "APP_EVT_WIFI_DONE 队列投递失败，已转入主循环 pending latch");
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "BikePower C 启动 v%s (target=esp32c3, idf=v5.5.4)", FIRMWARE_VERSION);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS 初始化完成");

    power_engine_init(&s_engine);
    nvs_store_load(&s_engine);
#ifdef BIKEPOWER_TEST_SPORT_PROFILE
    s_engine.sport_profile = BIKEPOWER_TEST_SPORT_PROFILE;
    ESP_LOGW(TAG, "[测试固件] 运动 profile 覆盖为 %d，不写入 NVS", (int)s_engine.sport_profile);
#endif
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

    ret = button_init(s_app_queue);
    if (ret != ESP_OK) {
        // 按钮失败属局部降级：设备仍可作为纯 BLE 功率计正常服务，不进入 ERROR_RECOVERY。
        // ERROR_RECOVERY 专用于 BLE 核心故障（仅 BLE 重试成功才 clear_error），
        // 按钮失败无法靠 BLE 重试恢复，若置该态会让状态机永卡、LED 永久误报快闪。
        ESP_LOGE(TAG, "按钮初始化失败 ret=%d，按钮功能不可用，设备降级为纯 BLE 功率计", ret);
    }

    ble_bikepower_set_erg_callback(on_erg_target);
    ble_bikepower_set_erg_clear_callback(on_erg_clear);
    ble_bikepower_set_run_control_callback(on_run_control);
    ble_bikepower_set_profile(s_engine.sport_profile);
    ble_bikepower_set_broadcast_mask(s_engine.broadcast_mask);
    ble_bikepower_set_device_name(s_engine.device_name);
    esp_err_t ble_ret = ble_bikepower_start();
    if (ble_ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE 启动失败 ret=%d，进入错误恢复态", ble_ret);
        app_state_set_error();
    }

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
        bool has_wifi_done = false;
        portDISABLE_INTERRUPTS();
        if (s_pending_wifi_done) {
            s_pending_wifi_done = false;
            has_wifi_done = true;
        }
        portENABLE_INTERRUPTS();
        if (has_wifi_done) {
            app_evt_t wifi_done = { .type = APP_EVT_WIFI_DONE, .data = 0 };
            app_state_handle_event(&wifi_done);
        }
        app_evt_t pending_erg_event = { 0 };
        bool has_pending_erg_event = false;
        portDISABLE_INTERRUPTS();
        if (s_has_pending_erg_event) {
            pending_erg_event = s_pending_erg_event;
            has_pending_erg_event = true;
            s_has_pending_erg_event = false;
        }
        portENABLE_INTERRUPTS();
        if (has_pending_erg_event) {
            app_state_handle_event(&pending_erg_event);
        }

        // WiFi 配网模式（共存）：BLE 保持在线继续通知，主循环继续生成快照推 BLE；
        // APP_EVT_WIFI_DONE 已在上方队列消费中处理（调用 radio_ctrl_exit_wifi + 切回 ST_BLE_ACTIVE）。
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
                    ble_bikepower_stop();
                }
                ESP_LOGW(TAG, "重试 BLE 启动...");
                esp_err_t r = ble_bikepower_start();
                if (r == ESP_OK) {
                    ESP_LOGI(TAG, "BLE 重试启动成功");
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

        if (wifi_portal_ble_restart_requested()) {
            if (conn == 0) {
                ESP_LOGI(TAG, "运动 profile 已切换，重启 BLE 以重建 GATT 服务表");
                if (ble_bikepower_stop()) {
                    vTaskDelay(pdMS_TO_TICKS(RADIO_BLE_RELEASE_WAIT_MS));
                    ble_bikepower_set_profile(s_engine.sport_profile);
                    ble_bikepower_set_broadcast_mask(s_engine.broadcast_mask);
                    ble_bikepower_set_device_name(s_engine.device_name);
                    esp_err_t r = ble_bikepower_start();
                    if (r == ESP_OK) {
                        wifi_portal_consume_ble_restart_requested();
                        last_conn = 0;
                        app_state_clear_error();
                    } else {
                        ESP_LOGE(TAG, "profile 切换后 BLE 重启失败 ret=%d", r);
                        app_state_set_error();
                    }
                } else {
                    ESP_LOGE(TAG, "profile 切换时 BLE 停用失败，等待下轮重试");
                    app_state_set_error();
                }
            } else if (tick % 20 == 0) {
                ESP_LOGW(TAG, "运动 profile 已切换，等待 BLE 断开后重建 GATT 服务表 conn=%d", conn);
            }
        }

        app_state_tick();

        snapshot_t s = s_engine.snapshot;
        ble_bikepower_set_broadcast_mask(s_engine.broadcast_mask);
        if (s_engine.sport_profile == SPORT_PROFILE_RUNNING) {
            running_snapshot_t rs = running_engine_snapshot(&s_engine.running, HR_LOOP_INTERVAL_MS);
            int running_hr = power_engine_running_heartrate(&s_engine, rs.speed_x100_kmh);
            s.heartrate = running_hr;
            ble_bikepower_set_running_data(rs, (uint8_t)running_hr);
        } else {
            s = power_engine_snapshot(&s_engine);
            ble_bikepower_set_data((uint16_t)s.power, (uint8_t)s.heartrate, (uint16_t)s.cadence);
        }

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
