#include <string.h>

#include "bridge_failover.h"

static bridge_failover_action_t no_action(void)
{
    bridge_failover_action_t action = {.slot = -1};
    return action;
}

static bool time_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static bool slot_valid(const bridge_config_t *config, int slot)
{
    return config && slot >= 0 && slot < config->device_count
        && slot < BRIDGE_DEVICE_MAX && config->devices[slot].bound;
}

static bool enabled(const bridge_config_t *config)
{
    return config && config->source == BRIDGE_SOURCE_REAL
        && config->device_count > 0;
}

static int preferred_slot(const bridge_config_t *config,
                          const bridge_device_t *recent, int requested)
{
    if (slot_valid(config, requested)) return requested;
    int recent_slot = power_bridge_find_device(config, recent);
    if (slot_valid(config, recent_slot)) return recent_slot;
    for (int i = 0; i < BRIDGE_DEVICE_MAX; i++) {
        if (slot_valid(config, i)) return i;
    }
    return -1;
}

static void build_order(bridge_failover_t *runtime,
                        const bridge_config_t *config,
                        const bridge_device_t *recent, int requested)
{
    runtime->order_count = 0;
    int first = preferred_slot(config, recent, requested);
    if (first < 0) return;
    for (int offset = 0; offset < BRIDGE_DEVICE_MAX; offset++) {
        int slot = (first + offset) % BRIDGE_DEVICE_MAX;
        if (slot_valid(config, slot)) {
            runtime->order[runtime->order_count++] = slot;
        }
    }
}

static bridge_failover_action_t disable(bridge_failover_t *runtime, bool stop)
{
    bridge_failover_action_t action = no_action();
    action.stop = stop;
    runtime->state = BRIDGE_FAILOVER_DISABLED;
    runtime->active_slot = -1;
    runtime->order_count = 0;
    runtime->order_index = 0;
    runtime->round = 0;
    runtime->probe_cursor = 0;
    runtime->persist_emitted = false;
    return action;
}

static bridge_failover_action_t enter_fallback(bridge_failover_t *runtime,
                                                uint32_t now, bool stop)
{
    bridge_failover_action_t action = no_action();
    action.stop = stop;
    runtime->state = BRIDGE_FAILOVER_FALLBACK_SIMULATION;
    runtime->active_slot = -1;
    runtime->probe_cursor = 0;
    runtime->next_probe_ms = now + BRIDGE_BACKGROUND_INTERVAL_MS;
    return action;
}

static bridge_failover_action_t start_fast(bridge_failover_t *runtime,
                                            const bridge_config_t *config,
                                            const bridge_device_t *recent,
                                            int requested, uint32_t now,
                                            bool stop)
{
    if (!enabled(config)) return disable(runtime, stop || runtime->active_slot >= 0);
    build_order(runtime, config, recent, requested);
    if (runtime->order_count == 0) return disable(runtime, stop || runtime->active_slot >= 0);
    bridge_failover_action_t action = no_action();
    action.stop = stop;
    action.connect = true;
    action.slot = runtime->order[0];
    runtime->state = BRIDGE_FAILOVER_FAST_RECOVERY;
    runtime->order_index = 0;
    runtime->round = 0;
    runtime->active_slot = action.slot;
    runtime->fast_deadline_ms = now + BRIDGE_FAST_WINDOW_MS;
    runtime->probe_cursor = 0;
    runtime->persist_emitted = false;
    return action;
}

static bridge_failover_action_t next_fast(bridge_failover_t *runtime,
                                           uint32_t now)
{
    runtime->order_index++;
    if (runtime->order_index >= runtime->order_count) {
        runtime->order_index = 0;
        runtime->round++;
    }
    if (runtime->round >= BRIDGE_FAST_ROUNDS) {
        return enter_fallback(runtime, now, false);
    }
    bridge_failover_action_t action = no_action();
    action.connect = true;
    action.slot = runtime->order[runtime->order_index];
    runtime->active_slot = action.slot;
    return action;
}

void bridge_failover_init(bridge_failover_t *runtime)
{
    if (!runtime) return;
    memset(runtime, 0, sizeof(*runtime));
    runtime->state = BRIDGE_FAILOVER_DISABLED;
    runtime->active_slot = -1;
}

