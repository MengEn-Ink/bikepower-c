# 测试覆盖率分析

> 配套规则：[`.trae/rules/workflows/test-coverage.md`](../../.trae/rules/workflows/test-coverage.md)
> 维护要求：每次抽取纯逻辑到领域层 / 新增测试脚本后，更新本矩阵。
> 版本：v4.0.4（已永久放弃设备内 OTA + 家庭网络；不再保留"为覆盖率抽取但生产代码未采用"的并行实现）。

## 一、项目模块清单与代码规模

### 领域层 `components/bikepower_domain/`（纯 C，host 可测）

| 文件 | 行数 | 职责 | 可测性 |
|------|------|------|--------|
| `power_engine.c` | 464 | 功率/踏频/心率快照生成 + 浮动幅度平滑游走 | ✅ host 单测 |
| `hr_model.c` | 233 | 心率一阶动力学 + HRV + 固定心率浮动 | ✅ host 单测 |
| `profile.c` | 181 | 用户档案 + 配速机器人 | ✅ host 单测 |
| `config_store.c` | 312 | 配置 schema 默认/校验/迁移/序列化 | ✅ host 单测 |
| `json_miniparse.c` | 36 | 扁平 JSON 字段提取（配网请求体解析） | ✅ host 单测 |
| `button_logic.c` | 27 | 按压时长分类（短按/长按/确认） | ✅ host 单测 |
| `led_pattern.c` | 44 | LED 模式→颜色映射 + 闪烁节拍 | ✅ host 单测 |
| `button_debounce.c` | 22 | 按钮去抖/长按节拍判定 | ✅ host 单测 |
| `domain_rng.c` | 40 | xorshift32 随机数 | ✅ host 单测 |
| **小计** | **1359** | | **100% host 可测** |

> 抽取原则（v4.0.0 校准）：仅抽取**生产代码真正调用**的纯逻辑（button_logic/led_pattern/button_debounce 分别被 app_state/led/button 引用）。此前为刷覆盖率抽取但 main/ 未采用的 `ftms_codec`/`app_state_machine`/`radio_state`/`dns_packet` 已删除（避免与 ble_bikepower/app_state/radio_ctrl/captive_portal 的手写实现形成并行双份）。

### 板级层 `main/`（依赖 ESP-IDF / 硬件）

| 文件 | 行数 | 职责 | 可测性分类 |
|------|------|------|-----------|
| `ble_bikepower.c` | 913 | BLE 五服务 + FTMS 编解码 | 🟡 GATT 注册/编解码真机必测（`ble_selftest.py`） |
| `wifi_portal.c` | 699 | HTTP 配网门户（纯 /api/xxx JSON） | 🟡 静态可校验（路由/HTML/JS/表单，TC-3a/3d）；handler 运行时真机必测 |
| `radio_ctrl.c` | 278 | WiFi/BLE 射频互斥（AP + coex） | 🟡 WiFi 启停真机必测 |
| `app_state.c` | 275 | 应用状态机 + 事件分发 | 🟡 决策/副作用真机必测 |
| `nvs_store.c` | 292 | NVS 配置持久化 | 🟡 schema 静态校验（TC-3e）；运行时真机必测 |
| `led.c` | 226 | LED 模式（GPIO/RMT） | 🟡 颜色映射已抽领域层；RMT/GPIO 真机必测 |
| `button.c` | 194 | 按钮去抖/长按 | 🟡 去抖逻辑已抽领域层；ISR/任务真机必测 |
| `captive_portal.c` | 176 | DNS 劫持 + 探测端点 | 🟡 socket 真机必测 |
| `app_main.c` | 272 | 主循环 + 任务创建 | 🟡 任务/WDT 静态校验（TC-3g）；主循环真机必测 |
| `board_detect.c` | 93 | 板级检测 | 🟡 描述表静态校验（TC-3f）；GPIO 检测真机必测 |
| **小计** | **3418** | | |

### 头文件（不计入覆盖率分母）

