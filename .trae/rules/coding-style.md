# C / ESP-IDF 代码风格规则

<!-- stability: tier-1 (极稳定，仅随语言/框架变更) -->
<!-- rule-id-prefix: CS -->

## 文字规范
- 所有注释、文档、commit message 统一使用简体中文，禁止繁体中文。

## 运行环境
| 约束 | 说明 |
|------|------|
| 框架 | ESP-IDF v5.5.4（本地工具链，已从 v5.5.1 升级，参考 v5.5.4 release notes） |
| BLE 栈 | NimBLE host（peripheral） |
| RTOS | FreeRTOS（多任务 + Queue + 信号量） |
| 语言 | C11 |

## 命名规范
- 文件：`snake_case.c` / `snake_case.h`（`button.c`, `led.c`, `app_state.c`, `ble_bikepower.c`）。
- 宏/常量：`UPPER_SNAKE_CASE`（`BTN_PIN`, `SHORT_PRESS_MS`, `POWER_STEP_W`）。
- 类型：`snake_case_t`（`app_state_t`, `led_pattern_t`, `app_evt_t`）。
- 函数：`<模块>_<动作>`（`button_init`, `led_set_pattern`, `app_state_handle_event`）。
- 内部静态：`s_` 前缀（`static led_pattern_t s_pattern;`）。
- 静态函数：模块私有，不暴露到头文件。

## 模块结构
每个 `.c` 文件：
1. 顶部 include 顺序：标准库 → ESP-IDF → FreeRTOS → 本项目（`bikepower_config.h` 等）。
2. `static const char *TAG = "MODULE";`（ESP_LOG 标签，大写简称）。
3. 静态变量 → 静态函数 → 公开函数。

头文件用 include guard 或 `#pragma once`，只暴露公开接口。

## 常量集中
- 引脚、UUID、阈值、超时、namespace key、栈大小集中到 `main/bikepower_config.h`。
- 领域层常量集中到 `components/bikepower_domain/.../domain_config.h`。
- 禁止业务代码硬编码这些值。

## 日志
- 统一 `ESP_LOGI/W/E/D`，禁止 `printf` 调试残留。
- TAG 用模块大写简称。

## 内存与中断安全
| 项 | 要求 |
|----|------|
| ISR | 只用 `*FromISR` API，禁止阻塞/日志/BLE 调用 |
| 大缓冲 | 预分配，避免在通知/回调/循环中重分配 |
| 任务栈 | 经实测确认，不随意缩小 |
| Task WDT | 主循环及长任务喂狗 |

## 禁止事项
- ❌ 禁止绕过 radio_ctrl 直接调用 esp_wifi_*（必须走 radio_ctrl_enter_wifi / radio_ctrl_exit_wifi）。
- ❌ 禁止开机自动启动 WiFi。
- ❌ 禁止 ISR 内调用阻塞/日志/BLE API。
- ❌ 禁止 `printf` 调试残留（用 ESP_LOG）。
- ❌ 禁止硬编码引脚/UUID/阈值（提取到 config 头文件）。
- ❌ 禁止行内注释（除非用户要求）。
- ❌ 禁止繁体中文。
- ❌ 禁止 commit message 标题用英文概述或繁体中文；正文必须逐文件列出变更。
- ❌ 禁止只 commit 不 push。
- ❌ 禁止修改无关模块（每次只解决一个明确问题）。

## 开发流程
- 设计先行：编码前理解需求、给 2-3 个方案并推荐。
- 改 bug 前先稳定复现，定位根因再改，避免"试试看"多处同时改。
- 分阶段开发：每阶段编译 → 烧录 → 真机验证 → 分批提交推送。