bridge_failover_action_t bridge_failover_handle(
    bridge_failover_t *runtime, const bridge_config_t *config,
    const bridge_device_t *recent, bridge_failover_event_t event,
    int session_slot, uint32_t now)
{
    if (!runtime) return no_action();
    if (!enabled(config) && event != BRIDGE_FAILOVER_PAUSE) {
        return disable(runtime, runtime->active_slot >= 0);
    }

    if (event == BRIDGE_FAILOVER_PAUSE) {
        bridge_failover_action_t action = no_action();
        action.stop = true;
        runtime->state = BRIDGE_FAILOVER_PAUSED;
        runtime->active_slot = -1;
        return action;
    }
    if (event == BRIDGE_FAILOVER_START || event == BRIDGE_FAILOVER_RESUME) {
        return start_fast(runtime, config, recent, -1, now, false);
    }
    if (event == BRIDGE_FAILOVER_RETRY_NOW) {
        return start_fast(runtime, config, recent, -1, now,
                          runtime->active_slot >= 0);
    }
    if (event == BRIDGE_FAILOVER_DISCONNECTED) {
        int requested = slot_valid(config, session_slot)
            ? session_slot : runtime->active_slot;
        return start_fast(runtime, config, recent, requested, now, false);
    }

    if (event == BRIDGE_FAILOVER_VALID) {
        if ((runtime->state != BRIDGE_FAILOVER_FAST_RECOVERY
             && runtime->state != BRIDGE_FAILOVER_BACKGROUND_PROBE
             && runtime->state != BRIDGE_FAILOVER_LIVE)
            || !slot_valid(config, session_slot)
            || (runtime->active_slot >= 0 && session_slot != runtime->active_slot)) {
            return no_action();
        }
        bridge_failover_action_t action = no_action();
        runtime->state = BRIDGE_FAILOVER_LIVE;
        runtime->active_slot = session_slot;
        if (!runtime->persist_emitted
            && !bridge_device_equal(&config->devices[session_slot], recent)) {
            action.persist_recent = true;
            runtime->persist_emitted = true;
        }
        return action;
    }

    if (runtime->state == BRIDGE_FAILOVER_FAST_RECOVERY) {
        if (time_reached(now, runtime->fast_deadline_ms)) {
            return enter_fallback(runtime, now, event == BRIDGE_FAILOVER_TICK);
        }
        if (event == BRIDGE_FAILOVER_FAILED
            && (session_slot < 0 || session_slot == runtime->active_slot)) {
            return next_fast(runtime, now);
        }
        if (event == BRIDGE_FAILOVER_TICK
            && !slot_valid(config, runtime->active_slot)) {
            return next_fast(runtime, now);
        }
        return no_action();
    }

    if (runtime->state == BRIDGE_FAILOVER_LIVE) {
        if (event == BRIDGE_FAILOVER_FAILED
            && (session_slot < 0 || session_slot == runtime->active_slot)) {
            return start_fast(runtime, config, recent, runtime->active_slot,
                              now, false);
        }
        if (event == BRIDGE_FAILOVER_TICK
            && !slot_valid(config, runtime->active_slot)) {
            return start_fast(runtime, config, recent, -1, now, true);
        }
        return no_action();
    }

    if (runtime->state == BRIDGE_FAILOVER_BACKGROUND_PROBE) {
        if (event == BRIDGE_FAILOVER_FAILED
            && (session_slot < 0 || session_slot == runtime->active_slot)) {
            int next_cursor = runtime->order_count > 0
                ? (runtime->probe_cursor + 1) % runtime->order_count : 0;
            bridge_failover_action_t action = enter_fallback(runtime, now, false);
            runtime->probe_cursor = next_cursor;
            return action;
        }
        return no_action();
    }

    if (runtime->state == BRIDGE_FAILOVER_FALLBACK_SIMULATION
        && event == BRIDGE_FAILOVER_TICK
        && time_reached(now, runtime->next_probe_ms)) {
        int previous_cursor = runtime->probe_cursor;
        build_order(runtime, config, recent, -1);
        if (runtime->order_count == 0) return disable(runtime, false);
        runtime->probe_cursor = previous_cursor % runtime->order_count;
        bridge_failover_action_t action = no_action();
        action.connect = true;
        action.slot = runtime->order[runtime->probe_cursor];
        runtime->active_slot = action.slot;
        runtime->state = BRIDGE_FAILOVER_BACKGROUND_PROBE;
        return action;
    }

    return no_action();
}

bool bridge_failover_uses_simulation(const bridge_failover_t *runtime)
{
    if (!runtime) return true;
    return runtime->state == BRIDGE_FAILOVER_DISABLED
        || runtime->state == BRIDGE_FAILOVER_FALLBACK_SIMULATION
        || runtime->state == BRIDGE_FAILOVER_BACKGROUND_PROBE
        || runtime->state == BRIDGE_FAILOVER_PAUSED;
}
