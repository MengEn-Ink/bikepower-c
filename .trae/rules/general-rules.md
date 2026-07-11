# 项目概览（BikePower C）

<!-- stability: tier-2 (稳定，随项目结构变更) -->
<!-- rule-id-prefix: PR -->

## 项目信息
- **名称**：BikePower C — ESP32-C3 蓝牙功率计模拟器（C 重写版）
- **运行环境**：ESP-IDF v5.5.4 + NimBLE + FreeRTOS（非 MicroPython）
- **硬件平台**：合宙 ESP32-C3（RISC-V 单核 160MHz, 400KB SRAM, WiFi+BLE5.0）
- **当前版本**：v4.1.0
- **参考仓库**：git@gitee.com:mengen4jv/bikepower-c.git
- **构建/烧录**：`idf.py build` / `idf.py -p <port> flash monitor`

## 强制规则
1. **蓝牙优先**：BLE 是核心功能，配网默认保持在线；仅共存初始化失败兜底时由 `radio_ctrl` 主动停 BLE。
2. **WiFi/BLE 射频共用**：WiFi 启停必须经 `radio_ctrl`，配网走 coex 共存模式（见 wifi-ble-mutex.md）。
3. **异常恢复**：BLE 初始化失败走可控恢复（记录 + 必要时 `esp_restart()`）。
4. **分阶段验证**：每阶段编译 → 烧录 → 真机验证 → 分批提交推送，禁止只写不验证。
5. **代码改动同步文档**：改代码必须同步 PROGRESS.md / 规则 / CHANGELOG。
6. **按功能分批提交**：不相关变更不混进同一 commit。
7. **commit 后立即 push**（最高优先级）。

## 文档命名规范（强制）

`docs/` 目录下文档按维度分子目录管理，所有 Markdown 文档使用 `docs/<category>/NN-kebab-case.md` 格式命名：

| 规则 | 说明 |
|------|------|
| 分类子目录 | `docs/user/`（产品使用）、`docs/dev/`（开发与技术）、`docs/archive/`（历史归档，仅供追溯） |
| 格式 `NN-名称.md` | NN 为两位数字前缀（01-99），**在所属子目录内**决定排列顺序 |
| kebab-case | 名称用小写+连字符（如 `ota-user-guide`） |
| 编号连续递增 | 新增文档取所属子目录当前最大编号 +1，不跳号、不复用已删除编号 |
| 过时不删除 | 过时/已完成文档移入 `docs/archive/` 并在顶部加归档标注，不物理删除 |
| 例外：`INDEX.md` | 索引文件留在 `docs/` 根，不加编号 |
| 例外：`images/` 子目录 | 图片资源留在 `docs/images/`，不编号 |
| 移动/重命名同步 | 必须同步更新 INDEX.md、跨文件交叉引用，以及门禁脚本（`tests/check_config_consistency.py`、`scripts/tools/quality_gate.py`）中硬编码的文档路径 |
| 禁止无编号文档 | 禁止在分类子目录放无编号的 .md |

当前目录划分与编号分配见 [docs/INDEX.md](../../docs/INDEX.md)。

