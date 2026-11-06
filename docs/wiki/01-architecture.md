# 01 - 整体架构

本文描述 BikePower C 的三层软件架构、模块依赖关系和设计决策。

---

## 三层架构概览

项目采用严格的三层分离设计，职责边界直接映射到代码目录结构：

| 层级 | 目录 | 依赖 | 职责 |
|------|------|------|------|
| 应用层 | `main/` | ESP-IDF、FreeRTOS、NimBLE、领域层 | 设备协调：按钮、LED、BLE、WiFi、状态机、主循环 |
| 领域层 | `components/bikepower_domain/` | 纯 C 标准库（无 ESP-IDF） | 骑行算法：功率/心率模型、用户配置、按钮语义、JSON 解析 |
| 硬件/平台层 | ESP-IDF 组件 | — | FreeRTOS 调度、NimBLE 协议栈、WiFi、NVS、GPIO、RMT |

### 架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                        应用层 (main/)                           │
│  ┌─────┐ ┌─────┐ ┌──────────┐ ┌───────────┐ ┌───────────────┐  │
│  │app_ │ │app_ │ │  button  │ │  led      │ │ ble_bikepower │  │
│  │main │ │state│ │          │ │           │ │               │  │
│  └──┬──┘ └──┬──┘ └────┬─────┘ └─────┬─────┘ └──────┬────────┘  │
│     │       │         │             │              │           │
│     └───────┴──── app_queue ────────┴──────────────┘           │
│                        │                                       │
│  ┌─────────────┐ ┌─────┴──────┐ ┌────────────┐ ┌────────────┐  │
│  │ radio_ctrl  │ │wifi_portal │ │captive_    │ │ nvs_store  │  │
│  │ (RF 唯一入口)│ │(HTTP 门户) │ │portal(DNS) │ │ (NVS桥接)  │  │
│  └──────┬──────┘ └────────────┘ └────────────┘ └─────┬──────┘  │
│         │                                           │         │
└─────────┼───────────────────────────────────────────┼─────────┘
          │                                           │
┌─────────┼───────────────────────────────────────────┼─────────┐
│         ▼                                           ▼         │
│  ┌─────────────────────────────────────────────────────────┐  │
│  │              领域层 (bikepower_domain)                   │  │
│  │  ┌──────────┐ ┌────────┐ ┌───────┐ ┌────────────┐      │  │
│  │  │power_    │ │hr_model│ │profile│ │config_store│      │  │
│  │  │engine    │ │        │ │       │ │            │      │  │
│  │  └──────────┘ └────────┘ └───────┘ └────────────┘      │  │
│  │  ┌──────────┐ ┌────────┐ ┌───────┐ ┌────────────┐      │  │
│  │  │running_  │ │button_ │ │led_   │ │json_       │      │  │
│  │  │engine    │ │logic   │ │pattern│ │miniparse   │      │  │
│  │  └──────────┘ └────────┘ └───────┘ └────────────┘      │  │
│  │  ┌──────────┐ ┌────────┐ ┌───────┐                      │  │
│  │  │device_   │ │device_ │ │domain_│                      │  │
│  │  │template  │ │analysis│ │rng    │                      │  │
│  │  └──────────┘ └────────┘ └───────┘                      │  │
│  └─────────────────────────────────────────────────────────┘  │
│                        领域层（纯 C）                           │
└─────────────────────────────────────────────────────────────────┘
                              │
┌─────────────────────────────┼───────────────────────────────────┐
│                             ▼                                   │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │              硬件/平台层 (ESP-IDF v5.5.4)                  │ │
│  │  FreeRTOS  │  NimBLE  │  ESP WiFi  │  NVS  │  GPIO/RMT   │ │
│  └───────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

参考图片：[架构](../images/architecture.svg)

真实功率桥接由应用层 `bridge_runtime` 协调：它在主任务中消费纯 C
`bridge_failover` 的动作，驱动单设备 `ble_power_bridge` 会话和手动
`ble_scanner`，并把运行时快照同步给门户。自动恢复只做已知地址的定向连接，
不会在后台启动扫描。

---

## 模块依赖关系（DAG）

