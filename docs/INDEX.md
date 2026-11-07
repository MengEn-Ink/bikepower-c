# BikePower C 文档索引

源码是实现依据；[PROGRESS](../PROGRESS.md) 记录当前进度及待验收项，[技术参考](dev/01-technical-reference.md) 说明当前行为，[规则](../.trae/rules/general-rules.md) 定义约束。Wiki 展开代码细节，`archive/` 只保存历史设计与证据。

## 用户入口

| 文档 | 用途 |
|---|---|
| [项目首页](../README.md) | 能力、版本边界、使用与开发入口 |
| [快速上手](user/01-quickstart.md) | 首次烧录、连接与恢复 |
| [用户手册](user/02-user-manual.md) | 按钮、配网、模拟、真实来源与跑步 |
| [硬件指南](user/03-hardware.md) | 双板接线、检测与供电 |
| [手机离线手册](user/pages-mobile-guide.html) | 从 Pages 手机手册生成的离线阅读版 |
| [在线工具台](https://mengen-ink.github.io/bikepower-c/) | 正式固件刷写、BLE 工具与手机手册 |

## 当前开发文档

| 文档 | 用途 |
|---|---|
| [技术参考](dev/01-technical-reference.md) | 运行时、协议、配置和门户行为 |
| [固件安全](dev/02-firmware-security.md) | 编译加固及硬件安全方案 |
| [测试覆盖分析](dev/03-test-coverage-analysis.md) | 模块覆盖与证据边界 |
| [跑步兼容性](dev/04-running-mode-compatibility.md) | 跑步 App、RSC/FTMS 适配口径 |
| [操作门禁](dev/05-operation-test-gate.md) | 操作覆盖矩阵与人工项 |
| [Windows 构建](dev/08-windows-build-guide.md) | 环境、编译与排障 |
| [智能家居调研](dev/09-smart-home-integration.md) | 尚未实施的接入路线 |
| [源码事实](dev/21-generated-project-facts.md) | 自动生成的版本、schema、路由与模块 |
| [主备桥接规范](dev/28-bridge-failover-multi-device-design.md) | 当前三设备恢复与降级设计，实物验收待完成 |
| [仓库治理报告](dev/31-repository-hygiene-report.md) | 本轮分支、代码、文档、资源与部署验证 |
| [BLE 兼容性诊断](dev/32-ble-compatibility-diagnosis.md) | 标准特征补齐、固定快照、连接交接与目标端实测 |
| [BLE 标准补测](dev/33-ble-standard-test-plan.md) | Feature/范围/广播/控制事务回归及实物验收边界 |
| [BLE 能力复核](dev/34-ble-compatibility-recheck.md) | 当前 USB 连接证据、标准功率计差异与真机验收边界 |
| [设备遥测实施](dev/35-device-telemetry-plan.md) | 内存/CPU/BLE 快照、协议、Pages 与验证计划 |
| [设备遥测协议与验证](dev/36-device-telemetry-verification.md) | 20 字节记录、CPU 口径、浏览器回归与实测边界 |
| [项目审查与优化](dev/37-project-review-and-optimization.md) | 配置保存修复、字符串与测试退出回归、残余风险和证据 |
| [持久化稳定性加固](dev/38-persistence-stability-hardening.md) | 双槽快照、联合提交、迁移与故障恢复、严格门禁 |
| [Pages 操作演示](dev/39-pages-operation-demos.md) | 合宙官方参考、BOOT 配网/保存/校正/连接交接、动画交付与验证 |
| [手机实体演示方案与实施](dev/40-phone-operation-demo-plan.md) | 设备/手机两类入口、真实页面复用、隔离与对齐验收 |
| [Pages 全站布局更新](dev/41-pages-layout-refresh.md) | 轻导航、演示主导首页、工具页排版与回归边界 |
| [骑行端适配差异采集](dev/42-cycling-compatibility-capture.md) | 两侧独立保存、顺序/双连、差异解析与补齐方向 |
| [BINAVI连接状态修复](dev/44-binavi-connection-fix.md) | Host活跃连接确认、订阅交接、最小候选及实物验收 |
| [BINAVI连接问题结项](dev/45-binavi-connection-closeout.md) | 根因、修复与码表实测结论，心率项按用户决定结项 |
| [鬥骑序列号修复](dev/43-qidou-serial-fix.md) | USB证据、非空默认序列号、配置保留刷写与待App复测 |
| [鬥骑会话加密修复](dev/46-qidou-session-security-fix.md) | 无绑定安全存储、手机配对/控制成功与未闭合边界 |

## Code Wiki

| 文档 | 用途 |
|---|---|
| [首页](wiki/00-home.md) | 代码导航 |
| [整体架构](wiki/01-architecture.md) | 分层与依赖 |
| [应用层](wiki/02-app-layer.md) | 主任务、状态机、外设与桥接 |
| [领域层](wiki/03-domain-layer.md) | 模拟、配置与纯 C 算法 |
| [数据结构](wiki/04-key-data-structures.md) | 公开结构与字段 |
| [状态机](wiki/05-state-machine-and-events.md) | 按钮、事件与状态 |
| [运行时数据流](wiki/06-runtime-flow.md) | 启动、50ms 主循环、1000ms 通知 |
| [BLE 服务](wiki/07-ble-services.md) | GATT、编解码与控制点 |
| [WiFi 门户](wiki/08-wifi-portal.md) | API、事务与共存生命周期 |
| [构建与测试](wiki/09-build-and-test.md) | 开发、双板构建、真机与发布流程 |
| [硬件约束](wiki/10-hardware-constraints.md) | 引脚、内存、RF 与 ISR |

## 历史归档

已执行计划、阶段设计与旧验收报告保留原时间、镜像身份和未覆盖项，不作为当前候选验收结论。

| 归档 | 内容 |
|---|---|
| [01 重写](archive/01-cidf-nimble-rewrite.md)、[02 技术栈对比](archive/02-micropython-vs-c-comparison.md) | 早期选型 |
| [03 审查](archive/03-review-checklist.md)、[04 内存评估](archive/04-memory-and-feature-assessment.md) | 早期验证 |
| [06 路线图](archive/06-roadmap-v316.md)、[07 移除 OTA](archive/07-remove-ota-plan.md) | 旧功能与分区演进 |
| [08 跑步方案](archive/08-running-mode-full-upgrade-plan.md)、[09 跑步计划](archive/09-running-mode-implementation-plan.md) | 历史跑步设计 |
| [10 优化设计](archive/10-project-optimization-design.md)、[11 优化计划](archive/11-project-optimization-plan.md) | 工程治理 |
| [12 证据链设计](archive/12-stable-delivery-design.md)、[13 证据链计划](archive/13-stable-delivery-plan.md) | 发布门禁演进 |
| [14 协议稳定性](archive/14-top5-protocol-stability-design.md) | CPS/FTMS 与可靠性 |
| [15 自动优化计划](archive/15-automatic-optimization-plan.md)、[16 异常测试计划](archive/16-stability-exception-test-plan.md) | 已执行测试步骤 |
| [17 运动交互计划](archive/17-wifi-portal-sport-mode-ux-plan.md)、[18 主备计划](archive/18-bridge-failover-multi-device-implementation-plan.md) | 已执行候选实现步骤 |
| [19 项目复审](archive/19-project-review-and-optimization.md) | 历史问题清单与处理状态 |
| [20 真实桥接设计](archive/20-real-power-bridge-web-design.md)、[21 门户设计](archive/21-core-portal-rewrite-design.md) | 已被当前规范吸收的阶段方案 |
| [22 门户计划](archive/22-core-portal-bridge-implementation-plan.md)、[23 门户报告](archive/23-core-portal-bridge-test-report.md) | 历史执行与设备证据 |
| [24 Pages 设计](archive/24-pages-layout-motion-design.md)、[25 Pages 计划](archive/25-pages-experience-implementation-plan.md) | 工具台设计演进 |
| [26 自动优化报告](archive/26-automatic-optimization-report.md)、[27 Pages 报告](archive/27-pages-experience-test-report.md) | 历史自动化证据 |
| [28 异常报告](archive/28-stability-exception-test-report.md)、[29 运动交互设计](archive/29-wifi-portal-sport-mode-ux-design.md) | 历史稳定性与交互证据 |
| [30 源码清理报告](archive/30-local-cleanup-verification-report.md) | 上一轮清理和未验收边界 |

## 配图与维护

配图唯一来源为 `images/`：[引脚](images/hardware-pinout.svg)、[架构](images/architecture.svg)、[按钮状态](images/button-led-state.svg)、[主循环](images/main-loop-flow.svg)、[WiFi 共存](images/rf-wifi-flow.svg) 与[真实串口选择截图](images/web-flash-serial-port.jpg)。Pages 构建只分发实际引用的图片。

[AGENTS](../AGENTS.md) · [更新日志](../CHANGELOG.md) · [项目规则](../.trae/rules/general-rules.md) · [编码风格](../.trae/rules/coding-style.md) · [硬件约束](../.trae/rules/hardware-constraints.md) · [RF 共存](../.trae/rules/wifi-ble-mutex.md)
