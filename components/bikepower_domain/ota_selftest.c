/**
 * OTA 自检状态机纯逻辑实现（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/ota_update.c 抽取，便于 host 单元测试覆盖自检状态迁移。
 */
#include "ota_selftest.h"

ota_selftest_state_t ota_selftest_advance(ota_selftest_state_t current, ota_selftest_event_t event)
{
    switch (current) {
    case OTA_SELFTEST_IDLE:
        if (event == OTA_SELFTEST_EVT_START) return OTA_SELFTEST_PENDING;
        return current;
    case OTA_SELFTEST_PENDING:
        if (event == OTA_SELFTEST_EVT_TIMEOUT) return OTA_SELFTEST_CONFIRMED;
        if (event == OTA_SELFTEST_EVT_MARK_VALID) return OTA_SELFTEST_CONFIRMED;
        if (event == OTA_SELFTEST_EVT_MARK_INVALID) return OTA_SELFTEST_ROLLED_BACK;
        return current;
    case OTA_SELFTEST_CONFIRMED:
    case OTA_SELFTEST_ROLLED_BACK:
    default:
        return current;
    }
}

bool ota_selftest_can_rollback(ota_selftest_state_t state)
{
    return state == OTA_SELFTEST_PENDING;
}

bool ota_selftest_should_start(int partition_state)
{
    // partition_state == 1 表示 ESP_OTA_IMG_PENDING_VERIFY
    return partition_state == 1;
}
