#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bridge_failover.h"

static bridge_config_t three_peers(void)
{
    bridge_config_t config;
    power_bridge_config_defaults(&config);
    config.source = BRIDGE_SOURCE_REAL;
    config.device_count = 3;
    for (int i = 0; i < BRIDGE_DEVICE_MAX; i++) {
        config.devices[i].bound = 1;
        config.devices[i].addr_type = i & 1;
        config.devices[i].addr[5] = (uint8_t)(0x10 + i);
        snprintf(config.devices[i].name, sizeof(config.devices[i].name),
                 "Meter-%d", i);
    }
    return config;
}

static bridge_failover_action_t exhaust_fast(bridge_failover_t *runtime,
                                             const bridge_config_t *config,
                                             const bridge_device_t *recent)
{
    bridge_failover_action_t action = bridge_failover_handle(
        runtime, config, recent, BRIDGE_FAILOVER_START, -1, 0);
    assert(action.connect);
    for (int i = 0; i < BRIDGE_FAST_ROUNDS * config->device_count; i++) {
        action = bridge_failover_handle(runtime, config, recent,
                                        BRIDGE_FAILOVER_FAILED,
                                        action.slot, (uint32_t)(i + 1) * 8000);
    }
    return action;
}

static void all_missing_falls_back_and_rotates(void)
{
    bridge_config_t config = three_peers();
    bridge_device_t recent = config.devices[1];
    bridge_failover_t runtime;
    bridge_failover_init(&runtime);

    bridge_failover_action_t action = bridge_failover_handle(
        &runtime, &config, &recent, BRIDGE_FAILOVER_START, -1, 0);
    assert(action.connect && action.slot == 1);

    const int expected[] = {2, 0, 1, 2, 0};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        action = bridge_failover_handle(&runtime, &config, &recent,
                                        BRIDGE_FAILOVER_FAILED,
                                        action.slot, (uint32_t)(i + 1) * 8000);
        assert(action.connect && action.slot == expected[i]);
    }
    action = bridge_failover_handle(&runtime, &config, &recent,
                                    BRIDGE_FAILOVER_FAILED,
                                    action.slot, 56000);
    assert(!action.connect);
    assert(runtime.state == BRIDGE_FAILOVER_FALLBACK_SIMULATION);
    assert(bridge_failover_uses_simulation(&runtime));

    action = bridge_failover_handle(&runtime, &config, &recent,
                                    BRIDGE_FAILOVER_TICK, -1, 116000);
    assert(action.connect && action.slot == 1);
    assert(runtime.state == BRIDGE_FAILOVER_BACKGROUND_PROBE);
    action = bridge_failover_handle(&runtime, &config, &recent,
                                    BRIDGE_FAILOVER_FAILED,
                                    action.slot, 124000);
    assert(!action.connect);
    assert(runtime.state == BRIDGE_FAILOVER_FALLBACK_SIMULATION);
    action = bridge_failover_handle(&runtime, &config, &recent,
                                    BRIDGE_FAILOVER_TICK, -1, 183999);
    assert(!action.connect);
    action = bridge_failover_handle(&runtime, &config, &recent,
                                    BRIDGE_FAILOVER_TICK, -1, 184000);
    assert(action.connect && action.slot == 2);
    action = bridge_failover_handle(&runtime, &config, &recent,
                                    BRIDGE_FAILOVER_FAILED,
                                    action.slot, 192000);
    assert(!action.connect);
    action = bridge_failover_handle(&runtime, &config, &recent,
                                    BRIDGE_FAILOVER_TICK, -1, 252000);
    assert(action.connect && action.slot == 0);
}

static void valid_persists_once_and_disconnect_prioritizes_current(void)
{
    bridge_config_t config = three_peers();
    bridge_device_t stale_recent = {
        .bound = 1,
        .addr_type = 0,
        .addr = {0, 0, 0, 0, 0, 0x7f},
    };
    bridge_failover_t runtime;
    bridge_failover_init(&runtime);
    bridge_failover_action_t action = bridge_failover_handle(
        &runtime, &config, &stale_recent, BRIDGE_FAILOVER_START, -1, 100);
    assert(action.connect && action.slot == 0);

    action = bridge_failover_handle(&runtime, &config, &stale_recent,
                                    BRIDGE_FAILOVER_VALID, 0, 200);
    assert(runtime.state == BRIDGE_FAILOVER_LIVE);
    assert(action.persist_recent && !action.connect);
    assert(!bridge_failover_uses_simulation(&runtime));

    action = bridge_failover_handle(&runtime, &config, &stale_recent,
                                    BRIDGE_FAILOVER_VALID, 0, 300);
    assert(!action.persist_recent);

    action = bridge_failover_handle(&runtime, &config, &config.devices[0],
                                    BRIDGE_FAILOVER_DISCONNECTED, 0, 400);
    assert(action.connect && action.slot == 0);
    assert(runtime.state == BRIDGE_FAILOVER_FAST_RECOVERY);
}

