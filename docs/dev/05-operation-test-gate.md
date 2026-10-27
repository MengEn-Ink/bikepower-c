# 操作覆盖矩阵与测试门禁

> 目标：把用户可见操作、配置保存、按钮、网页、BLE 控制点和异常稳定性全部绑定到可执行门禁。
> 原则：能自动化的必须有脚本；不能远程自动化的必须列为人工门禁，不能冒充自动通过。

## 一、执行分层

| 层级 | 用途 | 入口 |
|------|------|------|
| Host 领域层/协议 | 功率、踏频、心率、运行会话、配置 schema、BLE payload golden vectors、Config E2E 协议帧 | `make -C test run`、`python3 tests/test_ble_protocol_vectors.py`、`python3 tests/test_ble_config_e2e.py` |
| 离线 harness | 双次重连、running 四阶段、profile 全广播测试/恢复、FTMS 接纳和 WiFi done 可靠性契约 | `python3 tests/test_ble_reconnect_harness.py`、`python3 tests/test_ble_running_harness.py`、`python3 tests/test_device_profile_harness.py`、`python3 tests/check_ftms_control_contract.py`、`python3 tests/check_wifi_done_contract.py` |
| Mock 网页层 | `/api/config` 保存回显、网页核心流程、设备模板、关闭 WiFi | `python3 tests/test_portal_config_save.py`、`python3 tests/test_core_flow_endurance.py` |
| 静态门禁 | 门户结构、JS、常量、NVS、任务/WDT、板型、操作矩阵、Pages 网页刷写站点 | `python3 tests/check_operation_coverage.py`、`python3 tests/check_ble_monitor.py`、`python3 tests/test_prepare_web_flash_site.py` 等 |
| 真机自动化 | 串口启动、全量配置读写/NVS 持久化/GATT 重建、互斥 cycling/running GATT profile、FTMS ERG/跑步会话、配网门户 HTTP、5 分钟设备耐久 | `python3 tests/ble_config_e2e.py`、`python3 tests/ble_selftest.py`、`python3 tests/device_profile_harness.py`、`python3 tests/stage5_portal_test.py`、`python3 tests/device_soak_5min.py` |
| 人工门禁 | 物理按钮手感、LED 肉眼灯效、手机 Captive Portal、Zwift/MyWhoosh 真实课程（P1，不阻塞发版） | `scripts/tools/device_acceptance.py` + 本文“人工门禁记录” |

## 二、操作覆盖矩阵

