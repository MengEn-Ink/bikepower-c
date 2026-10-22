# 稳定交付证据链实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use test-driven development and execute this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将真机自动测试和人工确认统一为当前固件版本可机检的交付证据，并在正式发布前阻止证据缺失、失败或过期的产物。

**Architecture:** 新增独立的 `device_acceptance.py` 管理 JSON 证据，`operation_gate.py` 只负责采集它执行的自动项，release 工具负责复制、摘要和严格校验。证据进入 release 总包和 manifest，不改变现有 10 个顶层附件。

**Tech Stack:** Python 3 标准库、JSON、现有 operation/release gate、Shell 构建脚本、Markdown。

---

## 文件职责

- `scripts/tools/device_acceptance.py`：证据 schema、初始化、单项更新、自动结果写入、普通/严格校验和摘要。
- `tests/test_device_acceptance.py`：证据模型和 CLI 的功能测试。
- `tests/operation_gate.py`：把 device 模式实际执行结果写入证据。
- `tests/test_operation_gate_evidence.py`：验证 PASS、FAIL、SKIP 映射及人工记录保留。
- `scripts/build_all.sh`：可选复制证据到 `build/release/evidence/` 并纳入 manifest/zip。
- `scripts/tools/generate_release_docs.py`：把证据状态写入 `test-report.md`。
- `scripts/tools/check_release_artifacts.py`：可选或强制校验 release 内证据。
- `scripts/tools/publish_gitee_release.py`：正式发布必须启用严格证据检查。
- `tests/test_release_artifacts.py`：覆盖有证据、缺证据、Git ID 不一致和 P0 未通过。
- `tests/check_release_gate.py`：锁定发布入口接线。
- `scripts/tools/quality_gate.py`：聚合新增功能测试。
- `docs/dev/05-operation-test-gate.md`：补充结构化人工门禁记录方法。
- `docs/dev/03-test-coverage-analysis.md`、`tests/README.md`：补充测试入口与边界。
- `PROGRESS.md`、`CHANGELOG.md`：记录稳定交付证据链状态与实测结果。
- `docs/INDEX.md`：登记设计和计划。

## Task 1：证据模型与验证器

**Files:**
- Create: `scripts/tools/device_acceptance.py`
- Create: `tests/test_device_acceptance.py`
- Modify: `scripts/tools/quality_gate.py`

- [ ] **Step 1：先写缺失模块的失败测试**

测试创建临时 JSON，要求以下行为：

```python
record = new_record("9.9.9", "abc1234", "generic-c3", "tester", now="2026-07-22T12:00:00+08:00")
assert record["schema_version"] == 1
assert record["items"]["serial_boot"]["status"] == "NOT_RUN"
```

同时覆盖：

```python
validate_record(record, expected_version="9.9.9", expected_git_sha="abc1234", strict=False)
set_item(record, "serial_boot", "PASS", "12 秒启动日志无异常")
validate_record(record, expected_version="9.9.9", expected_git_sha="abc1234", strict=True)
```

严格模式应在其他 P0 仍为 `NOT_RUN` 时失败；版本、Git ID、未知 item、非法状态也必须失败。

- [ ] **Step 2：运行测试确认 RED**

Run:

```bash
python3 tests/test_device_acceptance.py
```

Expected: `ModuleNotFoundError` 或缺少目标 API，退出码非 0。

- [ ] **Step 3：实现最小证据 API 和 CLI**

CLI 固定提供：

```text
device_acceptance.py init --board BOARD [--operator NAME] [--file PATH]
device_acceptance.py set --item ID --status PASS|FAIL|NOT_RUN --evidence TEXT [--file PATH]
device_acceptance.py validate [--strict] [--file PATH]
device_acceptance.py summary [--file PATH]
```

默认从 `main/bikepower_config.h` 和 `git rev-parse --short HEAD` 读取版本与 Git ID。写文件使用临时文件加 `Path.replace()`，避免中途终止留下半截 JSON。

- [ ] **Step 4：运行测试确认 GREEN**

Run:

```bash
python3 tests/test_device_acceptance.py
```

Expected: 所有证据模型场景通过。

- [ ] **Step 5：接入聚合门禁**

在 `scripts/tools/quality_gate.py` 增加：

```text
python3 tests/test_device_acceptance.py
```

Run:

```bash
python3 tests/test_device_acceptance.py
python3 scripts/tools/quality_gate.py
```

Expected: 两条命令退出码均为 0。

## Task 2：device 门禁采集

**Files:**
- Create: `tests/test_operation_gate_evidence.py`
- Modify: `tests/operation_gate.py`
- Modify: `scripts/tools/quality_gate.py`

- [ ] **Step 1：写失败测试**

测试构造临时证据并调用可测试的结果记录函数，验证：

```python
record_gate_result(path, "serial_boot", rc=0, output="boot ok", required=True)
record_gate_result(path, "ble_cycling", rc=1, output="timeout", required=True)
```

预期 `serial_boot=PASS`、`ble_cycling=FAIL`；未执行的 portal 项保持 `NOT_RUN`，已有 `led_patterns=PASS` 不得被覆盖。

- [ ] **Step 2：运行测试确认 RED**

Run:

```bash
python3 tests/test_operation_gate_evidence.py
```

Expected: 缺少结果记录接口，退出码非 0。

