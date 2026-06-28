# 测试覆盖率分析（目标 90%）

> 配套规则：[`.trae/rules/workflows/test-coverage.md`](../.trae/rules/workflows/test-coverage.md)
> 维护要求：每次抽取纯逻辑到领域层 / 新增测试脚本后，更新本矩阵。

## 一、项目模块清单与代码规模

### 领域层 `components/bikepower_domain/`（纯 C，host 可测）

| 文件 | 行数 | 职责 | 可测性 |
|------|------|------|--------|
| `power_engine.c` | 335 | 功率/踏频/心率快照生成 | ✅ host 单测 |
| `hr_model.c` | 226 | 心率一阶动力学 + HRV | ✅ host 单测 |
| `profile.c` | 120 | 用户档案 + 配速机器人 | ✅ host 单测 |
| `config_store.c` | 86 | 配置 schema 默认/校验/迁移 | ✅ host 单测 |
| `ota_version.c` | 22 | 语义版本比较（防降级） | ✅ host 单测（v3.1.8 新增） |
| `json_miniparse.c` | 35 | latest.json 解析 | ✅ host 单测（v3.1.8 新增） |
| `button_logic.c` | 38 | 按压时长分类（短按/长按/确认） | ✅ host 单测（v3.1.8+ 新增） |
| `led_pattern.c` | 42 | LED 模式→颜色映射 + 闪烁节拍 | ✅ host 单测（v3.1.8+ 新增） |
| `domain_rng.c` | 40 | xorshift32 随机数 | ✅ host 单测 |
| **小计** | **944** | | **100% host 可测** |

### 板级层 `main/`（依赖 ESP-IDF / 硬件）

| 文件 | 行数 | 职责 | 可测性分类 |
|------|------|------|-----------|
| `wifi_portal.c` | 2261 | HTTP 配网门户 + OTA 页 + JS | 🟡 静态可校验（JS/路由/HTML） |
| `ble_bikepower.c` | 913 | BLE 五服务 | 🔴 真机必测 |
| `radio_ctrl.c` | 579 | WiFi/BLE 射频互斥 | 🔴 真机必测 |
| `nvs_store.c` | 322 | NVS 配置持久化 | 🔴 真机必测（schema 校验已抽领域层） |
| `app_main.c` | 274 | 主循环 + 任务创建 | 🔴 真机必测 |
| `app_state.c` | 271 | 状态机 | 🟡 状态转换表可静态校验 |
| `led.c` | 241 | LED 模式 | 🔴 真机必测 |
| `ota_downloader.c` | 227 | OTA HTTP 下载 | 🟡 配置可静态校验（纯逻辑已抽领域层） |
| `button.c` | 194 | 按钮去抖/长按 | 🟡 去抖逻辑可抽领域层 |
| `captive_portal.c` | 164 | DNS 劫持 | 🔴 真机必测 |
| `ota_update.c` | 102 | OTA 自检/回滚 | 🔴 真机必测（版本比较已抽领域层） |
| `board_detect.c` | 93 | 板级检测 | 🔴 真机必测 |
| **小计** | **5641** | | |

### 头文件（不计入覆盖率分母）

`main/*.h` + `components/.../include/*.h` 共约 1500 行，仅声明，不计入。

## 二、当前测试资产

### 自动化测试（无需设备，CI 可跑）

| 层 | 入口 | 覆盖对象 | 断言/用例数 | 状态 |
|----|------|---------|------------|------|
| TC-1 host 单测 | `test/test_domain` | 领域层 7 个文件 | 946 断言 | ✅ 全绿 |
| TC-3a JS 静态 | `tests/check_portal_js.py` | wifi_portal.c 所有 `<script>` | 按页面数 | ✅ |
| TC-3b OTA 静态 | `tests/ota_flow_test.py --static` | OTA 路由/JS/函数绑定 | 按检查项 | ✅ |
| TC-3c 常量一致性 | `tests/check_config_consistency.py` | config 头文件 ↔ 文档 | 按常量数 | ✅（v3.1.8 新增） |

### 真机测试（需 ESP32-C3 + 串口 + 手机）

| 层 | 入口 | 覆盖对象 | 状态 |
|----|------|---------|------|
| TC-2 BLE 自测 | `tests/ble_selftest.py` | 五服务 + 通知 + FTMS 控制 | ✅ |
| TC-3 配网自测 | `tests/stage5_portal_test.py` | 首页/倒计时/续期/提交 | ✅ |
| TC-3b OTA 动态 | `tests/ota_flow_test.py` | /ota/check /ota/status /ota/upgrade | ✅ |
| DT 真机系列 | `.trae/rules/workflows/device-testing.md` | 按钮/LED/状态机/内存 | ✅ |

### 串口监控工具（辅助）

| 脚本 | 用途 |
|------|------|
| `tests/serial_daemon.py` | 串口日志持续抓取 + 自动重连 |
| `tests/serial_capture.py` | 单次抓取 |
| `tests/ota_serial_monitor.py` | OTA 流程串口专用监控 |

## 三、覆盖率矩阵（按模块）