| ID | 操作/风险 | 自动化门禁 | 人工门禁/说明 |
|----|-----------|------------|----------------|
| `OP-MODE-001` | 骑行 fixed/配速机器人/ERG 模式切换；切非 ERG 后旧目标不得残留 | `make -C test run`、`python3 tests/test_core_flow_endurance.py` | 真机网页切换后观察 App 数据源 |
| `OP-MODE-002` | 跑步/骑行 profile 切换；RSC 与 CPS/FTMS 服务不能混乱；测试期间强制 mask=7，结束恢复原 profile/mask 和完整 running 持久配置 | `python3 tests/test_device_profile_harness.py`、`python3 tests/device_profile_harness.py --profile running` | Zwift/MyWhoosh 跑步入口人工确认 |
| `OP-MODE-003` | ERG Stop/Pause、Reset、BLE 断开后恢复 fixed；目标不改 base power/dirty/NVS | `make -C test run`、`python3 tests/ble_selftest.py --profile cycling` | Zwift ERG 课程退出后人工调功确认 |
| `OP-SAVE-001` | 骑行数值保存：功率、踏频、心率、心率锁定、浮动、按钮步进 | `python3 tests/test_portal_config_save.py`、`python3 tests/test_core_flow_endurance.py` | 真机保存后刷新网页确认 |
| `OP-SAVE-002` | 跑步数值保存：配速、配速浮动、步频、步频浮动、心率 | `python3 tests/test_portal_config_save.py`、`make -C test run` | MyWhoosh/Zwift 跑步速度显示人工确认 |
| `OP-SAVE-003` | 用户资料与自定义机器人：体重、FTP、阈值心率、功体比上下限、昵称 | `python3 tests/test_portal_config_save.py`、`python3 tests/test_core_flow_endurance.py` | 真机网页自定义机器人 UI 人工确认 |
| `OP-SAVE-004` | BLE Config Service 五组全量读取/修改、事务错误拒绝、NVS 重启持久化和 GATT 重建；测试后恢复原配置 | `python3 tests/test_ble_config_e2e.py`、`python3 tests/ble_config_e2e.py --port <串口> --board <板型>` | Web Bluetooth 首次授权与页面表单交互仍需人工确认 |
| `OP-BTN-001` | 短按减功率、长按连续加功率、非 steady 模式拒绝按钮调功 | `make -C test run`、`python3 tests/test_core_flow_endurance.py` | 实体按钮手感和重复节拍人工确认 |
| `OP-BTN-002` | 长按 2 秒进入 WiFi 二次确认窗口，确认后触发配网 | `make -C test run`、`python3 tests/test_core_flow_endurance.py` | 实体按钮 + LED 确认窗口人工确认 |
| `OP-BTN-003` | 确认窗口超时取消，回到 BLE 活动态，不误开 WiFi | `make -C test run` | 实体按钮等待 3 秒人工确认 |
| `OP-WEB-001` | 网页保存回显与字段映射，不丢字段、不串字段 | `python3 tests/test_portal_config_save.py`、`python3 tests/check_portal_structure.py` | 手机浏览器保存流程人工确认 |
| `OP-WEB-002` | 越界/非法输入保持旧值或夹紧，不写入危险配置 | `make -C test run`、`python3 tests/test_portal_config_save.py` | 手动输入异常值抽查 |
| `OP-WEB-003` | 续期、倒计时、保存并关闭 WiFi、设备模板扫描/保存/应用/删除 | `python3 tests/test_core_flow_endurance.py`、`python3 tests/stage5_portal_test.py` | 手机连接 AP 后配网入口人工确认 |
| `OP-WEB-004` | GitHub Pages 发布 BLE Monitor；自动获取双板最新 Release、验证 SHA256、生成 ESP Web Tools manifest；无完整 Release 时安全禁用刷写 | `python3 tests/check_ble_monitor.py`、`python3 tests/test_prepare_web_flash_site.py` | 首次启用 Pages 后人工确认 HTTPS、串口选择和两个板型各一次真机刷写 |
| `OP-BCAST-001` | 顶部功率/踏频/心率多选默认全开；取消项隐藏对应窗口且保存后停止对应 BLE 通知；至少保留一项 | `make -C test run`、`python3 tests/test_portal_config_save.py`、`python3 tests/check_portal_structure.py` | Zwift/MyWhoosh 中按开启项观察数据源 |
| `OP-BCAST-002` | BOOT 按钮按启用项选择目标：单项启用时直接调该项，多项启用时骑行优先功率、跑步优先配速/速度；长按 WiFi 确认回滚本次调节 | `make -C test run`、`python3 tests/test_core_flow_endurance.py` | 实体按钮在不同开关组合下人工确认 |
| `OP-BCAST-003` | 广播项开关真实影响 BLE 数据面：关闭功率后 CPS/FTMS 功率停止，只开心率时仅 HRS 持续通知 | `python3 tests/ble_broadcast_monitor.py`、`python3 tests/check_ble_broadcast_monitor.py` | 设备需处于配网态，脚本通过 `/api/config` 切换 `broadcast_mask` |
| `OP-BLE-001` | CPS 8B、FTMS flags、RSC 0.1m/Feature 与控制点响应 | `python3 tests/test_ble_protocol_vectors.py`、`python3 tests/ble_selftest.py` | 标准客户端扫描与字段显示人工确认 |
| `OP-BLE-002` | 两轮独立扫描/连接/断开；断开后重广播并清 ERG | `python3 tests/test_ble_reconnect_harness.py`、`python3 tests/stage3_conn_test.py` | 训练 App 退出后重连人工确认 |
| `OP-BLE-003` | 本地浏览器监控客户端可连接已授权 BikePower，持续记录通知停更、断连和数值异常 | `python3 tests/check_ble_monitor.py`、`python3 scripts/tools/serve_ble_monitor.py` | Web Bluetooth 首次授权必须人工点击，浏览器页面导出异常报告 |
| `OP-RUN-001` | 跑步 RSC 速度、步频、步幅、0.1m 距离单位与 running/paused flags | `make -C test run`、`python3 tests/test_ble_protocol_vectors.py`、`python3 tests/ble_selftest.py --profile running --control` | Zwift/MyWhoosh 跑步数据人工确认 |
| `OP-RUN-002` | 逐 opcode 等待 indication；moving 累计增长 → Stop 冻结 → paused Reset 清零且保持暂停 → Resume 恢复增长；队列拒绝返回 Operation Failed | `make -C test run`、`python3 tests/test_ble_running_harness.py`、`python3 tests/ble_selftest.py --profile running --control` | 每阶段至少两个独立样本；标准跑步客户端控制点人工确认 |
| `OP-WIFI-001` | WiFi done 队列满时 pending latch 最终可达；重复完成事件幂等清理 | `python3 tests/check_wifi_done_contract.py` | 真机队列压力与配网退出待验收 |
| `OP-STAB-001` | NVS schema、常量、板型、任务/WDT、门户结构不漂移 | `python3 tests/check_config_consistency.py`、`python3 tests/check_nvs_schema.py`、`python3 tests/check_board_desc.py`、`python3 tests/check_tasks_wdt.py`、`python3 tests/check_operation_coverage.py` | 无 |
| `OP-STAB-002` | 3 分钟核心流程耐久（mock）+ 5 分钟真机设备耐久（反复连接/通知） | `python3 tests/test_core_flow_endurance.py`、`python3 tests/device_soak_5min.py` | 发版阻塞项为 5 分钟真机耐久；更长老化按风险可选追加 |
| `OP-REL-001` | 发布产物门禁：10 个上传附件、SHA256、manifest、zip 内容、Git ID 和真机证据不漂移 | `python3 scripts/tools/check_release_artifacts.py --require-pushed --require-device-evidence`、`python3 tests/check_release_gate.py`、`python3 tests/test_release_artifacts.py` | 正式发布前必须从已提交并已推送源码重新构建，且当前 Git 的 P0 证据全部通过 |
| `OP-MANUAL-001` | 物理按钮、LED 颜色/闪烁、手机 Captive Portal、真实 Zwift/MyWhoosh 课程 | `python3 scripts/tools/device_acceptance.py validate` 校验记录结构 | 必须人工记录 `PASS`、`FAIL` 或 `NOT_RUN`；`NOT_RUN` 不能冒充通过 |

