# BikePower C — 文档索引

项目文档概览，按读者群体划分。

---

## 🚴 普通用户 / 骑行爱好者

| 文档 | 说明 |
|------|------|
| [快速上手](01-quickstart.md) | 第一次使用 BikePower C 的全流程指南 |
| [README](../README.md) | 项目简介、功能清单、配图说明、引脚表、LED 灯效 |

## 🔧 开发者 / DIY 爱好者

| 文档 | 说明 |
|------|------|
| [硬件指南](03-hardware.md) | 推荐开发板、引脚定义、接线、供电、排障 |
| [重写方案](05-cidf-nimble-rewrite.md) | 从 MicroPython 到 C 的分阶段重写路线图 |
| [MicroPython vs C 对比](06-micropython-vs-c-comparison.md) | 两种技术栈的性能/内存/复杂度对比 |
| [固件防提取/防反编译](07-firmware-security.md) | 编译层加固（已实现）+ Flash 加密/Secure Boot 方案与选型 |
| [内存与新功能评估](09-memory-and-feature-assessment.md) | v3.1.3/v5.5.4 各场景内存实测、满负载分析、OTA/Captive Portal/FTMS 扩展可行性 |
| [OTA 与 Captive Portal 落地方案](10-ota-implementation-plan.md) | Captive Portal 实施总结 + OTA 详细方案（分区表/上传协议/回滚自检/防降级/实施清单） |
| [OTA 固件升级操作手册](11-ota-user-guide.md) | 面向测试/客户的 OTA 升级操作指南（配网/上传/升级步骤/常见问题排查） |
| [v3.1.6 迭代规划](12-roadmap-v316.md) | FTMS 扩展（坡度/阻力/范围特征）+ 内存优化（WiFi 残留释放/HTTP 缓冲复用）可行性 |

## 🤖 AI 助手

| 文档/规则 | 说明 |
|----------|------|
| [AGENTS.md](../AGENTS.md) | AI 助手入口，必读优先级、硬性规则、验证要求 |
| [project-context.md](../.trae/memory/project-context.md) | 项目记忆（用户偏好、架构决策、踩坑记录） |
| [general-rules.md](../.trae/rules/general-rules.md) | 项目结构、模块边界、提交流程 |
| [coding-style.md](../.trae/rules/coding-style.md) | C/ESP-IDF 代码风格 |
| [hardware-constraints.md](../.trae/rules/hardware-constraints.md) | ESP32-C3 硬件约束 |
| [wifi-ble-mutex.md](../.trae/rules/wifi-ble-mutex.md) | WiFi/BLE 射频共用规则 |

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
| [项目进度](../PROGRESS.md) | 阶段 0-10 完成情况、待确认项、已知限制 |
| [更新日志](../CHANGELOG.md) | 版本历史与变更记录 |
| [优化审查清单](08-review-checklist.md) | v3.1.0 多板型/发行流程/配网网页全链路复查记录（已完成） |
