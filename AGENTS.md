# BikePower C — Agent Instructions

本文件是 AI 编码助手的跨工具入口。保持简短，链接到权威资产而非重复内容。

> 项目性质：BikePower 从 MicroPython 完全重写为 **C + ESP-IDF + NimBLE**（ESP32-C3）。
> 当前固件版本 v4.0.6。所有 MicroPython 专有约束（mpremote/gc/_thread/const/.mpy）**不适用**于本项目。

## 必读优先级

- [PROGRESS.md](PROGRESS.md) — 阶段进度、待完成项、已知限制（权威进度来源）。
- [.trae/memory/project-context.md](.trae/memory/project-context.md) — 用户偏好、已验证决策、凭证处理、踩坑记忆。
- [.trae/rules/general-rules.md](.trae/rules/general-rules.md) — 项目结构、模块边界、提交/发版规则。
- [.trae/rules/coding-style.md](.trae/rules/coding-style.md) — C/ESP-IDF 代码风格与 FreeRTOS 约束。
- [.trae/rules/hardware-constraints.md](.trae/rules/hardware-constraints.md) — ESP32-C3 硬件约束。
- [.trae/rules/wifi-ble-mutex.md](.trae/rules/wifi-ble-mutex.md) — WiFi/BLE 射频共用（硬约束）。
- [docs/dev/01-technical-reference.md](docs/dev/01-technical-reference.md) — 技术参考（当前实现的权威行为规格）。
- [docs/archive/01-cidf-nimble-rewrite.md](docs/archive/01-cidf-nimble-rewrite.md) — 基线重写方案（历史归档，仅供追溯）。

## Hard Rules

- 与用户用简体中文沟通，除非明确要求其他语言。
- **WiFi/BLE 射频共用**：配网默认走 `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` 共存模式，BLE 不停；WiFi 启停必须经 `radio_ctrl`，仅共存初始化失败兜底时由 `radio_ctrl` 先停 BLE。
- **禁止开机自启 WiFi**：配网模式必须由用户长按 + 二次确认触发。
- **常量集中**：引脚、UUID、阈值、超时、namespace key 必须定义在 `main/bikepower_config.h` 或领域层 `domain_config.h`，禁止散落硬编码。
- **中断上下文安全**：ISR 内只允许 `xSemaphoreGiveFromISR` / `xQueueSendFromISR` 等 FromISR API，禁止调用 BLE/日志/阻塞 API。
- **日志用 ESP_LOG**：禁止 `printf` 调试残留，统一 `ESP_LOGI/W/E`。

## Workflow

- 编辑前运行 `git status --short`，明确本任务影响的确切文件。
- 提交前运行相关测试，把真实结果记录到 `CHANGELOG.md` 或提交说明。
- 按功能分批提交：一个 bug、一个功能、一个脚本、一个发版产物、一个文档/规则主题一批。
- 禁止把不相关的代码、产物、治理文档混进同一个 commit。
- 用户确认提交后立即 push。
- 不重写已推送历史、不 amend，除非用户明确要求。

## Validation

- C 代码改动：`idf.py build` 必须通过。
- 领域层（components/bikepower_domain）改动：运行 host 单测（`test/test_domain`）。
- 设备行为改动：编译 → 烧录 → 串口确认 → BLE 自测脚本（`tests/`）。
- 物理按钮/LED 灯效无法远程触发，需人工确认（见 PROGRESS.md 待确认清单）。

## Memory Policy

- 长期用户偏好与已验证方案写入 [.trae/memory/project-context.md](.trae/memory/project-context.md)。
- 可强制执行的规则写入 `.trae/rules/`。
- 自定义 Skill 放 `.trae/skills/*/SKILL.md`，不拆分不移动。
- 长流程与说明写入 `docs/`。
- 不存储一次性任务细节、猜测、或可从源码推断的内容。

## Commit Message 规范

- 标题：`<type>(<scope>): <简体中文概述>`（type/scope 用英文，如 `feat(button):`）。
- 正文：逐文件列出变更（`文件名：变更内容`）。
- 末尾附：
  ```
  Co-authored-by: AI <ai@trae>
  Model: {模型名}/{版本号}
  ```