## 三、统一门禁命令

开发期快速门禁：

```bash
python3 tests/operation_gate.py --level quick
```

提交前完整无设备门禁：

```bash
python3 tests/operation_gate.py --level full
```

真机门禁：

```bash
python3 tests/operation_gate.py --level device \
  --board generic-c3 \
  --operator "验收人" \
  --port /dev/cu.usbmodem101 \
  --run-portal
```

`--run-portal` 会使用 HTTP profile harness 依次验证 cycling/running：每次测试先强制 `broadcast_mask=7`，避免原配置关闭功率/踏频/心率导致假失败，随后在 `finally` 恢复原 profile、原广播掩码和完整 running 持久配置。running 自测逐 opcode 等待 indication，并按 moving → Stop → paused Reset → Resume 四阶段分别采集多样本窗口。若不运行门户，必须通过 `--device-profile cycling|running` 明确当前固件 GATT profile；门禁验证当前 profile 后，再用 `device_profile_ble_harness.py` 通过 Config Service 临时切换并验证另一 profile，不启动 WiFi，最后恢复五组原配置。

手工拆分命令：

```bash
make -C test run
python3 tests/test_ble_protocol_vectors.py
python3 tests/test_ble_config_e2e.py
python3 tests/test_ble_reconnect_harness.py
python3 tests/test_ble_running_harness.py
python3 tests/test_device_profile_harness.py
python3 tests/check_ftms_control_contract.py
python3 tests/check_wifi_done_contract.py
python3 tests/test_portal_config_save.py
python3 tests/test_core_flow_endurance.py
python3 tests/ble_selftest.py --profile cycling
python3 tests/ble_selftest.py --profile running --control
python3 tests/ble_config_e2e.py --port /dev/cu.usbmodem101 --board generic-c3
python3 tests/device_profile_ble_harness.py --profile running --board generic-c3
python3 tests/device_profile_harness.py --profile cycling
python3 tests/device_profile_harness.py --profile running
python3 tests/stage3_conn_test.py
python3 tests/ble_broadcast_monitor.py
python3 tests/check_ble_broadcast_monitor.py
python3 tests/check_ble_monitor.py
python3 tests/test_prepare_web_flash_site.py
python3 tests/stage5_portal_test.py
python3 tests/check_config_consistency.py
python3 tests/check_nvs_schema.py
python3 tests/check_board_desc.py
python3 tests/check_tasks_wdt.py
python3 tests/check_portal_structure.py
python3 tests/check_portal_js.py
python3 tests/check_operation_coverage.py
python3 tests/check_release_gate.py
python3 tests/test_release_artifacts.py
python3 scripts/tools/serve_ble_monitor.py
python3 scripts/tools/device_acceptance.py validate
```

## 四、人工门禁记录（P1，不阻塞发版）