`main/*.h` + `components/.../include/*.h` 仅声明，不计入。

## 二、当前测试资产

### 自动化测试（无需设备，CI 可跑）

| 层 | 入口 | 覆盖对象 | 状态 |
|----|------|---------|------|
| TC-1 host 单测 | `test/test_domain` | 领域层 9 个文件 | ✅ 全绿（5282 断言，含按钮操作场景、随机功率心率随动、浮动启停、网页保存持久化链路） |
| TC-3a JS 静态 | `tests/check_portal_js.py` | `main/web/app.js` node --check | ✅ |
| TC-3c 常量一致性 | `tests/check_config_consistency.py` | config 头文件 ↔ 文档 | ✅ |
| TC-3d 门户结构 | `tests/check_portal_structure.py` | 路由表/HTML/前端 API 调用 | ✅ |
| TC-3e NVS schema | `tests/check_nvs_schema.py` | NVS key 定义 vs 使用 | ✅ |
| TC-3f 板型描述表 | `tests/check_board_desc.py` | 板型枚举/引脚/名称一致性 | ✅ |
| TC-3g 任务/WDT | `tests/check_tasks_wdt.py` | xTaskCreate/WDT/队列 | ✅ |
| TC-3h mock 保存 | `tests/test_portal_config_save.py` | `/api/config` 保存后 `/api/state` 回显 | ✅ |
| TC-3i 核心流程耐久 | `tests/test_core_flow_endurance.py` | 3 分钟循环覆盖按钮调功、功率/心率随动、网页编辑、LED 静默、设备模板、关闭 WiFi | ✅ |

### 真机测试（需 ESP32-C3 + 串口 + 手机）

| 层 | 入口 | 覆盖对象 | 状态 |
|----|------|---------|------|
| TC-2 BLE 自测 | `tests/ble_selftest.py` | 五服务 + 通知 + FTMS ERG 控制 | ✅ |
| TC-3 配网自测 | `tests/stage5_portal_test.py` | 首页/倒计时/续期/提交（/api/xxx JSON） | ✅ |
| DT 真机系列 | `.trae/rules/workflows/device-testing.md` | 按钮/LED/状态机/内存 | 🟡 物理按钮/LED 人工确认 |

### 本地 Mock / 串口工具（辅助）

| 脚本 | 用途 |
|------|------|
| `tests/mock_server.py` | 本地复刻全部 /api/xxx，浏览器调试 `main/web/` 前端不烧设备 |
| `tests/test_portal_config_save.py` | 自动启动 mock server，验证配网页保存字段即时回显 |
| `tests/test_core_flow_endurance.py` | 自动启动 mock server，默认 3 分钟循环验证核心流程并输出 markdown 报告 |
| `tests/serial_daemon.py` | 串口日志持续抓取 + 自动重连 |
| `tests/serial_capture.py` | 单次抓取 |

## 三、覆盖策略

- **领域层**：纯 C 无硬件依赖，100% host 单测覆盖，是回归保护主战场。
- **板级层 main/**：强依赖 ESP-IDF/硬件运行时，靠两条互补手段覆盖：
  1. **静态校验脚本**（TC-3a/3c/3d/3e/3f/3g）：不烧设备即可拦截路由表/常量/NVS key/任务/板型描述的漂移。
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
9. 涉及 BLE/配网行为 → 跑 TC-2 / TC-3 真机
  ↓
10. 生成固件产物 → 同步生成 test-report.md + core-flow-endurance.md + user-manual.md + release 总包 zip
  ↓
11. 更新本矩阵 + test-coverage.md 断言规模
  ↓
12. 全静态门禁绿 → 提交
```

## 五、不可自动化部分（明确边界，走真机测试）

以下代码强依赖硬件/网络运行时，靠真机测试覆盖：

| 模块 | 依赖 | 真机覆盖入口 |
|------|------|------------|
| `ble_bikepower.c` GATT 注册/通知回调/FTMS 编解码 | NimBLE 协议栈运行时 | `tests/ble_selftest.py` |
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
