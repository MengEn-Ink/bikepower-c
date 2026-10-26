# 测试覆盖率分析

> 配套规则：[`.trae/rules/workflows/test-coverage.md`](../../.trae/rules/workflows/test-coverage.md)
> 维护要求：每次抽取纯逻辑到领域层 / 新增测试脚本后，更新本矩阵。
> 版本：v4.1.3（已永久放弃设备内 OTA + 家庭网络；不再保留"为覆盖率抽取但生产代码未采用"的并行实现）。

## 一、项目模块清单与可测性

代码行数变化频繁且不能代表覆盖质量，本矩阵不再手工记录易漂移的行数；规模分析需要时以当前工作区 `wc -l` 或覆盖工具结果为准。

### 领域层 `components/bikepower_domain/`（纯 C，host 可测）

| 文件 | 职责 | 可测性 |
|------|------|--------|
| `power_engine.c` | 功率/踏频/心率快照生成 + 浮动幅度平滑游走 | ✅ host 单测 |
| `running_engine.c` | 跑步速度、步频、距离和配速机器人 | ✅ host 单测 |
| `hr_model.c` | 心率一阶动力学 + HRV + 固定心率浮动 | ✅ host 单测 |
| `profile.c` | 用户档案 + 骑行配速机器人 | ✅ host 单测 |
| `config_store.c` | 配置 schema 默认/校验/迁移/序列化 | ✅ host 单测 |
| `device_template.c` | 外部 BLE 设备模板、合并和服务标记 | ✅ host 单测 |
| `device_analysis.c` | 外部 BLE 运动数据解析、统计和 JSON 输出 | ✅ host 单测 |
| `ble_payload_codec.c` | CPS/CSC/HRS/FTMS/RSC 生产 payload 编码 | ✅ host 编译 + 独立 golden vectors |
| `json_miniparse.c` | 扁平 JSON 字段提取（配网请求体解析） | ✅ host 单测 |
| `button_logic.c` | 按压时长分类（短按/长按/确认） | ✅ host 单测 |
| `led_pattern.c` | LED 模式、颜色与闪烁节拍 | ✅ host 单测 |
| `button_debounce.c` | 按钮去抖/长按节拍判定 | ✅ host 单测 |
| `domain_rng.c` | xorshift32 随机数 | ✅ host 单测 |
| **合计** | **13 个生产源文件** | **全部可 host 编译；覆盖深度以断言和场景矩阵为准** |

> 抽取原则（v4.0.0 校准）：仅抽取**生产代码真正调用**的纯逻辑（button_logic/led_pattern/button_debounce 分别被 app_state/led/button 引用，`ble_payload_codec` 被 `main/ble_bikepower.c` 直接调用）。此前为刷覆盖率抽取但 main/ 未采用的 `ftms_codec`/`app_state_machine`/`radio_state`/`dns_packet` 已删除，避免并行双份实现。

### 板级层 `main/`（依赖 ESP-IDF / 硬件）

| 文件 | 职责 | 可测性分类 |
|------|------|-----------|
| `ble_bikepower.c` | BLE 骑行/跑步服务、通知发送、FTMS 控制点 | 🟡 payload 由生产 codec + vectors 覆盖；GATT/NimBLE 链路仍须真机 |
| `ble_scanner.c` | BLE Central/Observer 扫描、读取与运动数据采样 | 🟡 静态契约 + 真机外设必测 |
| `wifi_portal.c` | HTTP 配网门户（纯 `/api/xxx` JSON） | 🟡 路由/HTML/JS 静态校验；handler 真机必测 |
| `radio_ctrl.c` | WiFi/BLE 射频共存与失败兜底 | 🟡 WiFi 启停真机必测 |
| `app_main.c` | 主循环、任务创建、事件可靠性和 WDT | 🟡 任务/WDT 静态校验；运行时真机必测 |
| `app_state.c` | 应用状态机 + 事件分发 | 🟡 领域决策可抽取部分 host 测；副作用真机必测 |
| `nvs_store.c` | NVS 配置持久化 | 🟡 schema 静态校验；运行时真机必测 |
| `device_template_store.c` | 模板 NVS 并发读写 | 🟡 领域数据 host 测；NVS 真机必测 |
| `led.c` | LED 模式（GPIO/RMT） | 🟡 颜色映射已抽领域层；RMT/GPIO 真机必测 |
| `button.c` | GPIO ISR、去抖任务和事件投递 | 🟡 去抖逻辑已抽领域层；ISR/任务真机必测 |
| `captive_portal.c` | DNS 劫持 + 探测端点 | 🟡 socket 真机必测 |
| `board_detect.c` | 板级检测 | 🟡 描述表静态校验；GPIO 检测真机必测 |
| **合计** | **12 个板级生产源文件** | **静态门禁与真机证据互补** |