以下人工项默认不阻塞发版，但每次发可分发固件时建议按板型在结构化证据和测试报告中记录。P0 自动项（串口启动、BLE 全量配置读写与 NVS 持久化、骑行 BLE、重连、5 分钟设备耐久）必须全部通过；跑步 profile、配网门户和广播项因需要把测试机切到设备热点，归入 P1。证据文件是 `tests/reports/device-acceptance-generic-c3.json` 与 `tests/reports/device-acceptance-m5stamp-c3.json`，release 总包内保留相同文件名到 `evidence/`。

| 项目 | 通过标准 | 记录要求 |
|------|----------|----------|
| 实体按钮短按/长按 | 短按减功率，长按按步进持续加功率 | 记录板型、当前模式、步进 |
| WiFi 二次确认 | 2 秒长按进入确认窗口，再按触发配网；超时取消 | 记录 LED 场景和网页是否打开 |
| LED 灯效 | 待连接慢闪，已连接按配置，WiFi/确认/错误场景可见 | 记录板型和 active-low/high 行为 |
| 手机 Captive Portal | iOS/Android 能弹出或可手动访问 `192.168.4.1` | 记录手机系统和浏览器 |
| Zwift/MyWhoosh 骑行 | fixed、配速机器人、ERG 课程进入/退出均可恢复调功 | 记录 App、平台、课程类型 |
| Zwift/MyWhoosh 跑步 | RSC 速度/步频能被识别，配速浮动合理 | 记录 App、平台、速度/配速 |
| 广播数据项开关 | 取消功率/踏频/心率后，对应窗口隐藏且客户端不再收到对应数据；至少一项保持开启 | 记录 App、开启组合、观察到的数据项 |
| BLE 浏览器监控 | 页面能连接已授权设备，异常计数无误，导出 JSON 报告可追溯 | 记录浏览器、运行时长、异常计数和报告文件 |

初始化当前固件和 Git ID 的证据：

```bash
python3 scripts/tools/device_acceptance.py init \
  --board generic-c3 \
  --operator "验收人"
```

记录人工项：

```bash
python3 scripts/tools/device_acceptance.py set \
  --file tests/reports/device-acceptance-generic-c3.json \
  --item led_patterns \
  --status PASS \
  --evidence "generic-c3：待连接慢闪、连接常亮、确认快闪"
```

开发期可校验结构、版本和 Git ID：

```bash
python3 scripts/tools/device_acceptance.py validate \
  --file tests/reports/device-acceptance-generic-c3.json
```

正式发布前必须运行严格校验（只强制 P0 自动项；P1 人工项允许 `NOT_RUN`，但必须在测试报告明示）：

```bash
python3 scripts/tools/device_acceptance.py validate --strict \
  --file tests/reports/device-acceptance-generic-c3.json
python3 scripts/tools/device_acceptance.py validate --strict \
  --file tests/reports/device-acceptance-m5stamp-c3.json
python3 scripts/tools/check_release_artifacts.py \
  --require-pushed \
  --require-device-evidence
```

状态只有 `PASS`、`FAIL` 和 `NOT_RUN`。如果人工项未执行，保留 `NOT_RUN` 并在 evidence 中写明原因；`NOT_RUN` 不能冒充自动通过。每份证据的板型、`FIRMWARE_VERSION` 和 Git ID 必须匹配，旧固件或另一板型证据不得复用。

本地 `build_all.sh` 允许没有证据，以便在无设备环境生成预览产物；若证据存在，脚本会按目标板型校验并收录到 release 的 manifest、测试报告和总包。`publish_github_release.py` 正式双板发布时强制要求 generic-c3 与 m5stamp-c3 的全部 P0 项通过，并验证 tag 已推送。

## 五、维护规则

- 新增用户可见操作时，先在本矩阵增加 `OP-*` 行，再补测试或人工门禁。
- 新增保存字段时，同时更新 `tests/test_portal_config_save.py`、`tests/test_core_flow_endurance.py` 和本矩阵。
- 新增模式或 BLE 控制点时，同时更新 `test/test_domain.c`、`tests/ble_selftest.py` 和本矩阵。
- 调整广播项语义时，同步更新 `tests/ble_broadcast_monitor.py` 和 `tests/check_ble_broadcast_monitor.py`。
- 调整 BLE 协议面或异常阈值时，同步更新 `tools/web/ble-monitor.html` 与 `tests/check_ble_monitor.py`。
- 每次发版前运行 `python3 tests/operation_gate.py --level full`；涉及 BLE/配网/硬件行为时追加 `--level device`。
- 真机步骤重跑时只更新对应自动项，不覆盖按钮、LED、手机等人工证据。
- 固件版本或 Git ID 变化后重新初始化并执行证据，不能手工修改元数据绕过重测。