| 模块 | 总行 | host 单测 | 静态脚本 | 真机 | 自动化覆盖行 | 自动化率 |
|------|------|----------|---------|------|------------|---------|
| `power_engine.c` | 335 | ✅ | - | - | 335 | 100% |
| `hr_model.c` | 226 | ✅ | - | - | 226 | 100% |
| `profile.c` | 120 | ✅ | - | - | 120 | 100% |
| `config_store.c` | 86 | ✅ | - | - | 86 | 100% |
| `ota_version.c` | 22 | ✅ | - | - | 22 | 100% |
| `json_miniparse.c` | 35 | ✅ | - | - | 35 | 100% |
| `button_logic.c` | 38 | ✅ | - | - | 38 | 100%（v3.1.8+ 新增） |
| `led_pattern.c` | 42 | ✅ | - | - | 42 | 100%（v3.1.8+ 新增） |
| `domain_rng.c` | 40 | ✅ | - | - | 40 | 100% |
| `wifi_portal.c` | 2261 | - | 🟡 JS+路由+HTML | ✅ | ~680（30%） | 30% |
| `ota_downloader.c` | 227 | 🟡 纯逻辑已抽 | 🟡 配置 | ✅ | ~180（80%） | 80% |
| `app_state.c` | 271 | 🟡 按压分类已抽 | 🟡 状态表 | ✅ | ~120（44%） | 44%（v3.1.8+ 提升） |
| `button.c` | 194 | - | - | ✅ | 0 | 0% |
| `ble_bikepower.c` | 913 | - | - | ✅ | 0 | 0% |
| `radio_ctrl.c` | 579 | - | - | ✅ | 0 | 0% |
| `nvs_store.c` | 322 | - | - | ✅ | 0 | 0% |
| `app_main.c` | 274 | - | - | ✅ | 0 | 0% |
| `led.c` | 225 | 🟡 颜色映射+节拍已抽 | - | ✅ | ~90（40%） | 40%（v3.1.8+ 提升） |
| `captive_portal.c` | 164 | - | - | ✅ | 0 | 0% |
| `ota_update.c` | 102 | - | - | ✅ | 0 | 0% |
| `board_detect.c` | 93 | - | - | ✅ | 0 | 0% |
| **合计** | **6603** | | | | **1979** | **30.0%** |

## 四、当前覆盖率与目标差距

### 当前自动化覆盖率：约 30%（v3.1.8+ 提升至 30%，新增 button_logic/led_pattern 抽取）

- 领域层 944 行 100% 覆盖（v3.1.8+ 新增 button_logic 38 行 + led_pattern 42 行）
- main/ 静态可校验部分约 1035 行覆盖
- 合计 1979 / 6603 = 30.0%

### 达到 90% 的路径（缺口 ~60%）

| 优先级 | 动作 | 预期提升 | 复杂度 |
|--------|------|---------|--------|
| P0 | ✅ 已完成：抽取 `button_logic` 按压时长分类到领域层（v3.1.8+） | +38 行 → 0.6% | 低 |
| P0 | ✅ 已完成：抽取 `led.c` 模式枚举映射到领域层（v3.1.8+） | +42 行 → 0.6% | 低 |
| P0 | 抽取 `app_state.c` 状态转换表到领域层 + host 单测 | +271 行 → 4% | 中 |
| P1 | 扩充 `wifi_portal.c` 静态脚本（路由表/HTML 结构/表单字段） | +680 行 → 10% | 中 |
| P1 | 抽取 `nvs_store.c` schema 校验到领域层（部分已抽 config_store） | +160 行 → 2.5% | 中 |
| P1 | 抽取 `captive_portal.c` DNS 包构造到领域层 + host 单测 | +100 行 → 1.5% | 中 |
| P2 | `radio_ctrl.c` 状态机纯逻辑抽取 | +200 行 → 3% | 高 |
| P2 | `ble_bikepower.c` GATT 特征值定义静态校验 | +300 行 → 4.6% | 中 |
| P3 | `board_detect.c` 引脚映射表静态校验 | +93 行 → 1.4% | 低 |
| 累计 | | **+60% → 88%** | |

> 剩余 2% 通过补充边界用例达成 90%。

## 五、Loop 模式测试建立流程（每次改动必走）

```
改动需求
  ↓
1. 识别纯逻辑（不依赖 ESP-IDF/硬件的纯计算）
  ↓
2. 抽取到 components/bikepower_domain/（新文件 or 已有文件）
  ↓
3. main/ 改为 include 领域层头文件 + 调用
  ↓
4. test/test_domain.c 加测试函数（正常/边界/异常）
  ↓
5. 跑 TC-1: make -C test && ./test/test_domain  →  全绿
  ↓
6. 涉及 wifi_portal → 跑 TC-3a (JS) + TC-3b (OTA)
  ↓
7. 涉及常量 → 跑 TC-3c (常量一致性)
  ↓
8. 涉及 BLE/配网行为 → 跑 TC-2/TC-3 真机
  ↓
9. 更新本矩阵 + test-coverage.md 断言数
  ↓
10. TC-9a 全绿门 → 提交
```

## 六、不可自动化部分（明确边界）

以下代码因强依赖硬件/网络环境，**不计入自动化覆盖率分母**，走真机测试：

- BLE 通知回调（`ble_bikepower.c` 通知发送路径）
- ISR / GPIO 中断（`button.c` ISR 部分）
- WiFi 连接 / SoftAP / DNS 监听（`radio_ctrl.c` / `captive_portal.c` 运行时）
- HTTP 请求响应（`wifi_portal.c` handler 运行时）
- NVS 读写（`nvs_store.c` 运行时）
- OTA 下载写入（`ota_downloader.c` HTTP 运行时）
- 主循环 / 任务调度（`app_main.c` 运行时）

这些通过 `tests/ble_selftest.py` / `tests/stage5_portal_test.py` / `tests/ota_flow_test.py` 动态 + DT 真机系列覆盖。

## 七、维护规则

- 每次抽取纯逻辑到领域层，更新「覆盖率矩阵」对应行。
- 每次新增/修改测试脚本，更新「当前测试资产」表。
- 每次断言数变化，同步 `test-coverage.md` 的 TC-1 断言规模。
- 文档与代码漂移由 TC-3c `check_config_consistency.py` 拦截。
