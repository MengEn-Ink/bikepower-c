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
#include "board_detect.h"

static const char *TAG = "APP";

static power_engine_t s_engine;
static QueueHandle_t s_app_queue;
// ERG目标单独缓存：最新目标优先，不占用事件队列，避免队列满误报Invalid Param
static volatile int s_pending_erg_target = 0;
static volatile bool s_has_pending_erg = false;

/**
 * 处理 FTMS 控制点下发的 ERG 目标功率。
 * 先做范围校验，缓存最新目标由主循环串行处理，不进事件队列。
 * target_power：目标功率，单位瓦特（signed 16-bit）。
 * 返回值：true 表示参数合法已接收，false 表示范围无效。
 * 特殊行为：运行在 NimBLE host 任务上下文，只做轻量校验和缓存，不直接改引擎状态；永远使用最新目标，旧目标自动覆盖。
 */
static bool on_erg_target(int target_power)
{
    // 范围校验：与领域层 POWER_MIN/POWER_MAX 一致
    if (target_power < POWER_MIN || target_power > POWER_MAX) return false;
    // 关中断保护写入，与主循环读取+清标志互斥，避免新目标被覆盖丢失（单核C3关中断足够）
    portDISABLE_INTERRUPTS();
    s_pending_erg_target = target_power;
    s_has_pending_erg = true;
    portENABLE_INTERRUPTS();
    return true;
}

/**
 * 处理进入 WiFi 配网模式的触发回调。
 * 由状态机在用户完成二次确认后调用，切换到射频独占的 WiFi 配网流程。
 * 无参数。
 * 返回值：无。
 * 特殊行为：会停止 BLE 并进入 radio_ctrl 管理的 WiFi 配网模式。
 */
static void on_wifi_trigger(void)
{
    radio_ctrl_enter_wifi(&s_engine);
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
    power_engine_clear_dirty(&s_engine);
    ESP_LOGI(TAG, "功率引擎初始化 p=%dW c=%dRPM hr=%d mode=%d",
             s_engine.base_power, s_engine.cadence_base,
             s_engine.last_heartrate, s_engine.mode);

    s_app_queue = xQueueCreate(16, sizeof(app_evt_t));
    if (!s_app_queue) {
        ESP_LOGE(TAG, "xQueueCreate 失败，无法继续");
        esp_restart();
    }

    board_detect();

    ret = led_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED 初始化失败 ret=%d", ret);
        esp_restart();
    }

    app_state_init(&s_engine);
    app_state_set_wifi_trigger(on_wifi_trigger);

    ret = button_init(s_app_queue);
    if (ret != ESP_OK) {
        // 按钮失败属局部降级：设备仍可作为纯 BLE 功率计正常服务，不进入 ERROR_RECOVERY。
        // ERROR_RECOVERY 专用于 BLE 核心故障（仅 BLE 重试成功才 clear_error），
        // 按钮失败无法靠 BLE 重试恢复，若置该态会让状态机永卡、LED 永久误报快闪。
        ESP_LOGE(TAG, "按钮初始化失败 ret=%d，按钮功能不可用，设备降级为纯 BLE 功率计", ret);
    }

    ble_bikepower_set_erg_callback(on_erg_target);
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
    app_evt_t e;

    while (1) {
        // 消费按钮/事件队列（非阻塞批量）
        while (xQueueReceive(s_app_queue, &e, 0) == pdTRUE) {
            app_state_handle_event(&e);
        }
        // 处理待处理的ERG目标：关中断原子复制目标+清标志，避免竞态丢新目标（单核C3足够）
        int erg_target = 0;
        bool has_erg = false;
        portDISABLE_INTERRUPTS();
        if (s_has_pending_erg) {
            erg_target = s_pending_erg_target;
            has_erg = true;
            s_has_pending_erg = false;
        }
        portENABLE_INTERRUPTS();
        if (has_erg) {
            app_evt_t e = { .type = APP_EVT_ERG_TARGET, .data = erg_target };
            app_state_handle_event(&e);
        }

        // WiFi 配网模式：BLE 已停用，主循环只喂狗等待重启恢复
        if (app_state_get() == ST_WIFI_CONFIG) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(HR_LOOP_INTERVAL_MS));
            continue;
        }

        // BLE 未就绪：周期重试，跳过 BLE 调用，避免假运行
        if (!ble_bikepower_is_ready()) {
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

        // 轮询 BLE 连接变化并投递事件
        int conn = ble_bikepower_conn_count();
        if (conn != last_conn) {
            last_conn = conn;
            app_evt_t ce = { .type = APP_EVT_CONN_CHANGED, .data = conn };
            app_state_handle_event(&ce);
        }

        app_state_tick();

        snapshot_t s = power_engine_snapshot(&s_engine);
        ble_bikepower_set_data((uint16_t)s.power, (uint8_t)s.heartrate, (uint16_t)s.cadence);

        if (++tick >= 200) {  // 每 10s 一次心跳日志
            tick = 0;
            uint32_t heap = esp_get_free_heap_size();
            ESP_LOGI(TAG, "心跳 堆=%lu p=%dW hr=%d c=%d mode=%d conn=%d state=%d",
                     (unsigned long)heap,
                     s.power, s.heartrate, s.cadence, s.mode,
                     conn, app_state_get());

            if (heap < HEAP_CRIT_BYTES) {
                ESP_LOGE(TAG, "空闲堆严重不足 %lu < %d，存在崩溃风险",
                         (unsigned long)heap, HEAP_CRIT_BYTES);
            } else if (heap < HEAP_WARN_BYTES) {
                ESP_LOGW(TAG, "空闲堆偏低 %lu < %d",
                         (unsigned long)heap, HEAP_WARN_BYTES);
            }

            // 配置脏标记则周期落盘（调功/改配置后断电可恢复）
            if (power_engine_is_dirty(&s_engine)) {
                if (nvs_store_save(&s_engine)) {
                    power_engine_clear_dirty(&s_engine);
                }
            }
        }

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(HR_LOOP_INTERVAL_MS));
    }
}
