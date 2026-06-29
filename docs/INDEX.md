# BikePower C — 文档索引

项目文档概览，按读者群体划分。文档命名规范见 [general-rules.md](../.trae/rules/general-rules.md)「文档命名规范」章节。

> 更新日期：2026-06-29（v3.2.0）

---

## 🚴 普通用户 / 骑行爱好者

| 文档 | 说明 |
|------|------|
| [快速上手](01-quickstart.md) | 第一次使用 BikePower C 的全流程指南 |
| [README](../README.md) | 项目简介、功能清单、配图说明、引脚表、LED 灯效 |
| [用户手册](02-user-manual.md) | 日常使用：按钮操作、骑行模式、配网、LED 状态 |

## 🔧 开发者 / DIY 爱好者

| 文档 | 说明 |
|------|------|
| [硬件指南](03-hardware.md) | 推荐开发板、引脚定义、接线、供电、排障 |
| [技术参考](04-technical-reference.md) | 模块边界、依赖图、状态机、API 路由、测试入口 |
| [重写方案](05-cidf-nimble-rewrite.md) | 从 MicroPython 到 C 的分阶段重写路线图 |
| [MicroPython vs C 对比](06-micropython-vs-c-comparison.md) | 两种技术栈的性能/内存/复杂度对比 |
| [固件防提取/防反编译](07-firmware-security.md) | 编译层加固（已实现）+ Flash 加密/Secure Boot 方案与选型 |
| [内存与新功能评估](09-memory-and-feature-assessment.md) | v3.1.3/v5.5.4 各场景内存实测、满负载分析、OTA/Captive Portal/FTMS 扩展可行性 |
| [OTA 与 Captive Portal 落地方案](10-ota-implementation-plan.md) | Captive Portal 实施总结 + OTA 详细方案（分区表/上传协议/回滚自检/防降级/实施清单） |
| [OTA 固件升级操作手册](11-ota-user-guide.md) | 面向测试/客户的 OTA 升级操作指南（配网/上传/升级步骤/常见问题排查） |
| [v3.1.6 迭代规划](12-roadmap-v316.md) | FTMS 扩展（坡度/阻力/范围特征）+ 内存优化（WiFi 残留释放/HTTP 缓冲复用）可行性 |
| [测试覆盖率分析](test-coverage-analysis.md) | 各测试规则（TC-1 ~ TC-7）的覆盖范围、断言数、跳过策略 |

## 🤖 AI 助手

### 入口与记忆

| 文档/规则 | 说明 |
|----------|------|
| [AGENTS.md](../AGENTS.md) | AI 助手入口，必读优先级、硬性规则、验证要求 |
| [project-context.md](../.trae/memory/project-context.md) | 项目记忆（用户偏好、架构决策、踩坑记录） |

### 治理层规则（`.trae/rules/`，tier-1 极稳定）

| 规则 | 说明 |
|------|------|
| [coding-style.md](../.trae/rules/coding-style.md) | C/ESP-IDF 代码风格 + FreeRTOS 约束（命名/模块结构/日志/内存/禁止事项） |
| [hardware-constraints.md](../.trae/rules/hardware-constraints.md) | ESP32-C3 硬件约束（引脚/内存/中断/共存） |
| [wifi-ble-mutex.md](../.trae/rules/wifi-ble-mutex.md) | WiFi/BLE 射频共用规则（硬约束，配网走 coex 共存） |
| [ai-challenge.md](../.trae/rules/ai-challenge.md) | AI 质疑规则（不谄媚、发现漏洞主动提出 + 操作权限分级 Allow/Ask/Deny） |

### 项目层规则（`.trae/rules/`，tier-2 随项目变更）

| 规则 | 说明 |
|------|------|
| [general-rules.md](../.trae/rules/general-rules.md) | 项目结构、模块边界、依赖图、提交流程、文档命名规范 |
| [best-practices.md](../.trae/rules/best-practices.md) | 项目优化审查规则（发版前 7 大审查 + 分批提交 + 输出报告） |

### 领域层规则（`.trae/rules/domain/`）

| 规则 | 说明 |
|------|------|
| [domain/ble.md](../.trae/rules/domain/ble.md) | BLE 五服务（CPS+HRS+CSC+FTMS+DIS）+ FTMS ERG 控制点契约 |
| [domain/wifi.md](../.trae/rules/domain/wifi.md) | WiFi 配网门户 HTTP 路由表（v3.2.0 起 15 个 /api/xxx JSON 接口）+ Captive Portal 探测端点 |

### 工作流规则（`.trae/rules/workflows/`）

| 规则 | 说明 |
|------|------|
| [workflows/pre-dev-checklist.md](../.trae/rules/workflows/pre-dev-checklist.md) | 开发前检查清单（PDC-1 ~ PDC-7） |
| [workflows/test-coverage.md](../.trae/rules/workflows/test-coverage.md) | 测试覆盖规则（TC-1 host 单测 ~ TC-8 web 资源） |
| [workflows/device-testing.md](../.trae/rules/workflows/device-testing.md) | 设备测试流程（烧录 → 串口 → BLE → 配网 → OTA） |
| [workflows/context-engineering.md](../.trae/rules/workflows/context-engineering.md) | 上下文工程（先读哪些规则再开工） |
| [workflows/iteration-state.md](../.trae/rules/workflows/iteration-state.md) | 迭代状态管理（PROGRESS/CHANGELOG 同步） |
| [workflows/entropy-governance.md](../.trae/rules/workflows/entropy-governance.md) | 熵治理（防止规则/文档腐化） |
| [workflows/task-templates.md](../.trae/rules/workflows/task-templates.md) | 任务模板（bug 修复 / 新功能 / 重构） |

## 📊 配图

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
| [项目进度](../PROGRESS.md) | 阶段 0-11 完成情况、待确认项、已知限制 |
| [更新日志](../CHANGELOG.md) | 版本历史与变更记录 |
| [优化审查清单](08-review-checklist.md) | v3.1.0 多板型/发行流程/配网网页全链路复查记录（已完成） |

## 🧪 测试

| 入口 | 说明 |
|------|------|
| [tests/README.md](../tests/README.md) | 测试脚本总览（静态门禁 / 动态自测 / OTA / Mock / 串口 / 压力 / 运行器）+ test/ 与 tests/ 合并评估 |
| [test/test_domain.c](../test/test_domain.c) | 领域层 host 单测（纯 C，make -C test && ./test/test_domain） |
