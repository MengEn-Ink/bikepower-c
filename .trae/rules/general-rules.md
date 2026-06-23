# 项目概览（BikePower C）

<!-- stability: tier-2 (稳定，随项目结构变更) -->
<!-- rule-id-prefix: PR -->

## 项目信息
- **名称**：BikePower C — ESP32-C3 蓝牙功率计模拟器（C 重写版）
- **运行环境**：ESP-IDF v5.5.4 + NimBLE + FreeRTOS（非 MicroPython）
- **硬件平台**：合宙 ESP32-C3（RISC-V 单核 160MHz, 400KB SRAM, WiFi+BLE5.0）
- **目标版本**：v3.1.1
- **参考仓库**：git@gitee.com:mengen4jv/bikepower-c.git
- **构建/烧录**：`idf.py build` / `idf.py -p <port> flash monitor`

## 强制规则
1. **蓝牙优先**：BLE 是核心功能，仅在用户二次确认进入配网模式时关闭。
2. **WiFi/BLE 互斥**：进入配网必须先停 BLE 再启 WiFi AP（见 wifi-ble-mutex.md）。
3. **异常恢复**：BLE 初始化失败走可控恢复（记录 + 必要时 `esp_restart()`）。
4. **分阶段验证**：每阶段编译 → 烧录 → 真机验证 → 分批提交推送，禁止只写不验证。
5. **代码改动同步文档**：改代码必须同步 PROGRESS.md / 规则 / CHANGELOG。
6. **按功能分批提交**：不相关变更不混进同一 commit。
7. **commit 后立即 push**（最高优先级）。

## 文件结构
```
bikepower-c/
├── AGENTS.md                 — 跨 AI 工具入口
├── PROGRESS.md               — 阶段进度 + 待完成项 + 已知限制
├── CMakeLists.txt            — 顶层工程
├── partitions.csv            — 分区表
├── sdkconfig.defaults        — 默认配置
├── main/
│   ├── app_main.c            — 入口：任务创建 + 事件循环 + 连接轮询 + WDT
│   ├── app_state.c/.h        — 7 态状态机（按钮/连接事件 → 调功/确认/配网）
│   ├── app_events.h          — 事件类型定义（BTN_DOWN/REPEAT/UP/CONN_CHANGED）
│   ├── button.c/.h           — GPIO9 中断驱动按钮任务
│   ├── led.c/.h              — GPIO12 LED 任务（四态）
│   ├── ble_bikepower.c/.h    — NimBLE 五服务 + FTMS ERG
│   └── bikepower_config.h    — 全局常量（引脚/阈值/UUID/版本）
├── components/bikepower_domain/   — 纯 C 领域层（可 host 单测）
│   ├── power_engine.c        — 功率/踏频数据引擎
│   ├── hr_model.c            — 心率模型
│   ├── profile.c            — 用户配置 + 范围校验
│   ├── domain_rng.c         — 随机数
│   └── include/             — 头文件 + domain_config.h
├── tests/
│   ├── ble_selftest.py       — bleak 五服务 + FTMS ERG 自测
│   ├── stage3_conn_test.py   — 连接事件驱动状态机验证
│   └── serial_capture.py     — 串口日志抓取工具
├── test/test_domain          — host 单测（领域层）
├── docs/05-cidf-nimble-rewrite.md  — 基线重写方案
├── .trae/rules/            — AI 规则（C/ESP-IDF 版）
│   ├── general-rules.md      — 项目结构 + 模块边界 + 提交/发版规则
│   ├── coding-style.md       — C/ESP-IDF 代码风格 + FreeRTOS 约束
│   ├── hardware-constraints.md — ESP32-C3 硬件约束
│   ├── wifi-ble-mutex.md     — WiFi/BLE 射频互斥（硬约束）
│   ├── ai-challenge.md       — AI 质疑规则
│   ├── best-practices.md     — 项目优化审查规则（发版前）
│   └── workflows/
│       ├── pre-dev-checklist.md   — 开发前检查清单
│       └── entropy-governance.md  — Entropy 治理（发版前扫描）
└── .trae/memory/             — 项目记忆
```

## 模块依赖图
```
        app_main.c (入口/事件循环/WDT)
            │
   ┌────────┼────────┬─────────┬──────────┐
   │        │        │         │          │
button.c  led.c  app_state.c  ble_bikepower.c  (阶段4/5: nvs_store/radio_ctrl/wifi_portal)
   │        │        │         │
   └────────┴───→ app_queue (Queue)
                    │
            components/bikepower_domain (power_engine/hr_model/profile)
```

## 运行时数据流
```
按钮中断 → button_task → app_queue → app_state_handle_event
                                          │
连接轮询(app_main) → APP_EVT_CONN_CHANGED ┤→ 调功/确认窗口/配网触发
                                          │→ led_set_pattern
power_engine.snapshot → ble_bikepower_set_data → BLE 通知
```

## 模块互斥约束
| 约束 | 执行者 |
|------|--------|
| WiFi/BLE 射频互斥 | radio_ctrl（阶段5）先停 BLE 再启 WiFi |
| 蓝牙优先 | app_state 长按 → 确认 → wifi_trigger |
| 配网后重启恢复 BLE | radio_ctrl `esp_restart()`（阶段5） |
| 无循环依赖 | import/include 链为 DAG |

## 阶段路线（详见 PROGRESS.md）
| 阶段 | 内容 | 状态 |
|------|------|------|
| 0 | 工程骨架 | ✅ |
| 1 | BLE CPS 最小闭环 | ✅ |
| 2 | 领域层 + 五服务 + FTMS ERG | ✅ |
| 3 | 按钮 + LED + 状态机 | ✅ 代码完成（物理按钮待人工确认） |
| 4 | 配置持久化 NVS | ⬜ |
| 5 | WiFi 配网（radio_ctrl + AP + HTTP） | ⬜ |
| 6 | 稳定性与发版 | ⬜ |

## 发布流程
- 分支：GitHub Flow（main + feature/hotfix）。
- 版本号：SemVer `vMAJOR.MINOR.PATCH`，commit/tag/Release 三者一致。
- 发版前：编译通过 + 真机测试 + CHANGELOG 测试报告 + tag v3.0.0 + DIS 固件版本同步。
- **固件发行产物必须合并为单文件（强制）**：创建发行版时，必须用 `esptool.py merge_bin` 把 bootloader + partition-table + app 合并为单一 `.bin`，用户一次烧录即可，禁止分发分散的多个 bin 文件。
- Commit message：`<type>(<scope>): <简体中文概述>` + 逐文件正文 + `Co-authored-by: AI <ai@trae>` + `Model:`。