```
app_main.c
   ├── app_state.c
   │     └── power_engine.h (领域层)
   ├── button.c
   │     └── board_detect.h (引脚)
   ├── led.c
   │     └── board_detect.h (引脚/驱动类型)
   ├── ble_bikepower.c
   │     └── ble_config_service.c (八组配置事务)
   ├── bridge_runtime.c
   │     ├── bridge_failover.c (领域层主备状态机)
   │     ├── ble_power_bridge.c (单设备上游 CPS 会话)
   │     └── ble_scanner.c (仅手动扫描)
   ├── radio_ctrl.c
   │     └── wifi_portal.c
   │           └── captive_portal.c
   ├── nvs_store.c
   │     └── config_store.h (领域层)
   ├── device_identity_store.c
   │     └── device_identity.h (领域层)
   └── board_detect.c
```

**关键约束**：
- 依赖链必须是有向无环图（DAG），禁止循环依赖
- 领域层不得 include 任何 ESP-IDF 头文件
- WiFi/BLE 射频操作唯一入口是 `radio_ctrl.c`，禁止其他模块直接调用 `esp_wifi_*`
- 应用层模块间通过 `app_queue` 事件通信，不直接调用彼此内部状态

---

## 任务模型

固件运行在 FreeRTOS 上，主要任务如下：

| 任务名 | 创建位置 | 栈大小 | 优先级 | 职责 |
|--------|----------|--------|--------|------|
| `app_main_task` | [app_main.c](../../main/app_main.c) 的 `app_main()` | 系统默认 | 1（main） | 50ms 节拍主循环：事件消费、快照生成、BLE 数据更新、WDT 喂狗 |
| `nimble_host` | `ble_bikepower_start()` 内部 | NimBLE 默认 | 5 | NimBLE 协议栈主机任务、GATT 回调、通知打包 |
| `button_task` | [button.c](../../main/button.c) | 2560B | 5 | GPIO 中断信号量等待、去抖、长按自动重复、事件投递 |
| `led_task` | [led.c](../../main/led.c) | 2048B | 3 | LED 模式/颜色/闪烁节拍驱动（GPIO 或 SK6812 RMT） |
| `httpd` | `wifi_portal_start()` 内部 | 12288B | 5 | HTTP 服务器任务，处理 /api/xxx 请求 |
| `sys_evt` | ESP-IDF 系统事件 | 默认 | — | WiFi/IP 事件处理 |
| `tiT` | ESP-IDF 定时器 | 默认 | — | esp_timer 回调（BLE 通知、配网超时检查） |

**任务间通信**：
- `app_queue`（QueueHandle_t，深度 16）：按钮事件、连接变化、ERG 目标、配网结束等统一投递点
- `s_btn_sem`（SemaphoreHandle_t）：GPIO ISR → button_task 唤醒信号
- 共享输出 `s_output`：主循环锁内整体发布，通知定时器每轮采集一次；静态 payload/动态长度同样受短临界区保护，生命周期单值使用 C11 原子
- 桥接运行时：主任务独占 `bridge_runtime` 与 `bridge_failover`，Portal 只读取同步快照并经 callback mailbox 投递扫描、测试、暂停、恢复或立即重试命令

---

## 目录结构详解

### 根目录

| 文件/目录 | 说明 |
|-----------|------|
| `CMakeLists.txt` | 顶层 ESP-IDF 工程 |
| `partitions.csv` | 分区表（单 factory，无 OTA，含 coredump） |
| `sdkconfig.defaults` | 默认 SDK 配置（coex、NimBLE、coredump 等） |
| `README.md` | 项目简介、用户快速上手、构建入口 |
| `AGENTS.md` | AI 助手协作规范 |
| `PROGRESS.md` | 阶段进度与待确认项 |
| `CHANGELOG.md` | 版本变更记录 |
| `LICENSE` | 许可证 |

### `main/` — 应用层

模块数量以 [源码事实摘要](../dev/21-generated-project-facts.md) 为准，不手工维护行数。

