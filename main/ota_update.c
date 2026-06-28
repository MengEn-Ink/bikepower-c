/**
 * OTA 自检确认/回滚 + 版本比较工具
 *
 * 关键设计：
 *   1. 版本比较：语义版本 major.minor.patch 数值比较，非该格式回退 strcmp。
 *   2. 自检回滚：app_main 启动调 ota_update_check_and_confirm，10s 一次性定时器到点确认；
 *      期间若调 ota_update_mark_invalid_and_rollback 则回滚到上一可用固件。
 */
#include "ota_update.h"
#include "ota_version.h"   // 领域层语义版本比较（host 可测）
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_system.h"
#include <string.h>

static const char *TAG = "OTA";

#define OTA_SELFTEST_MS     10000

static esp_timer_handle_t s_selftest_timer = NULL;
static volatile bool s_selftest_done = false;

// ============================ 自检回滚 ============================

static void selftest_timer_cb(void *arg)
{
    (void)arg;
    if (s_selftest_done) return;
    s_selftest_done = true;
    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "新固件自检通过，已确认有效");
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGD(TAG, "非 PENDING_VERIFY，无需确认");
    } else {
        ESP_LOGW(TAG, "mark_valid ret=%d", ret);
    }
    if (s_selftest_timer) {
        esp_timer_delete(s_selftest_timer);
        s_selftest_timer = NULL;
    }
}

void ota_update_check_and_confirm(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) return;
    esp_ota_img_states_t state;
    esp_err_t ret = esp_ota_get_state_partition(running, &state);
    if (ret != ESP_OK) return;
    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGD(TAG, "运行分区 %s 状态=%d 正常启动", running->label, state);
        return;
    }

    ESP_LOGI(TAG, "新固件首次启动 PENDING_VERIFY，启动 10s 自检");
    s_selftest_done = false;
    const esp_timer_create_args_t targs = { .callback = selftest_timer_cb, .name = "ota_selftest" };
    if (esp_timer_create(&targs, &s_selftest_timer) != ESP_OK) {
        ESP_LOGE(TAG, "自检定时器创建失败，立即确认");
        esp_ota_mark_app_valid_cancel_rollback();
        return;
    }
    if (esp_timer_start_once(s_selftest_timer, OTA_SELFTEST_MS * 1000) != ESP_OK) {
        ESP_LOGE(TAG, "自检定时器启动失败，立即确认");
        esp_timer_delete(s_selftest_timer);
        s_selftest_timer = NULL;
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

void ota_update_mark_invalid_and_rollback(void)
{
    if (s_selftest_done) return;
    s_selftest_done = true;
    ESP_LOGE(TAG, "自检失败，标记无效并回滚重启");
    if (s_selftest_timer) {
        esp_timer_stop(s_selftest_timer);
        esp_timer_delete(s_selftest_timer);
        s_selftest_timer = NULL;
    }
    esp_ota_mark_app_invalid_rollback_and_reboot();
}
