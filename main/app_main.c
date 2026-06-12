/**
 * BikePower C — 应用入口
 * 运行环境：ESP-IDF v5.5.1 + NimBLE，目标芯片 ESP32-C3
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

static const char *TAG = "APP";

static power_engine_t s_engine;
static QueueHandle_t s_app_queue;

// FTMS 控制点 ERG 回调（NimBLE host 任务上下文）
static void on_erg_target(int target_power)
{
    power_engine_set_erg_target(&s_engine, target_power);
}

// WiFi 配网触发（app_state 二次确认后回调）：进入射频独占配网模式
static void on_wifi_trigger(void)
{
    radio_ctrl_enter_wifi(&s_engine);
}

void app_main(void)
{
    ESP_LOGI(TAG, "BikePower C 启动 v%s (target=esp32c3, idf=v5.5.1)", FIRMWARE_VERSION);

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

    led_init();
    app_state_init(&s_engine);
    app_state_set_wifi_trigger(on_wifi_trigger);
    button_init(s_app_queue);

    ble_bikepower_set_erg_callback(on_erg_target);
    ble_bikepower_set_device_name(s_engine.device_name);
    ble_bikepower_start();

    // 看门狗：主循环每拍喂狗（idf 默认已为 IDLE 任务注册 TWDT，这里订阅当前任务）
    esp_task_wdt_add(NULL);

    int tick = 0;
    int last_conn = 0;
    app_evt_t e;

    while (1) {
        // 消费按钮/事件队列（非阻塞批量）
        while (xQueueReceive(s_app_queue, &e, 0) == pdTRUE) {
            app_state_handle_event(&e);
        }

        // WiFi 配网模式：BLE 已停用，主循环只喂狗等待重启恢复
        if (app_state_get() == ST_WIFI_CONFIG) {
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
            ESP_LOGI(TAG, "心跳 堆=%lu p=%dW hr=%d c=%d mode=%d conn=%d state=%d",
                     (unsigned long)esp_get_free_heap_size(),
                     s.power, s.heartrate, s.cadence, s.mode,
                     conn, app_state_get());

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