| 文件 | 核心职责 |
|------|----------|
| [app_main.c](../../main/app_main.c) | 入口、50ms 主循环、WDT、mailbox 与持久化 |
| [app_state.c/h](../../main/app_state.c) | 状态机、按钮/确认窗口与配网协调 |
| [app_events.h](../../main/app_events.h) | 应用事件类型 |
| [button.c/h](../../main/button.c) | GPIO ISR 与按钮任务 |
| [led.c/h](../../main/led.c) | GPIO/RGB 双板灯效 |
| [board_detect.c/h](../../main/board_detect.c) | 板型描述、检测与固件名称 |
| [ble_bikepower.c/h](../../main/ble_bikepower.c) | 运动 GATT、广播、通知、FTMS 与生命周期 |
| [ble_config_service.c/h](../../main/ble_config_service.c) | 八组配置与 revision 事务 |
| [bridge_runtime.c/h](../../main/bridge_runtime.c) | 三设备恢复协调、实际来源选择、最近成功提示和 Portal 命令 |
| [ble_power_bridge.c/h](../../main/ble_power_bridge.c) | 单台已知 CPS 的定向连接、DIS、订阅与样本快照 |
| [ble_scanner.c/h](../../main/ble_scanner.c) | 用户主动触发的有界 CPS 扫描与候选代次 |
| [radio_ctrl.c/h](../../main/radio_ctrl.c) | 射频共存唯一入口与失败兜底 |
| [wifi_portal.c/h](../../main/wifi_portal.c) | HTTP JSON、配置事务、超时与静态页面 |
| [captive_portal.c/h](../../main/captive_portal.c) | DNS 与门户探测 |
| [nvs_store.c/h](../../main/nvs_store.c) | 旧键只读迁移与引擎配置搬运 |
| [persistent_store.c/h](../../main/persistent_store.c) | config/DIS 双槽、CRC、提交标记与故障闭锁 |
| [device_identity_store.c/h](../../main/device_identity_store.c) | DIS 旧键读取与联合快照接口 |
| [bikepower_config.h](../../main/bikepower_config.h) | 板级常量 |
| `web/` | 编译时内联 HTML/CSS/JS |

### `components/bikepower_domain/` — 领域层（纯 C，可 host 单测）

| 文件 | 核心职责 |
|------|----------|
| [power_engine.c](../../components/bikepower_domain/power_engine.c) | 功率/踏频/心率快照生成、Steady/Road/ERG 三种模式、双时间尺度骑行动态 |
| [hr_model.c](../../components/bikepower_domain/hr_model.c) | 负荷功率滤波、FTP 6 区映射与锚点压缩、心率平滑（tau 升 30s/降 60s）、HRV 慢游走与输出限速 |
| [running_engine.c](../../components/bikepower_domain/running_engine.c) | 跑步模式：速度/配速/步频生成、配速机器人、RSC 数据 |
| [profile.c](../../components/bikepower_domain/profile.c) | 用户档案（体重/FTP/阈值心率）、配速机器人表（Coco/Yumi/Jacques/Genie/Constance/自定义） |
| [power_bridge.c](../../components/bikepower_domain/power_bridge.c) | 三设备配置、wire v1/v2、校正、0W 保护与同源踏频 |
| [bridge_failover.c](../../components/bikepower_domain/bridge_failover.c) | 最近成功优先、两轮快速恢复、模拟降级和低频轮转纯状态机 |
| [config_store.c](../../components/bikepower_domain/config_store.c) | schema/defaults/校验/迁移/序列化；版本以源码事实摘要为准 |
| [config_snapshot.c](../../components/bikepower_domain/config_snapshot.c) | config/DIS 显式编码、头字段与负载 CRC、代次标记 |
| [json_miniparse.c](../../components/bikepower_domain/json_miniparse.c) | 轻量 JSON 字段提取器（无动态分配，用于 HTTP 请求解析） |
| [button_logic.c](../../components/bikepower_domain/button_logic.c) | 按钮按压语义纯逻辑（短按/长按/自动重复/确认窗口） |
| [button_debounce.c](../../components/bikepower_domain/button_debounce.c) | 去抖与长按节拍纯逻辑 |
| [led_pattern.c](../../components/bikepower_domain/led_pattern.c) | LED 模式（慢闪/快闪/常亮/熄灭/WiFi）与颜色映射纯逻辑 |
| [device_template.h](../../components/bikepower_domain/include/device_template.h) | 扫描与 DIS 身份复制共用的数据结构；模板库由 Pages 管理 |
| [device_analysis.c](../../components/bikepower_domain/device_analysis.c) | 外部 BLE 设备运动数据解析与能力摘要 |
| [domain_rng.c](../../components/bikepower_domain/domain_rng.c) | 领域层随机数（不直接用 esp_random，保持 host 可测） |
| `include/` | 公开头文件 + `domain_config.h`（领域层常量中心） |

### `scripts/` — 构建与工具脚本

