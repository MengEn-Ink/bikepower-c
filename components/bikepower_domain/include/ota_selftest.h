/**
 * OTA 自检状态机纯逻辑（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/ota_update.c 抽取自检窗口状态转换逻辑，
 * 便于 host 单元测试覆盖自检开始/超时确认/手动回滚的状态迁移。
 *
 * 设计：纯函数，输入当前状态和事件，输出下一状态。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_SELFTEST_IDLE = 0,        // 未启动自检（非 PENDING_VERIFY）
    OTA_SELFTEST_PENDING,         // 自检窗口运行中
    OTA_SELFTEST_CONFIRMED,       // 已确认有效（超时或显式确认）
    OTA_SELFTEST_ROLLED_BACK,     // 已回滚（不可恢复）
} ota_selftest_state_t;

typedef enum {
    OTA_SELFTEST_EVT_START = 0,        // 启动自检窗口
    OTA_SELFTEST_EVT_TIMEOUT = 1,      // 自检窗口超时（确认有效）
    OTA_SELFTEST_EVT_MARK_INVALID = 2, // 标记无效并回滚
    OTA_SELFTEST_EVT_MARK_VALID = 3,   // 显式标记有效
} ota_selftest_event_t;

/**
 * 推进自检状态机。
 * current：当前状态。
 * event：自检事件。
 * 返回值：下一状态。
 * 特殊行为：
 *   - IDLE 收到 START → PENDING
 *   - PENDING 收到 TIMEOUT/MARK_VALID → CONFIRMED
 *   - PENDING 收到 MARK_INVALID → ROLLED_BACK
 *   - CONFIRMED/ROLLED_BACK 是终态，任何事件不变
 */
ota_selftest_state_t ota_selftest_advance(ota_selftest_state_t current, ota_selftest_event_t event);

/**
 * 判断自检窗口是否仍可被回滚。
 * state：当前状态。
 * 返回值：true 表示仍处于 PENDING，可调用 mark_invalid。
 */
bool ota_selftest_can_rollback(ota_selftest_state_t state);

/**
 * 判断是否需要启动自检窗口。
 * partition_state：ESP-IDF 分区状态（0=非 PENDING_VERIFY，1=PENDING_VERIFY）。
 * 返回值：true 表示需要启动自检。
 */
bool ota_selftest_should_start(int partition_state);

#ifdef __cplusplus
}
#endif