### 头文件（不计入覆盖率分母）

`main/*.h` + `components/.../include/*.h` 仅声明，不计入。

## 二、当前测试资产

### 自动化测试（无需设备，CI 可跑）

| 层 | 入口 | 覆盖对象 | 状态 |
|----|------|---------|------|
| TC-1 host 单测 | `test/test_domain` | 领域层 13 个生产源文件 | ✅ 全绿（5892 断言 / 0 失败，含 running 会话、ERG 非持久化、配置映射、按钮场景、随机功率心率随动和设备分析） |
| TC-3a JS 静态 | `tests/check_portal_js.py` | `main/web/app.js` node --check | ✅ |
| TC-3c 常量一致性 | `tests/check_config_consistency.py` | config 头文件 ↔ 文档 | ✅ |
| TC-3d 门户结构 | `tests/check_portal_structure.py` | 路由表/HTML/前端 API 调用/设备分析详细与原始数据展示 | ✅ |
| TC-3e NVS schema | `tests/check_nvs_schema.py` | NVS key 定义 vs 使用 | ✅ |
| TC-3f 板型描述表 | `tests/check_board_desc.py` | 板型枚举/引脚/名称一致性 | ✅ |
| TC-3g 任务/WDT | `tests/check_tasks_wdt.py` | xTaskCreate/WDT/队列 | ✅ |
| TC-3h mock 保存 | `tests/test_portal_config_save.py` | `/api/config` 保存后 `/api/state` 回显 | ✅ |
| TC-3i 核心流程耐久 | `tests/test_core_flow_endurance.py` | 3 分钟循环覆盖按钮调功、功率/心率随动、网页编辑、LED 静默、设备模板、外部设备分析 raw 数据、关闭 WiFi | ✅ |
| TC-3q 真机验收证据 | `tests/test_device_acceptance.py` | 证据 schema、版本/Git 匹配、P0 严格模式、原子写入 | ✅ |
| TC-3r device 证据采集 | `tests/test_operation_gate_evidence.py` | 自动结果 PASS/FAIL 映射、未执行项保持、人工证据保留 | ✅ |
| TC-3s 串口启动判定 | `tests/test_serial_boot_check.py` | 空日志、缺 ROM/复位标志、panic、Backtrace、Task WDT/coredump 拒绝 | ✅ |
| TC-BLE-ADV 广播字段 | `tests/check_ble_advertising_fields.py` | 骑行/跑步主广播服务 UUID 与广播包名字预算 | ✅ |
| TC-BLE-VEC 协议 vectors | `tests/test_ble_protocol_vectors.py` | 10 组固定 hex；生产 C encoder 与无 bleak Python parser 独立交叉校验 | ✅ 3 项通过 |
| TC-BLE-RECON 重连 harness | `tests/test_ble_reconnect_harness.py` | fake scanner/client 验证两轮独立扫描、连接、显式断开 | ✅ |
| TC-BLE-RUNNING running harness | `tests/test_ble_running_harness.py` | opcode indication 等待、严格增长/冻结 helper 与 moving/pause/reset/resume 四阶段结构 | ✅ |
| TC-BLE-PROFILE profile harness | `tests/test_device_profile_harness.py` | 切换 profile、测试阶段强制 mask=7、running `--control` 参数与 finally 恢复原配置 | ✅ |
| TC-BLE-CP 控制点契约 | `tests/check_ftms_control_contract.py` | Invalid Parameter / Operation Failed、成功后更新 Status | ✅ |
| TC-WIFI-DONE 完成契约 | `tests/check_wifi_done_contract.py` | bool 接纳、pending latch、错误态幂等退出 | ✅ |