| 脚本 | 说明 |
|------|------|
| `flash_latest.sh` / `.ps1` | 自动探测 ESP-IDF 与串口，reconfigure + build + flash + monitor |
| `build_all.sh` / `.ps1` | 双板型 release 构建：门禁 → 编译 → merged bin → SHA256 → 测试报告 → zip |
| `inline_static.py` | 编译时把 web/app.css + app.js 内联到 index.html，生成 web_index_html.h |
| `log_daemon.sh` | 串口日志守护进程包装脚本 |
| `tools/quality_gate.py` | 聚合静态门禁：host 单测、配置一致性、JS 语法、结构校验等 |
| `tools/device_acceptance.py` | 真机验收证据管理（PASS/FAIL/NOT_RUN） |
| `tools/check_release_artifacts.py` | Release 产物完整性校验 |
| `tools/generate_release_docs.py` | 生成 test-report.md |
| `tools/device_monitor.py` | 串口设备监控 |
| `tools/serve_ble_monitor.py` | BLE 监控本地服务器 |
| `scripts/tools/publish_github_release.py` | GitHub Release 发布（严格证据、tag 与附件检查） |

### `tests/` — 设备测试与门禁

| 脚本 | 说明 |
|------|------|
| `ble_selftest.py` | bleak 五服务 + FTMS ERG 自测 |
| `ble_broadcast_monitor.py` | BLE 广播监控 |
| `stage3_conn_test.py` | 连接事件驱动状态机验证 |
| `stage5_portal_test.py` | 配网门户 /api/xxx 路由自测 |
| `test_portal_config_save.py` | 配置保存 mock 测试（无需设备） |
| `test_core_flow_endurance.py` | 3 分钟核心流程耐久测试 |
| `mock_server.py` | 本地 mock /api/xxx 接口，前端独立调试 |
| `serial_daemon.py` | 串口日志守护进程（持久化缓冲、崩溃快照、日志轮转） |
| `serial_capture.py` | 一次性串口日志抓取 |
| `check_*.py` | 静态门禁脚本（配置一致性、NVS schema、JS 语法、板型描述、WDT/任务等） |
| `operation_gate.py` | 操作级门禁矩阵 |
| `run_stress.sh` | 压力测试 |

### `test/` — Host 单测

| 文件 | 说明 |
|------|------|
| `Makefile` | host 单测构建（gcc，不依赖 ESP-IDF） |
| [test_domain.c](../../test/test_domain.c) | 领域层单元测试（power_engine/hr_model/profile/config_store 等） |

---

## 编译时资源处理

前端资源（HTML/CSS/JS）在编译期通过 CMake `add_custom_command` 自动处理：

```
main/web/index.html  ─┐
main/web/app.css     ─┼→ scripts/inline_static.py → build/.../generated/web_index_html.h
main/web/app.js      ─┘                                               ↓
                                                         #include "web_index_html.h"
                                                         wifi_portal.c GET / 输出
```

**为什么内联**：iOS Captive Portal WebView（CNA）不会加载外部 CSS/JS 资源，必须全部内联到单个 HTML 中才能正常显示配网页面。

---

## 构建注入变量

`main/CMakeLists.txt` 在构建时通过 `target_compile_definitions` 注入：

| 宏 | 来源 | 用途 |
|----|------|------|
| `BIKEPOWER_GIT_SHA` | `git rev-parse --short HEAD` | DIS 固件版本字段、manifest、release 文件名 |
| `BIKEPOWER_BUILD_TIME` | CMake `string(TIMESTAMP ...)` | 构建时间，编译信息记录 |
| `PORTAL_TEST_AUTO_CONFIG` | CMake option（默认 OFF） | 测试固件：BLE 就绪 3s 后自动触发配网 |

---

## 分区表

[partitions.csv](../../partitions.csv) 使用单 factory 布局（v4.0.0 起移除 OTA）：

| 分区 | 大小 | 说明 |
|------|------|------|
| `nvs` | 24KB | NVS 配置存储 |
| `phy_init` | 4KB | PHY 初始化数据 |
| `factory` | 3.25MB | 固件（bootloader + partition-table + app 合并为单 bin） |
| `coredump` | 64KB | coredump 分区（写 flash） |

参考：[sdkconfig.defaults](../../sdkconfig.defaults) 中 `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`、`CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y`。
