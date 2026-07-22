# BikePower C — 文档索引

项目文档按维度分目录管理：`user/` 产品使用、`dev/` 开发与技术、`wiki/` Code Wiki（代码级完整参考）、`archive/` 历史归档、`images/` 配图。

---

## 📚 wiki/ — Code Wiki（代码级完整参考）

面向开发者，由代码直接生成/维护的结构化技术 Wiki，覆盖完整架构、模块职责、数据结构、运行时流程。

| 文档 | 说明 |
|------|------|
| [Code Wiki 首页](wiki/00-HOME.md) | Wiki 目录、代码结构速览、关键设计原则、运行时架构图 |
| [整体架构](wiki/01-architecture.md) | 三层架构概览、模块依赖 DAG、任务模型、目录详解、编译时资源处理、分区表 |
| [应用层模块](wiki/02-app-layer.md) | app_main、app_state、button、led、board_detect、radio_ctrl、captive_portal、nvs_store、device_template_store、ble_scanner |
| [领域层模块](wiki/03-domain-layer.md) | power_engine（三模式）、hr_model（四层心率）、running_engine、profile（配速机器人）、config_store（schema v11）、json_miniparse、button_logic、led_pattern |
| [关键数据结构](wiki/04-key-data-structures.md) | app_evt_t、board_desc_t、power_engine_t（26字段详表）、snapshot_t、fluct_walk_t、profile_t、hr_state_t、running_engine_t、config_t、led_config_t、device_template_t |
| [状态机与事件](wiki/05-state-machine-and-events.md) | 7 态状态迁移图、10 种事件类型、按钮语义阈值、交互流程、长按回滚机制 |
| [运行时数据流](wiki/06-runtime-flow.md) | 14 步启动流程、主循环 50ms 节拍、BLE 通知双时间尺度、配网完整时序、脏标记落盘、11 种异常恢复路径 |
| [BLE 服务详解](wiki/07-ble-services.md) | 5+1 服务 UUID、两种运动 profile GATT 表、6 种数据包格式、FTMS 控制点 OpCode/Result Code、广播参数、双时间尺度通知、g_notify_lock 线程安全、DIS 字段 |
| [WiFi 配网门户](wiki/08-wifi-portal.md) | HTTP 路由表（29 条）、超时状态机、JSON API 详解（50+字段）、Captive Portal 三层弹窗、临时副本原子替换、BLE 扫描/设备模板 API、前端内联 |
| [构建与测试](wiki/09-build-and-test.md) | idf.py 构建流程、CMake 配置、flash_latest.sh 一键烧录、build_all.sh 双板型发布、host 单测、静态门禁脚本、BLE 自测、mock server、产物校验 |
| [硬件约束与平台限制](wiki/10-hardware-constraints.md) | ESP32-C3 规格、双板型 GPIO 分配、内存预算（80KB/20KB 告警线）、WiFi/BLE 共存硬约束、ISR 安全规则、FreeRTOS 任务模型、WDT 配置、禁止事项 |

---

## 🚴 user/ — 产品使用说明

面向普通用户 / 骑行爱好者。

| 文档 | 说明 |
|------|------|
| [快速上手](user/01-quickstart.md) | 第一次使用：烧录固件 + 首次连接，其余细节链到手册 |
| [用户手册](user/02-user-manual.md) | 完整操作：按钮、LED、配网字段、骑行模式、FAQ、技术规格 |
| [硬件指南](user/03-hardware.md) | 推荐开发板、引脚定义、接线、供电、排障 |
| [README](../README.md) | 项目简介、功能清单、构建/门禁/Release 入口、配图说明、引脚表、LED 灯效 |

## 🔧 dev/ — 开发与技术

面向开发者 / 维护者 / 后续接手者。

| 文档 | 说明 |
|------|------|
| [技术参考](dev/01-technical-reference.md) | 三层架构、启动流程、BLE 服务、配网链路、配置 JSON、测试门禁（基于当前实现） |
| [固件防提取/防反编译](dev/02-firmware-security.md) | 编译层加固（已实现）+ Flash 加密/Secure Boot 方案与选型 |
| [测试覆盖率分析](dev/03-test-coverage-analysis.md) | 领域层/应用层模块清单、覆盖矩阵、固件随附测试报告规则 |
| [跑步模式兼容性调研与适配方案](dev/04-running-mode-compatibility.md) | Zwift/MyWhoosh 跑步模式 BLE 兼容口径、广播/GATT 数据需求、分阶段落地方案 |
| [操作覆盖矩阵与测试门禁](dev/05-operation-test-gate.md) | 模式切换、保存、按钮、网页、BLE、异常稳定性的操作级门禁矩阵与人工项记录规则 |
| [Windows 环境编译指南](dev/08-windows-build-guide.md) | Windows 下环境配置、编译流程、速度优化（ccache/Defender/Ninja）、排障 |
| [智能家居平台接入调研](dev/09-smart-home-integration.md) | 米家官方接入、Matter、Home Assistant/MQTT 网关等路径对比与分阶段建议 |
| [稳定交付证据链设计](dev/10-stable-delivery-design.md) | 真机自动测试、人工确认、release 报告与正式发布阻断的证据模型 |
| [稳定交付证据链实施计划](dev/11-stable-delivery-plan.md) | 按证据模型、device 门禁采集、release 强校验分批实施的测试驱动计划 |