- [ ] **Step 3：实现采集参数与映射**

为 device 模式增加：

```text
--board generic-c3|m5stamp-c3
--operator NAME
--evidence PATH
```

未传 `--board` 时不得新建证据；如果证据已存在，可继续写入。每个 `run_step` 显式绑定 acceptance item，失败也必须落盘。

- [ ] **Step 4：验证采集**

Run:

```bash
python3 tests/test_operation_gate_evidence.py
python3 tests/operation_gate.py --help
```

Expected: 功能测试通过，帮助中显示新增参数。

## Task 3：release 收录和严格校验

**Files:**
- Modify: `scripts/tools/generate_release_docs.py`
- Modify: `scripts/tools/check_release_artifacts.py`
- Modify: `scripts/tools/publish_gitee_release.py`
- Modify: `scripts/build_all.sh`
- Modify: `tests/test_release_artifacts.py`
- Modify: `tests/check_release_gate.py`

- [ ] **Step 1：扩展 release 失败测试**

在 release 样例中加入：

```text
evidence/device-acceptance-generic-c3.json
evidence/device-acceptance-m5stamp-c3.json
```

测试四个场景：

1. 证据完整且 P0 全部 PASS，`require_device_evidence=True` 通过。
2. 证据缺失时严格模式失败。
3. 证据 Git ID 与产物不一致时失败。
4. 任一 P0 为 `NOT_RUN` 或 `FAIL` 时失败。

- [ ] **Step 2：运行确认 RED**

Run:

```bash
python3 tests/test_release_artifacts.py
```

Expected: 新增严格证据参数或行为尚不存在，退出码非 0。

- [ ] **Step 3：实现 release 集成**

具体规则：

- `build_all.sh` 按目标板型查找 `tests/reports/device-acceptance-<板型>.json`，复制到 release `evidence/`。
- `manifest.txt` 和 zip 收录证据相对路径。
- `generate_release_docs.py` 读取证据并列出 P0/P1 通过、失败、未执行数量。
- `check_release_artifacts.py` 增加 `--require-device-evidence`。
- `publish_gitee_release.py` 始终以严格模式调用 release gate。
- 普通 release gate 不要求证据，以兼容本地构建和历史产物。

- [ ] **Step 4：运行 release 功能测试**

Run:

```bash
python3 tests/test_release_artifacts.py
python3 tests/check_release_gate.py
./scripts/build_all.sh --dry-run
```

Expected: 全部通过；dry-run 说明证据的可选收录与正式发布要求。

## Task 4：文档、门禁和首份证据

**Files:**
- Modify: `docs/dev/05-operation-test-gate.md`
- Modify: `docs/dev/03-test-coverage-analysis.md`
- Modify: `tests/README.md`
- Modify: `docs/INDEX.md`
- Modify: `PROGRESS.md`
- Modify: `CHANGELOG.md`

- [ ] **Step 1：同步使用说明**

文档必须写清：

- 自动测试不替代按钮、LED 和手机弹窗的人工观察。
- `NOT_RUN` 不是通过。
- 证据必须匹配当前 `FIRMWARE_VERSION` 和 Git ID。
- 本地构建可以无证据，正式发布不能无 P0 证据。
- 72h 老化和第三方 App 验证仍按真实执行结果记录。

- [ ] **Step 2：运行文档和操作矩阵门禁**

Run:

```bash
python3 tests/check_operation_coverage.py
python3 tests/check_config_consistency.py
python3 tests/check_release_gate.py
```

Expected: 全部退出码为 0。

- [ ] **Step 3：运行完整无设备门禁**

Run:

```bash
python3 scripts/tools/quality_gate.py
```

Expected: 0 fail；若外部工具缺失，只允许权威门禁已定义的环境型 warning。

- [ ] **Step 4：有硬件时生成当前版本证据**

Run:

```bash
python3 scripts/tools/device_acceptance.py init --board generic-c3 --operator "曹蒙恩"
python3 tests/operation_gate.py --level device \
  --board generic-c3 \
  --operator "曹蒙恩" \
  --port /dev/cu.usbmodem101 \
  --run-portal
python3 scripts/tools/device_acceptance.py validate
```

Expected: 自动项按真实结果写入；人工项保持 `NOT_RUN`，直到人工逐项确认。

- [ ] **Step 5：严格验收**

人工项记录完成后运行：

```bash
python3 scripts/tools/device_acceptance.py validate --strict
```

Expected: 全部 P0 为 PASS 后退出码 0；否则列出阻断项。

## Task 5：提交边界

- [ ] **Step 1：检查工作区**

Run:

```bash
git status --short
git diff --check
```

Expected: 无空白错误；`.superpowers/` 不出现在状态中。

- [ ] **Step 2：按主题分批提交**

建议提交：

```text
docs(delivery): 设计稳定交付证据链
feat(test): 新增真机验收证据工具
feat(test): 采集真机门禁执行结果
feat(release): 发布前强制校验真机证据
docs(test): 补充稳定交付验收流程
```

每个提交正文逐文件列出变更，并附：

```text
Co-authored-by: AI <ai@trae>
Model: GPT-5.6-Sol/xhigh
```

- [ ] **Step 3：用户已授权提交时推送**

Run:

```bash
git push origin main
```

Expected: 当前 HEAD 与 `origin/main` 一致。
