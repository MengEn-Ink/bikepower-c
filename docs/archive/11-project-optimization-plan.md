# Project Optimization Implementation Plan

> Status: historical execution-plan archive. The live project now uses `docs/user`, `docs/dev`, and `docs/archive`; the old `docs/superpowers` working directory has been retired.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix project optimization issues in small tested commits covering hard constraints, gates, docs, architecture boundaries, and bounded waits.

**Architecture:** Keep runtime architecture intact. First lock project invariants with constants and static gates, then synchronize docs, then make low-risk C boundary and robustness fixes. Defer large async BLE scan redesign to a later plan.

**Tech Stack:** ESP-IDF v5.5.4, ESP32-C3, NimBLE, C, Python static gates, Make host tests, Markdown docs.

---

## File Map

- `docs/archive/10-project-optimization-design.md`: archived design for this optimization round.
- `docs/archive/11-project-optimization-plan.md`: archived execution plan and test/commit loop.
- `sdkconfig.defaults`: build-time hard constraints, including WiFi/BLE coex.
- `main/bikepower_config.h`: runtime constants for heap thresholds, task stacks, waits, and firmware metadata.
- `tests/check_config_consistency.py`: static gate for constants and documentation synchronization.
- `README.md`: user-facing overview and quick behavior facts.
- `PROGRESS.md`: authoritative progress and validation status.
- `docs/user/01-quickstart.md`: user quickstart facts.
- `docs/dev/01-technical-reference.md`: detailed implementation reference.
- `main/radio_ctrl.h`: public RF control API.
- `main/radio_ctrl.c`: private SoftAP lifecycle helpers and public WiFi enter/exit.
- `main/wifi_portal.c`: HTTP body reading and portal implementation.
- `main/ble_bikepower.c`: BLE service lifecycle and stop path.
- `main/ble_scanner.c`: BLE scan/read/analyze callbacks and locks.

## Task 1: Planning Commit

**Files:**
- Create: `docs/archive/10-project-optimization-design.md`
- Create: `docs/archive/11-project-optimization-plan.md`

- [ ] **Step 1: Inspect diff**

Run: `git diff --name-status`

Expected: only the two planning files are listed.

- [ ] **Step 2: Run lightweight validation**

Run: `python3 tests/check_config_consistency.py`

Expected: `结果: 204 通过, 0 失败` or higher pass count with zero failures.

- [ ] **Step 3: Commit**

Run:

```bash
git add docs/archive/10-project-optimization-design.md docs/archive/11-project-optimization-plan.md
git commit -m "docs: add project optimization plan"
```

Expected: one docs-only commit.

## Task 2: Hard Constraints And Gates

**Files:**
- Modify: `sdkconfig.defaults`
- Modify: `main/bikepower_config.h`
- Modify: `tests/check_config_consistency.py`
- Modify: docs only if gate wording requires exact synchronization.

- [ ] **Step 1: Add failing/static expectations**

Update `tests/check_config_consistency.py` so it checks:

```python
check("CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y" in sdkconfig_defaults,
      "sdkconfig.defaults 启用 CONFIG_ESP_COEX_SW_COEXIST_ENABLE")
check(heap_warn == 81920, f"HEAP_WARN_BYTES = 81920 (实际={heap_warn})")
check(heap_crit == 20480, f"HEAP_CRIT_BYTES = 20480 (实际={heap_crit})")
httpd_stack = extract_int(config_src, "TASK_STACK_HTTPD")
check(httpd_stack == 12288, f"TASK_STACK_HTTPD = 12288 (实际={httpd_stack})")
```

- [ ] **Step 2: Run gate and confirm failure**

Run: `python3 tests/check_config_consistency.py`

Expected: fails before implementation because coex and heap warning are not aligned.

- [ ] **Step 3: Implement constants**

Set:

```c
#define TASK_STACK_HTTPD        12288
#define HEAP_WARN_BYTES      81920
#define HEAP_CRIT_BYTES      20480
```

Add to `sdkconfig.defaults`:

```ini
CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y
```

- [ ] **Step 4: Run targeted tests**

Run:

```bash
python3 tests/check_config_consistency.py
make -C test clean && make -C test && ./test/build/test_domain
```

Expected: both pass.

- [ ] **Step 5: Inspect and commit**

Run:

```bash
git diff --name-status
git add sdkconfig.defaults main/bikepower_config.h tests/check_config_consistency.py
git commit -m "test: lock radio and heap constraints"
```

Expected: one commit limited to hard constraints and gates.

## Task 3: Documentation Synchronization

**Files:**
- Modify: `README.md`
- Modify: `PROGRESS.md`
- Modify: `docs/user/01-quickstart.md`
- Modify: `docs/dev/01-technical-reference.md`
- Modify: `docs/dev/03-test-coverage-analysis.md` if version or gate text is stale.