## 🗄️ archive/ — 历史 / 归档（仅供追溯）

以下文档对应历史版本或已完成的一次性工作，**不代表 v4.0.0 当前实现**，仅作背景追溯。

| 文档 | 说明 |
|------|------|
| [重写方案](archive/01-cidf-nimble-rewrite.md) | 从 MicroPython 到 C 的分阶段重写路线图（早期设计背景） |
| [MicroPython vs C 对比](archive/02-micropython-vs-c-comparison.md) | 两种技术栈的性能/内存/复杂度对比（选型阶段） |
| [优化审查清单](archive/03-review-checklist.md) | v3.1.0 多板型/发行流程/配网网页全链路复查（已完成） |
| [内存与新功能评估](archive/04-memory-and-feature-assessment.md) | v3.1.3 各场景内存实测、满负载分析、扩展可行性 |
| [v3.1.6 迭代规划](archive/06-roadmap-v316.md) | 在线 OTA + FTMS 扩展 + 内存优化规划（OTA 已于 v4.0.0 移除，仅供追溯） |
| [移除 OTA + 网页端精简方案](archive/07-remove-ota-plan.md) | v4.0.0 移除在线 OTA + 家庭网络、分区表改单 factory、网页端精简方案 |
| [历史跑步模式完整升级方案](archive/08-running-mode-full-upgrade-plan.md) | 历史方案，包含已移除的坡度/控制点设想，仅供追溯 |
| [历史跑步模式完整升级实施计划](archive/09-running-mode-implementation-plan.md) | 历史执行计划，包含已移除的坡度/控制点批次，仅供追溯 |
| [项目优化设计归档](archive/10-project-optimization-design.md) | 历史优化设计，当前结果已合入代码、门禁和文档 |
| [项目优化执行计划归档](archive/11-project-optimization-plan.md) | 历史执行计划，原 `docs/superpowers` 工作目录已归档 |

## 🤖 AI 助手

| 文档/规则 | 说明 |
|----------|------|
| [AGENTS.md](../AGENTS.md) | AI 助手入口，必读优先级、硬性规则、验证要求 |
| [project-context.md](../.trae/memory/project-context.md) | 项目记忆（用户偏好、架构决策、踩坑记录） |
| [general-rules.md](../.trae/rules/general-rules.md) | 项目结构、模块边界、提交流程、文档命名规范 |
| [coding-style.md](../.trae/rules/coding-style.md) | C/ESP-IDF 代码风格 |
| [hardware-constraints.md](../.trae/rules/hardware-constraints.md) | ESP32-C3 硬件约束 |
| [wifi-ble-mutex.md](../.trae/rules/wifi-ble-mutex.md) | WiFi/BLE 射频共用规则 |

## 📊 images/ — 配图

| 图片 | 说明 |
|------|------|
| ![硬件引脚对照](images/hardware-pinout.png) | 通用 ESP32-C3 与 M5Stamp C3 的按钮、LED、板型检测差异 |
| ![架构图](images/architecture.png) | 三层软件架构：应用层→领域层→硬件层（双板适配） |
| ![按钮&LED 状态机](images/button-led-state.png) | 按钮操作、LED 四态、状态迁移 |
| ![主循环数据流](images/main-loop-flow.png) | 50ms 节拍的事件消费→BLE通知全流程 |
| ![WiFi/BLE 射频共用流程](images/rf-wifi-flow.png) | 二次确认进入配网：BLE 保持在线→启 WiFi→保存/超时→关闭 WiFi 回 BLE |

## 📋 进度与变更

| 文档 | 说明 |
|------|------|
| [项目进度](../PROGRESS.md) | 阶段 0-14 完成情况、第七轮 P0 真机验收清单、待确认项、已知限制 |
| [更新日志](../CHANGELOG.md) | 版本历史与变更记录 |