## 文件结构
```
bikepower-c/
├── AGENTS.md                 — 跨 AI 工具入口
├── PROGRESS.md               — 阶段进度 + 待完成项 + 已知限制
├── CMakeLists.txt            — 顶层工程
├── partitions.csv            — 分区表
├── sdkconfig.defaults        — 默认配置
├── main/
│   ├── app_main.c            — 入口：任务创建 + 事件循环 + 连接轮询 + WDT + 堆监控
│   ├── app_state.c/.h        — 7 态状态机（按钮/连接事件 → 调功/确认/配网）
│   ├── app_events.h          — 事件类型定义（BTN_DOWN/REPEAT/UP/CONN_CHANGED/ERG_TARGET）
│   ├── button.c/.h           — GPIO 中断驱动按钮任务（引脚由 board_detect 运行时决定）
│   ├── led.c/.h              — LED 任务（通用板 GPIO12 四态 / M5Stamp SK6812 RGB）
│   ├── board_detect.c/.h     — 运行时板型检测（通用 ESP32-C3 / M5Stamp C3）
│   ├── ble_bikepower.c/.h    — NimBLE 五服务 + FTMS ERG
│   ├── nvs_store.c/.h        — NVS 配置持久化桥接
│   ├── radio_ctrl.c/.h       — BLE/WiFi 射频软切换（唯一 RF 开关）
│   ├── wifi_portal.c/.h      — HTTP 配网门户（v3.2.0 起仅 /api/xxx JSON 接口，HTML 由 web/ 内联）
│   ├── captive_portal.c/.h   — 轻量 DNS 劫持（UDP 53 → 192.168.4.1，自动弹配网页）
│   ├── web/                  — 前端三件套（v3.2.0 起，编译时由 scripts/inline_static.py 内联到固件）
│   │   ├── index.html        — SPA 入口（含 <!-- INLINE_CSS --> / <!-- INLINE_JS --> 占位符）
│   │   ├── app.css           — 统一所有视图样式
│   │   └── app.js            — SPA 路由 + XMLHttpRequest 调用 /api/xxx 自行渲染
│   └── bikepower_config.h    — 全局常量（引脚/阈值/UUID/版本）
├── components/bikepower_domain/   — 纯 C 领域层（可 host 单测）
│   ├── power_engine.c        — 功率/踏频数据引擎
│   ├── hr_model.c            — 心率模型
│   ├── profile.c             — 用户配置 + 范围校验
│   ├── config_store.c        — 配置 schema（defaults/validate/migrate/serialize）
│   ├── json_miniparse.c      — 轻量 JSON 字段提取
│   ├── button_logic.c        — 按钮语义纯逻辑
│   ├── button_debounce.c     — 去抖与长按节拍纯逻辑
│   ├── led_pattern.c         — LED 模式/颜色映射纯逻辑
│   ├── domain_rng.c          — 随机数
│   └── include/              — 头文件 + domain_config.h
├── scripts/
│   ├── inline_static.py      — 编译时把 web/app.css + web/app.js 内联到 web/index.html，生成 main/web_index_html.h
│   ├── flash_latest.sh       — 自动探测 ESP-IDF 路径与串口，编译并烧录最新正式固件
│   ├── build_all.sh          — 双板型 release 产物构建（merged bin + manifest）
│   ├── log_daemon.sh         — 串口日志守护包装脚本
│   ├── tools/                — 质量门禁 / 设备监控工具
│   └── platform/             — 平台相关构建入口
├── tests/
│   ├── ble_selftest.py       — bleak 五服务 + FTMS ERG 自测
│   ├── stage3_conn_test.py   — 连接事件驱动状态机验证
│   ├── stage5_portal_test.py — 配网门户 /api/xxx JSON 路由自测
│   ├── check_portal_js.py    — TC-3a：对 main/web/app.js 跑 node --check
│   ├── check_portal_structure.py — TC-3d：校验 index.html 结构 + 路由表一致性 + 前端 API 调用
│   ├── check_config_consistency.py — TC-3c：常量一致性校验
│   ├── check_nvs_schema.py   — TC-3e：NVS key/schema 静态一致性
│   ├── check_board_desc.py   — TC-3f：板型描述表静态一致性
│   ├── check_tasks_wdt.py    — TC-3g：任务/队列/WDT 静态完整性
│   ├── mock_server.py        — v3.2.0 起本地 mock /api/xxx 接口，前端不烧设备调试
│   ├── serial_capture.py     — 串口日志抓取工具
│   ├── serial_daemon.py      — 串口守护进程
│   ├── run_stress.sh         — 压力测试
│   └── README.md             — 测试脚本使用说明
├── test/test_domain          — host 单测（领域层）
├── docs/                     — 文档（docs/<category>/NN-kebab-case.md，见文档命名规范）
│   ├── INDEX.md              — 文档索引（根目录，不编号）
│   ├── user/                 — 产品使用说明
│   │   ├── 01-quickstart.md          — 快速上手
│   │   ├── 02-user-manual.md         — 用户手册
│   │   ├── 03-hardware.md            — 硬件指南
│   ├── dev/                  — 开发与技术
│   │   ├── 01-technical-reference.md — 技术参考（当前实现权威规格）
│   │   ├── 02-firmware-security.md   — 固件安全
│   │   └── 03-test-coverage-analysis.md — 测试覆盖率分析
│   ├── archive/              — 历史归档（仅供追溯）
│   │   ├── 01-cidf-nimble-rewrite.md — 基线重写方案
│   │   ├── 02-micropython-vs-c-comparison.md — 技术栈对比
│   │   ├── 03-review-checklist.md    — 审查清单
│   │   ├── 04-memory-and-feature-assessment.md — 内存与新功能评估
│   │   ├── 06-roadmap-v316.md        — v3.1.6 迭代规划（OTA 已于 v4.0.0 移除）
│   │   └── 07-remove-ota-plan.md     — v4.0.0 移除 OTA + 网页端精简方案
│   └── images/               — 配图（不编号）
├── .trae/rules/            — AI 规则（C/ESP-IDF 版）
│   ├── general-rules.md      — 项目结构 + 模块边界 + 提交/发版规则
│   ├── coding-style.md       — C/ESP-IDF 代码风格 + FreeRTOS 约束
│   ├── hardware-constraints.md — ESP32-C3 硬件约束
│   ├── wifi-ble-mutex.md     — WiFi/BLE 射频共用（硬约束）
│   ├── ai-challenge.md       — AI 质疑规则
│   ├── best-practices.md     — 项目优化审查规则（发版前）
│   ├── domain/              — BLE / WiFi 领域规则
│   └── workflows/           — 开发前检查/迭代/治理/测试/设备测试等流程规则
└── .trae/memory/             — 项目记忆
```