### 真机测试（需 ESP32-C3 + 串口 + 手机）

| 层 | 入口 | 覆盖对象 | 状态 |
|----|------|---------|------|
| TC-2 cycling BLE 自测 | `tests/ble_selftest.py --profile cycling` | CPS/HRS/CSC/FTMS Bike/DIS + ERG 控制 | 🟡 当前工作区待重新执行 |
| TC-2 running BLE 自测 | `tests/ble_selftest.py --profile running --control` | 逐 opcode indication；moving → Stop 冻结 → paused Reset 清零保持暂停 → Resume 四阶段 RSC/Treadmill 多样本 | 🟡 当前工作区待重新执行 |
| TC-2 profile 编排 | `tests/device_profile_harness.py` | 配网态切换互斥 GATT profile，测试强制 mask=7，finally 恢复原配置 | 🟡 当前工作区待重新执行 |
| TC-2 双次重连 | `tests/stage3_conn_test.py` | 两轮各自重新扫描、连接、显式断开与恢复广播 | 🟡 当前工作区待重新执行 |
| TC-3 配网自测 | `tests/stage5_portal_test.py` | 首页/倒计时/续期/提交（/api/xxx JSON） | 🟡 当前工作区待重新执行 |
| DT 真机系列 | `.trae/rules/workflows/device-testing.md` | 按钮/LED/状态机/内存 | 🟡 物理按钮/LED 人工确认 |

历史 release 曾有部分真机 PASS，但不能覆盖 2026-10-22 当前工作区。真机自动测试按板型分别写入 `tests/reports/device-acceptance-generic-c3.json` 与 `tests/reports/device-acceptance-m5stamp-c3.json`。每份文件必须匹配板型、当前 `FIRMWARE_VERSION` 和 Git ID；`NOT_RUN` 不算通过。开发期可以只校验结构，正式发布要求参与发布板型的全部 P0 自动项通过（含 5 分钟设备耐久），人工项为 P1 不阻塞。

### 本地 Mock / 串口工具（辅助）

| 脚本 | 用途 |
|------|------|
| `tests/mock_server.py` | 本地复刻全部 /api/xxx，浏览器调试 `main/web/` 前端不烧设备 |
| `tests/test_portal_config_save.py` | 自动启动 mock server，验证配网页保存字段即时回显 |
| `tests/test_core_flow_endurance.py` | 自动启动 mock server，默认 3 分钟循环验证核心流程并输出 markdown 报告 |
| `tests/serial_daemon.py` | 串口日志持续抓取 + 自动重连 |
| `tests/serial_capture.py` | 单次抓取；`--check-boot` 模式要求 ROM/复位启动标志完整且无致命异常，BLE 就绪由 `ble_selftest.py` 独立验证 |
| `tests/device_profile_harness.py` | 配网态临时切换 cycling/running，测试时强制 mask=7，调用对应 selftest，并在 finally 恢复原 profile/mask 和全部 running 持久配置 |

## 三、覆盖策略

