# BikePower C Code Wiki

欢迎使用 BikePower C 代码知识库。本 Wiki 提供项目的结构化技术文档，面向开发者、维护者和后续接手者。

## 项目简介

**BikePower C** 是一个运行在 ESP32-C3 上的蓝牙骑行功率计模拟器，基于 ESP-IDF v5.5.4 + NimBLE + FreeRTOS 开发。它将原 MicroPython 版本完整重写为纯 C，支持：

- 标准 BLE 骑行服务（CPS/HRS/CSC/FTMS/DIS/RSC）
- 功率、踏频、心率数据模拟（含平滑随机游走、HRV）
- FTMS ERG 控制支持
- 实体按钮操作与 LED 状态提示
- WiFi 配网门户（BLE/WiFi 共存模式）
- NVS 配置持久化与 schema 迁移
- 自动板型检测（通用 ESP32-C3 / M5Stamp C3）

**当前固件版本**：v4.1.0

---

## Wiki 目录

### 核心文档

| 文档 | 说明 |
|------|------|
| [01-整体架构](01-architecture.md) | 三层架构设计、模块依赖图、目录结构 |
| [02-应用层模块](02-app-layer.md) | main/ 目录各模块职责、关键函数 |
| [03-领域层模块](03-domain-layer.md) | components/bikepower_domain/ 纯 C 算法模块 |
| [04-关键数据结构](04-key-data-structures.md) | power_engine_t、snapshot_t、配置结构体等 |
| [05-状态机与事件](05-state-machine-and-events.md) | 7 态状态机、事件队列、按钮语义 |
| [06-运行时流程](06-runtime-flow.md) | 启动顺序、主循环节拍、数据流 |
| [07-BLE 服务详解](07-ble-services.md) | 五服务 UUID、通知机制、FTMS 控制点 |
| [08-WiFi 配网门户](08-wifi-portal.md) | 射频共存、HTTP 路由、超时控制 |
| [09-构建与测试](09-build-and-test.md) | 编译命令、host 单测、真机自测脚本 |
| [10-硬件约束](10-hardware-constraints.md) | WiFi/BLE 共存、ISR 安全、内存阈值 |

### 快速参考

- [README.md](../../README.md) - 项目简介与用户指南
- [docs/dev/01-technical-reference.md](../dev/01-technical-reference.md) - 详细技术参考手册
- [AGENTS.md](../../AGENTS.md) - AI 助手协作规范
- [PROGRESS.md](../../PROGRESS.md) - 阶段进度与已知限制
- [CHANGELOG.md](../../CHANGELOG.md) - 版本变更历史

---

## 代码结构速览

```
bikepower-c/
├── main/                           # 应用层（板级代码，依赖 ESP-IDF）
│   ├── app_main.c                  # 入口：任务创建 + 事件循环 + WDT
│   ├── app_state.c/h               # 7 态状态机
│   ├── button.c/h                  # GPIO 中断驱动按钮任务
│   ├── led.c/h                     # LED 灯效任务（GPIO/SK6812）
│   ├── board_detect.c/h            # 运行时板型检测
│   ├── ble_bikepower.c/h           # NimBLE 五服务 + FTMS ERG
│   ├── ble_scanner.c/h             # BLE Central/Observer（模板库扫描）
│   ├── radio_ctrl.c/h              # BLE/WiFi 射频软切换（唯一 RF 入口）
│   ├── wifi_portal.c/h             # HTTP 配网门户（/api/xxx JSON）
│   ├── captive_portal.c/h          # DNS 劫持（Captive Portal 自动弹窗）
│   ├── nvs_store.c/h               # NVS 配置持久化桥接
│   ├── device_template_store.c/h   # BLE 设备信息模板库存储
│   ├── web/                        # 前端 SPA（编译时内联）
│   │   ├── index.html
│   │   ├── app.css
│   │   └── app.js
│   └── bikepower_config.h          # 应用层常量中心
│
├── components/bikepower_domain/    # 领域层（纯 C，可 host 单测，无 ESP-IDF 依赖）
│   ├── power_engine.c              # 功率/踏频/心率快照生成
│   ├── hr_model.c                  # 心率模型（FTP 分区/平滑/HRV/限速）
│   ├── running_engine.c            # 跑步模式引擎
│   ├── profile.c                   # 用户档案（体重/FTP/阈值心率/配速机器人）
│   ├── config_store.c              # 配置 schema/defaults/validate/migrate/serialize
│   ├── json_miniparse.c            # 轻量 JSON 字段提取
│   ├── button_logic.c              # 按钮语义纯逻辑
│   ├── button_debounce.c           # 去抖与长按节拍纯逻辑
│   ├── led_pattern.c               # LED 模式/颜色映射纯逻辑
│   ├── device_template.c           # 设备信息模板领域逻辑
│   ├── device_analysis.c           # 外部设备运动数据分析
│   ├── domain_rng.c                # 领域层随机数
│   └── include/                    # 公开头文件 + domain_config.h
│
├── scripts/                        # 构建、烧录、工具脚本
├── tests/                          # 设备测试、静态门禁、mock 工具
├── test/                           # 领域层 host 单测
└── docs/                           # 文档（user/dev/archive/wiki/images）
```