## 模块依赖图
```
        app_main.c (入口/事件循环/WDT/堆监控)
            │
   ┌────────┼────────┬─────────┬──────────┬───────────┐
   │        │        │         │          │           │
button.c  led.c  app_state.c  ble_bikepower.c  nvs_store.c  radio_ctrl.c→wifi_portal.c
   │        │        │         │          (board_detect.c 供 button/led 取引脚)
   └────────┴───→ app_queue (Queue)
                    │
            components/bikepower_domain (power_engine/hr_model/profile/config_store/button_logic/led_pattern)
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
| WiFi/BLE 射频共用 | radio_ctrl 统一管理 AP，配网 coex 共存 |
| 蓝牙优先 | app_state 长按 → 确认 → wifi_trigger |
| 配网后原地回 BLE | wifi_portal done 回调 → APP_EVT_WIFI_DONE → radio_ctrl_exit_wifi |
| 无循环依赖 | import/include 链为 DAG |

## 阶段路线（详见 PROGRESS.md）
| 阶段 | 内容 | 状态 |
|------|------|------|
| 0 | 工程骨架 | ✅ |
| 1 | BLE CPS 最小闭环 | ✅ |
| 2 | 领域层 + 五服务 + FTMS ERG | ✅ |
| 3 | 按钮 + LED + 状态机 | ✅ 代码完成（物理按钮待人工确认） |
| 4 | 配置持久化 NVS | ✅ |
| 5 | WiFi 配网（radio_ctrl + AP + HTTP） | ✅ 代码完成（配网真机流程待人工确认） |
| 6 | 稳定性与发版（WDT/coredump/老化/tag） | ✅ v3.0.0 已发版 |
| 7 | M5Stamp C3 适配（板型检测/SK6812/配网页修复） | ✅ v3.1.0 已发版 |
| 8-12 | WiFi/BLE 共存、Captive Portal、前后端分离、文档/测试梳理 | ✅ 已完成 |
| 13 | 移除 OTA + 家庭网络 + 网页端精简 | ✅ v4.0.0 代码完成（真机待确认项见 PROGRESS.md） |

## 发布流程
- 分支：GitHub Flow（main + feature/hotfix）。
- 版本号：SemVer `vMAJOR.MINOR.PATCH`，commit/tag/Release 三者一致。
- 发版前：编译通过 + 真机测试 + CHANGELOG 测试报告 + 打 tag（当前 v4.1.0）+ DIS 固件版本同步。
- **固件发行产物必须合并为单文件（强制）**：创建发行版时，必须用 `esptool.py merge_bin` 把 bootloader + partition-table + app 合并为单一 `.bin`，用户一次烧录即可，禁止分发分散的多个 bin 文件。
- Commit message：`<type>(<scope>): <简体中文概述>` + 逐文件正文 + `Co-authored-by: AI <ai@trae>` + `Model:`。
