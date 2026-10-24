# 稳定交付证据链设计

> 状态：已确认实施。优先补齐真机验收、人工确认与正式发布之间的证据链，不改变 BLE、配网或训练业务行为。

## 一、目标

当前项目的 host、Mock、静态检查、ESP-IDF 构建和 release 产物门禁已经较完整，主要交付缺口是：

- `tests/operation_gate.py --level device` 能运行串口、BLE 和配网测试，但结果只存在 Markdown 报告中。
- 按钮、LED、手机 Captive Portal 和长时间老化等人工项只能显示为 `SKIP`，没有统一的结构化记录。
- `build/release/test-report.md` 不读取真机门禁结果。
- 旧发布入口只验证附件、SHA256、manifest、Git 状态，无法阻止缺少当前版本真机证据的固件发布。

本轮目标是建立一条可验证的交付链：

```text
自动真机测试 + 人工确认
  -> device-acceptance-<板型>.json
  -> 校验版本 / Git ID / 板型 / 必测项
  -> 写入 release 总包、manifest 和 test-report
  -> GitHub 发布前严格阻断
```

## 二、范围

### 本轮包含

1. 定义稳定、可读、可机检的真机验收证据格式。
2. 提供初始化、更新和校验证据的命令行工具。
3. 让 `operation_gate.py --level device` 输出自动测试结果到证据文件。
4. 让 release 构建按需收录证据，并在测试报告中生成摘要。
5. 让正式 GitHub 发布强制要求当前 Git 版本的完整 P0 证据。
6. 更新操作矩阵、测试说明、进度与发布文档。

### 本轮不包含

- 不改变 BLE GATT、广播、通知或 FTMS 控制逻辑。
- 不改变 WiFi/BLE 共存和 `radio_ctrl` 边界。
- 不尝试自动判断 LED 颜色、实体按钮手感或系统 Captive Portal 弹窗。
- 不恢复设备内 OTA、常驻 WiFi、MQTT 或 Matter。
- 不改变当前 GitHub Release 的 10 个顶层上传附件约定。

## 三、证据模型

默认本地证据文件：

```text
tests/reports/device-acceptance-generic-c3.json
tests/reports/device-acceptance-m5stamp-c3.json
```

release 总包内文件：

```text
evidence/device-acceptance-generic-c3.json
evidence/device-acceptance-m5stamp-c3.json
```

核心字段：

| 字段 | 含义 |
|------|------|
| `schema_version` | 证据格式版本，首版固定为 `1` |
| `firmware_version` | 必须等于 `FIRMWARE_VERSION` |
| `git_sha` | 必须等于当前 HEAD 短 Git ID |
| `board` | `generic-c3` 或 `m5stamp-c3` |
| `created_at` / `updated_at` | ISO 8601 时间 |
| `operator` | 验收执行人或设备 |
| `items` | 按固定 ID 记录状态、时间和证据 |

状态限定为：

- `PASS`：已执行且满足通过标准。
- `FAIL`：已执行但不满足通过标准。
- `NOT_RUN`：尚未执行或当前环境无法执行。

每个项目保留简短 `evidence` 文本，用于记录命令、日志路径、手机系统、App、运行时长或观察结果。工具不接受把 `NOT_RUN` 伪装成通过，也不允许严格模式存在 `FAIL` 或必测项 `NOT_RUN`。

## 四、验收项目

### P0 自动项

| ID | 项目 | 来源 |
|----|------|------|
| `serial_boot` | ROM/复位启动标志完整，且无 panic、WDT、Backtrace、coredump | `serial_capture.py --check-boot` |
| `ble_cycling` | 骑行 BLE 服务、通知、控制点 | `ble_selftest.py` |
| `ble_reconnect` | 连接、断开、重新广播 | `stage3_conn_test.py` |
| `soak_5min` | 5 分钟反复连接/通知无失败、无断流 | `device_soak_5min.py` |

### P1 自动项（需切换测试机网络，不阻塞发版）