---

## 关键设计原则

1. **三层分离**：应用层（设备协调）、领域层（纯算法，可 host 测）、硬件层（ESP-IDF）
2. **BLE 优先**：配网默认保持 BLE 在线，WiFi 启停必须经 `radio_ctrl`
3. **事件驱动**：按钮/BLE 事件统一入 `app_queue`，由状态机串行消费
4. **静态预分配**：BLE 通知缓冲区全部静态，避免回调内动态分配
5. **脏标记落盘**：配置变更打 `dirty`，主循环 10s 心跳自动持久化
6. **看门狗覆盖**：主循环 + LED 任务订阅 WDT，按钮任务空闲阻塞不订阅
7. **领域层纯净**：components/bikepower_domain 不 include ESP-IDF 头文件

---

## 运行时架构图

应用入口、模块依赖与数据流：

```
                    app_main.c (入口/事件循环/WDT/堆监控)
                        │
   ┌────────┬───────────┼───────────┬────────────┬─────────────┐
   │        │           │           │            │             │
button.c  led.c     app_state.c  ble_bikepower.c  nvs_store.c  radio_ctrl.c → wifi_portal.c + captive_portal.c
   │        │           │           │            │             │      ↑
   └────────┴────→ app_queue (QueueHandle_t, 深度 16)          │      │
                       │                                       │      │
                components/bikepower_domain                    │      │
                (power_engine / hr_model / profile /           │      │
                 config_store / running_engine / ...)          │      │
                                                               ↓      │
                                                         ble_scanner.c
                                                  (模板库 Central/Observer)
```

**运行时数据流**：

```
GPIO 按钮中断 → button_task → app_queue → app_state_handle_event
                                                          │
BLE 连接轮询(app_main) → APP_EVT_CONN_CHANGED ──────────────┤→ 调功/确认/配网触发
                                                          │→ led_set_pattern
FTMS 控制点回调 → on_erg_target → app_queue ────────────────┤
                                                          │
power_engine_snapshot → ble_bikepower_set_data → BLE 通知定时器 → notify_tick()
```

---

## 快速开始（开发者）

### 本地编译烧录

```bash
# 自动探测 ESP-IDF 和串口，编译烧录并打开监视器
./scripts/flash_latest.sh

# 手动流程
idf.py set-target esp32c3
idf.py build
idf.py -p <port> flash monitor
```

### 运行测试

```bash
# 领域层 host 单测
cd test && make clean && make && ./test_domain

# 静态门禁聚合检查
python3 scripts/tools/quality_gate.py

# BLE 真机自测（设备已烧录并广播）
python3 tests/ble_selftest.py
```

### 发行版构建

```bash
./scripts/build_all.sh          # 双板型完整构建 + 门禁 + 产物
./scripts/build_all.sh --dry-run # 预览，不编译/不写文件
```

更多详情见各章节文档。
