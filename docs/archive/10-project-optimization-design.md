# Project Optimization Design

> Status: historical planning archive. The executed outcomes have been folded into current code, gates, `CHANGELOG.md`, and `docs/dev/01-technical-reference.md`. Keep this file only for traceability.

## Goal

Improve BikePower C project quality by fixing rule drift, strengthening release gates, synchronizing documents, and reducing low-risk architecture and robustness issues without changing core BLE riding behavior.

## Scope

This optimization is split into independent batches:

1. Planning documents.
2. Hard constraints and static gates.
3. Documentation synchronization.
4. Architecture boundary cleanup.
5. Robustness fixes for bounded waits and HTTP body reads.
6. Optional larger async BLE scan/analyze redesign after the low-risk batches pass.

The first five batches are in scope for the initial execution loop. The async BLE job redesign is intentionally deferred unless the earlier batches are clean because it changes API behavior and requires stronger portal testing.

## Problems

### Coex Configuration Drift

Project rules and runtime comments state that WiFi/BLE coexistence depends on `CONFIG_ESP_COEX_SW_COEXIST_ENABLE`, but the current config files do not explicitly lock this option. This can make the BLE-online portal guarantee ambiguous.

### Heap Health Drift

Project governance requires warning when free heap drops below 80 KB, while current runtime constants and gates accept 40 KB warning and 20 KB critical thresholds. This delays early detection during WiFi/BLE coexistence.

### Documentation Drift

`PROGRESS.md`, `README.md`, `docs/user/01-quickstart.md`, and `docs/dev/01-technical-reference.md` contain stale values for firmware validation, NVS schema, host test assertion counts, HTTPD stack size, Captive Portal redirect status, LED state, and configuration fields.

### Gate Gaps

Existing checks pass but do not lock coex, the 80 KB health line, exact HTTPD stack size, or several high-value documentation facts.

### Architecture Boundary Issues

`radio_ctrl.h` exposes lower-level SoftAP helpers and includes an unused WiFi driver header. `wifi_portal.c` includes `radio_ctrl.h` even though the intended dependency direction is `radio_ctrl -> wifi_portal`.

### Robustness Issues

Several blocking paths use unbounded waits or fragile receive behavior:

- `recv_body()` treats any `httpd_req_recv()` timeout as fatal.
- `ble_bikepower_stop()` waits indefinitely for the notification lock.
- BLE scanner callbacks use `portMAX_DELAY` while running in NimBLE callback context.

## Approach

Use small, testable batches. Each batch must run targeted checks, inspect `git diff --name-status`, commit only its files, and then continue. Avoid broad refactors until the hard constraints and documentation gates are clean.

## Batch Design

### Batch 1: Planning

Add this design and the implementation plan. Test by checking formatting and git diff. Commit as documentation only.

### Batch 2: Hard Constraints And Gates

Modify:

- `sdkconfig.defaults`
- `main/bikepower_config.h`
- `tests/check_config_consistency.py`

Expected behavior:

- Coex is explicitly enabled.
- `HEAP_WARN_BYTES` is 81920.
- `HEAP_CRIT_BYTES` remains 20480.
- `TASK_STACK_HTTPD` is exactly 12288.
- Static checks verify docs and code remain synchronized.

### Batch 3: Documentation Synchronization

Modify:

- `README.md`
- `PROGRESS.md`
- `docs/user/01-quickstart.md`
- `docs/dev/01-technical-reference.md`
- `docs/dev/03-test-coverage-analysis.md` if gate expectations require it.

Expected behavior:

- User-facing docs match current constants and implementation.
- Progress notes distinguish old v4.0.4 recorded hardware validation from current v4.0.6 status.
- NVS schema and key lists match `CONFIG_VERSION=10`.
- Captive Portal redirect status is consistently documented as 303.

### Batch 4: Architecture Boundary Cleanup

Modify:

- `main/radio_ctrl.h`
- `main/radio_ctrl.c`
- `main/wifi_portal.c`

Expected behavior:

- Low-level SoftAP helpers become private to `radio_ctrl.c`.
- `radio_ctrl.h` no longer includes unused `esp_wifi.h`.
- `wifi_portal.c` no longer includes `radio_ctrl.h`.
- Runtime behavior stays unchanged.

### Batch 5: Bounded Wait Robustness

Modify:

- `main/wifi_portal.c`
- `main/ble_bikepower.c`
- `main/ble_scanner.c`
- `main/bikepower_config.h` if a new bounded wait constant is needed.

Expected behavior:

- HTTP request body reads retry socket timeouts a small bounded number of times.
- BLE stop notification lock wait is finite.
- NimBLE scanner callbacks use short waits and drop optional scan data if the lock is busy.

## Testing Strategy

Run per batch:

- `python3 tests/check_config_consistency.py`
- `make -C test clean && make -C test && ./test/build/test_domain`
- `python3 scripts/tools/quality_gate.py` when the batch affects gates, code, or docs.

Run before final handoff:

- `git status --short`
- `git log --oneline -5`

Run or explicitly skip with reason:

- `idf.py build`, depending on ESP-IDF availability.
- Real-device flash and BLE/portal tests, because coex and portal behavior require hardware confirmation.

## Risks

- Enabling coex in defaults may reveal existing local `sdkconfig` drift. If build fails due to unavailable Kconfig, the fallback is to remove the setting and document the exact IDF-supported option.
- Raising heap warning threshold may increase warning logs during WiFi/BLE coexistence. This is intentional and aligns with the project health rule.
- Bounded lock waits may drop optional scan entries under contention. This is preferable to blocking NimBLE host callbacks.