static void deadline_pause_resume_and_retry(void)
{
    bridge_config_t config = three_peers();
    bridge_failover_t runtime;
    bridge_failover_init(&runtime);
    bridge_failover_action_t action = bridge_failover_handle(
        &runtime, &config, NULL, BRIDGE_FAILOVER_START, -1, 0);
    assert(action.connect && action.slot == 0);
    action = bridge_failover_handle(&runtime, &config, NULL,
                                    BRIDGE_FAILOVER_TICK, 0,
                                    BRIDGE_FAST_WINDOW_MS);
    assert(action.stop);
    assert(runtime.state == BRIDGE_FAILOVER_FALLBACK_SIMULATION);

    action = bridge_failover_handle(&runtime, &config, NULL,
                                    BRIDGE_FAILOVER_PAUSE, -1, 61000);
    assert(action.stop && runtime.state == BRIDGE_FAILOVER_PAUSED);
    assert(bridge_failover_uses_simulation(&runtime));
    action = bridge_failover_handle(&runtime, &config, &config.devices[2],
                                    BRIDGE_FAILOVER_RESUME, -1, 62000);
    assert(action.connect && action.slot == 2);

    action = bridge_failover_handle(&runtime, &config, NULL,
                                    BRIDGE_FAILOVER_PAUSE, action.slot, 63000);
    assert(action.stop);
    action = bridge_failover_handle(&runtime, &config, NULL,
                                    BRIDGE_FAILOVER_RETRY_NOW, -1, 64000);
    assert(action.connect && action.slot == 0);
}

static void deleted_device_and_disabled_config_are_safe(void)
{
    bridge_config_t config = three_peers();
    bridge_device_t recent = config.devices[1];
    bridge_failover_t runtime;
    bridge_failover_init(&runtime);
    bridge_failover_action_t action = exhaust_fast(&runtime, &config, &recent);
    assert(runtime.state == BRIDGE_FAILOVER_FALLBACK_SIMULATION);

    bridge_config_t reduced;
    power_bridge_config_defaults(&reduced);
    reduced.source = BRIDGE_SOURCE_REAL;
    reduced.device_count = 1;
    reduced.devices[0] = config.devices[2];
    action = bridge_failover_handle(&runtime, &reduced, &recent,
                                    BRIDGE_FAILOVER_TICK, -1,
                                    runtime.next_probe_ms);
    assert(action.connect && action.slot == 0);

    reduced.source = BRIDGE_SOURCE_SIMULATED;
    action = bridge_failover_handle(&runtime, &reduced, NULL,
                                    BRIDGE_FAILOVER_TICK, action.slot,
                                    runtime.next_probe_ms + 1);
    assert(action.stop);
    assert(runtime.state == BRIDGE_FAILOVER_DISABLED);
    assert(bridge_failover_uses_simulation(&runtime));
}

static void deadlines_survive_uint32_wrap(void)
{
    bridge_config_t config = three_peers();
    bridge_failover_t runtime;
    bridge_failover_init(&runtime);
    uint32_t start = UINT32_MAX - 30000u;
    bridge_failover_action_t action = bridge_failover_handle(
        &runtime, &config, NULL, BRIDGE_FAILOVER_START, -1, start);
    assert(action.connect);
    action = bridge_failover_handle(&runtime, &config, NULL,
                                    BRIDGE_FAILOVER_TICK, action.slot,
                                    start + BRIDGE_FAST_WINDOW_MS - 1u);
    assert(!action.stop);
    assert(runtime.state == BRIDGE_FAILOVER_FAST_RECOVERY);
    action = bridge_failover_handle(&runtime, &config, NULL,
                                    BRIDGE_FAILOVER_TICK, runtime.active_slot,
                                    start + BRIDGE_FAST_WINDOW_MS);
    assert(action.stop);
    assert(runtime.state == BRIDGE_FAILOVER_FALLBACK_SIMULATION);
}

int main(void)
{
    all_missing_falls_back_and_rotates();
    valid_persists_once_and_disconnect_prioritizes_current();
    deadline_pause_resume_and_retry();
    deleted_device_and_disabled_config_are_safe();
    deadlines_survive_uint32_wrap();
    puts("桥接主备状态机：快速恢复、模拟降级、轮转、暂停与回绕通过");
    return 0;
}