- **领域层**：纯 C 无硬件依赖，100% host 单测覆盖，是回归保护主战场。
- **板级层 main/**：强依赖 ESP-IDF/硬件运行时，靠两条互补手段覆盖：
  1. **host/静态/离线 harness**：不烧设备即可拦截路由表、常量、NVS key、任务、协议 payload、控制点接纳和测试编排漂移。
  2. **真机测试**（TC-2/TC-3 + DT 系列）：BLE 五服务、配网门户、按钮/LED/内存等运行时行为。
- **口径说明**：不再追求"抽取灌水"的百分比数字。核心原则是——凡是生产代码实际使用的纯逻辑就抽到领域层做 host 测；硬件/网络运行时路径由静态校验 + 真机测试兜底。

## 四、Loop 模式测试流程（每次改动必走）

```
改动需求
  ↓
1. 识别纯逻辑（不依赖 ESP-IDF/硬件的纯计算）
  ↓
2. 若 main/ 会真正调用它 → 抽取到 components/bikepower_domain/ 并 include 调用
   （不为覆盖率而抽取生产代码不用的并行实现）
  ↓
3. test/test_domain.c 加测试函数（正常/边界/异常）
  ↓
4. 跑 TC-1: make -C test && ./test/test_domain  →  全绿
  ↓
5. 涉及 wifi_portal / 前端 → 跑 TC-3a (JS) + TC-3d (门户结构)
  ↓
6. 涉及常量 → TC-3c；涉及 NVS → TC-3e；涉及任务 → TC-3g；涉及板型 → TC-3f
  ↓
7. 涉及网页保存字段 → 跑 TC-3h mock 保存回显
  ↓
8. 生成固件前 → 跑 TC-3i 3 分钟核心流程耐久测试
  ↓
9. 涉及 BLE payload → 跑 golden vectors；涉及控制点/重连/profile → 跑对应离线 harness/静态契约
  ↓
10. 涉及 BLE/配网行为 → 按实际 profile 跑 TC-2 / TC-3 真机；重连必须完成两轮独立扫描
  ↓
11. 生成固件产物 → 同步生成 test-report.md + core-flow-endurance.md + user-manual.md + release 总包 zip；存在当前版本证据时按板型收录到 `evidence/`
12. 正式发布 → `check_release_artifacts.py --require-pushed --require-device-evidence`，证据缺失、过期或 P0 未通过均阻断
  ↓
13. 更新本矩阵 + test-coverage.md 断言规模
  ↓
14. 全门禁绿 → 提交
```

## 五、不可自动化部分（明确边界，走真机测试）

以下代码强依赖硬件/网络运行时，靠真机测试覆盖：

| 模块 | 依赖 | 真机覆盖入口 |
|------|------|------------|
| `ble_bikepower.c` GATT 注册/通知/指示链路 | NimBLE 协议栈运行时 | cycling/running `tests/ble_selftest.py` + 标准客户端；payload 本身由 vectors 覆盖 |
| `radio_ctrl.c` WiFi 启停/coex | esp_wifi_* + 射频硬件 | `tests/stage5_portal_test.py` |
| `wifi_portal.c` HTTP handler 运行时 | HTTP server socket | `tests/stage5_portal_test.py` |
| `captive_portal.c` DNS socket 监听 | UDP socket | `tests/stage5_portal_test.py` |
| `nvs_store.c` nvs_open/get/set 运行时 | flash 分区 | 真机 DT 系列 |
| `app_main.c` 主循环/任务调度 | FreeRTOS 时序 | 真机 DT 系列 |
| `app_state.c` 事件分发副作用 | ble/led/radio 副作用 | 真机 DT 系列 |
| `led.c` RMT/GPIO 驱动 | RMT 外设 | 真机 DT 系列（人工观察灯效） |
| `button.c` ISR/任务调度 | GPIO 中断 + FreeRTOS | 真机 DT 系列（人工按键） |
| `board_detect.c` GPIO 检测 | GPIO 电平 | 真机 DT 系列 |

## 六、维护规则

- 每次抽取纯逻辑到领域层，更新「一、模块清单」与「二、测试资产」。
- 每次新增/修改测试脚本，更新「当前测试资产」表。
- 每次 TC-1 断言数变化，同步 `test-coverage.md`。
- 文档与代码漂移由 TC-3c `check_config_consistency.py` 拦截。