- [ ] **Step 1: Update stale facts**

Synchronize these facts:

```text
Current firmware: v4.0.6
Recorded generic-c3 hardware validation: v4.0.4, current v4.0.6 needs rerun if not revalidated.
NVS schema: CONFIG_VERSION 10
Captive Portal redirect: 303 See Other
HTTPD stack: TASK_STACK_HTTPD 12288
Heap warning/critical: 80KB / 20KB
Default WiFi password: 12345678
M5Stamp WiFi LED state: cyan slow blink
Button power step: configurable, default 20W, range 5-50W
```

- [ ] **Step 2: Run doc gate**

Run: `python3 tests/check_config_consistency.py`

Expected: pass with zero failures.

- [ ] **Step 3: Inspect and commit**

Run:

```bash
git diff --name-status
git add README.md PROGRESS.md docs/user/01-quickstart.md docs/dev/01-technical-reference.md docs/dev/03-test-coverage-analysis.md
git commit -m "docs: synchronize project reference facts"
```

Expected: one docs-focused commit.

## Task 4: Architecture Boundary Cleanup

**Files:**
- Modify: `main/radio_ctrl.h`
- Modify: `main/radio_ctrl.c`
- Modify: `main/wifi_portal.c`

- [ ] **Step 1: Make SoftAP helpers private**

In `main/radio_ctrl.c`, change helper declarations and definitions to:

```c
static esp_err_t radio_ctrl_start_wifi_ap(void);
static void radio_ctrl_stop_wifi_ap(void);
```

Remove those declarations from `main/radio_ctrl.h`.

- [ ] **Step 2: Remove unused include coupling**

Remove from `main/radio_ctrl.h`:

```c
#include "esp_wifi.h"
```

Remove from `main/wifi_portal.c`:

```c
#include "radio_ctrl.h"
```

- [ ] **Step 3: Run tests**

Run:

```bash
python3 tests/check_config_consistency.py
make -C test clean && make -C test && ./test/build/test_domain
```

Expected: all pass.

- [ ] **Step 4: Commit**

Run:

```bash
git add main/radio_ctrl.h main/radio_ctrl.c main/wifi_portal.c
git commit -m "refactor: tighten radio control boundaries"
```

Expected: one behavior-preserving refactor commit.

## Task 5: Bounded Wait Robustness

**Files:**
- Modify: `main/bikepower_config.h`
- Modify: `main/wifi_portal.c`
- Modify: `main/ble_bikepower.c`
- Modify: `main/ble_scanner.c`

- [ ] **Step 1: Add constants**

Add constants:

```c
#define HTTP_RECV_TIMEOUT_RETRIES 3
#define BLE_STOP_LOCK_WAIT_MS     100
#define BLE_SCAN_LOCK_WAIT_MS     5
```

- [ ] **Step 2: Improve HTTP body receive**

Change `recv_body()` so `HTTPD_SOCK_ERR_TIMEOUT` retries up to `HTTP_RECV_TIMEOUT_RETRIES`; any other negative return still fails and frees the buffer.

- [ ] **Step 3: Bound BLE stop lock**

Change `ble_bikepower_stop()` from `portMAX_DELAY` to `pdMS_TO_TICKS(BLE_STOP_LOCK_WAIT_MS)`. On timeout, log an error and return `false`.

- [ ] **Step 4: Bound BLE scanner callback locks**

Change NimBLE callback lock waits from `portMAX_DELAY` to `pdMS_TO_TICKS(BLE_SCAN_LOCK_WAIT_MS)`. If the lock is busy, drop the optional scan update and return safely.

- [ ] **Step 5: Run tests and gate**

Run:

```bash
python3 tests/check_config_consistency.py
make -C test clean && make -C test && ./test/build/test_domain
python3 scripts/tools/quality_gate.py
```

Expected: static checks and host tests pass. `quality_gate.py` may run build if ESP-IDF is available.

- [ ] **Step 6: Commit**

Run:

```bash
git add main/bikepower_config.h main/wifi_portal.c main/ble_bikepower.c main/ble_scanner.c
git commit -m "fix: bound portal and BLE waits"
```

Expected: one robustness commit.

## Task 6: Final Verification

**Files:**
- No planned modifications.

- [ ] **Step 1: Run final status**

Run:

```bash
git status --short
git log --oneline -5
```

Expected: clean worktree and recent commits match the batch loop.

- [ ] **Step 2: Report skipped hardware tests**

If no device test is run, report:

```text
真机测试未执行：本轮在本地完成静态门禁、host 单测和可用构建检查；WiFi/BLE coex、配网、BLE 扫描仍需烧录后验证。
```