| ID | 项目 | 来源 |
|----|------|------|
| `ble_running` | 跑步 profile、RSC/FTMS 数据 | `device_profile_harness.py --profile running` |
| `portal_http` | 配网门户 API、续期、关闭 | `stage5_portal_test.py` |
| `broadcast_mask` | 广播开关真实影响通知数据面 | `ble_broadcast_monitor.py` |

### P1 人工与兼容项（不阻塞发版）

| ID | 项目 | 最低证据 |
|----|------|----------|
| `button_short_long` | 短按、长按连续调功 | 板型、模式、步进和观察结果 |
| `wifi_confirm` | 长按二次确认进入配网，超时取消 | LED 状态和串口关键日志 |
| `led_patterns` | 待连接、连接、确认、WiFi、错误灯效 | 板型和各状态观察结果 |
| `portal_mobile` | 手机连接 AP，自动弹窗或手动入口可用 | 系统版本、浏览器和结果 |
| `portal_exit` | 保存关闭与 180 秒超时均回 BLE | 串口日志和 BLE 连续性 |
| `zwift_cycling` | Zwift 骑行 fixed/ERG 进入退出 | 第三方平台兼容证据 |
| `running_apps` | Zwift/MyWhoosh 跑步识别 | 第三方平台兼容证据 |

严格模式只要求 P0 自动项全部通过（串口启动、骑行 BLE、重连、5 分钟耐久）；P1 允许 `NOT_RUN`，但必须在 `test-report.md` 明示，不能显示为已验证。跑步 profile、配网门户和广播项因需要把测试机切到设备热点，归入 P1；原 72 小时老化不再是发版阻塞项。

## 五、命令行交互

初始化当前版本证据：

```bash
python3 scripts/tools/device_acceptance.py init \
  --board generic-c3 \
  --operator "曹蒙恩"
```

记录人工结果：

```bash
python3 scripts/tools/device_acceptance.py set \
  --item led_patterns \
  --status PASS \
  --evidence "generic-c3: 待连接慢闪、连接常亮、确认快闪"
```

开发期检查结构和当前版本：

```bash
python3 scripts/tools/device_acceptance.py validate
```

正式发布严格检查：

```bash
python3 scripts/tools/device_acceptance.py validate --strict
```

`operation_gate.py --level device --board <板型>` 复用对应板型文件，只更新它实际执行的自动项，不覆盖已有人工记录。

## 六、Release 集成

`build_all.sh` 的本地构建仍允许没有真机证据，以支持无设备环境生成预览产物；若默认证据文件存在，则复制到 `build/release/evidence/`，写入 manifest、zip 和 `test-report.md`。

正式发布入口 `publish_github_release.py` 必须：

1. 使用 release 目录内 generic-c3 与 m5stamp-c3 两份证据。
2. 严格校验每份证据的版本、Git ID、板型和 P0 状态。
3. 校验证据已写入 manifest 和 zip。
4. 校验失败时在调用 GitHub CLI 前退出。

因此保持 10 个顶层附件不变，证据作为总包内部可追溯文件随发布保存。

## 七、失败与兼容策略

- 旧 release 产物可继续使用普通 `check_release_artifacts.py` 离线检查。
- 正式双板发布使用 `--require-device-evidence`，缺任一板型证据都会明确失败。
- 自动测试失败写入 `FAIL`，后续重跑通过后才能更新为 `PASS`。
- 自动测试因环境缺失被跳过时保持 `NOT_RUN`。
- 证据 Git ID 与当前 HEAD 不一致时必须重新烧录和验证，禁止手工改 Git ID 复用旧结果。
- 证据文件不保存密钥、WiFi 密码、MAC 全量或用户隐私。

## 八、分批实施

1. **证据模型与验证器**：先用功能测试锁定格式、版本匹配和严格模式。
2. **device 门禁采集**：将自动结果写入同一证据，保留人工项。
3. **release 强校验**：收录证据、生成摘要、发布前阻断。
4. **文档与真机执行**：更新矩阵并在当前开发板上生成首份真实证据。

每一批独立通过目标测试和 `quality_gate.py` 后再进入下一批。
