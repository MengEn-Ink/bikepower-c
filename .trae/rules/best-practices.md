# 项目优化审查规则（C / ESP-IDF 版）

<!-- stability: tier-2 (稳定，随审查标准变更) -->
<!-- rule-id-prefix: OR (引用格式: OR-章节名，如 OR-文件命名规范) -->
> 每次版本发布前，执行以下优化审查。确保核心功能不变的前提下持续改进。
> 命名 / 风格细则见 [coding-style.md](coding-style.md)，硬件约束见 [hardware-constraints.md](hardware-constraints.md)，本文件只列审查项。

---

## 一、项目结构审查

### 文件命名规范

| 规则 | 说明 |
|------|------|
| 模块文件 `snake_case.c/.h` | `ble_bikepower.c`, `power_engine.c`, `wifi_portal.c` |
| 入口文件保持简短 | `app_main.c`（任务创建 + 事件循环 + WDT） |
| 常量集中 | 板级常量放 `main/bikepower_config.h`，领域层常量放 `components/bikepower_domain/include/domain_config.h` |
| 头文件只暴露公开接口 | `#pragma once` + 公开声明，内部静态不进头文件 |

### 冗余文件检测

| 检查方法 | 通过标准 |
|---------|---------|
| 是否存在与 config 头文件重复的配置定义 | 无重复 |
| 是否存在过时的测试报告 / 临时产物 | 无 |
| 是否存在未被任何文件包含的 .c/.h | 无 |
| README.md / general-rules.md 文件结构是否与实际目录一致 | 一致 |
| README.md 参数值是否与 config 头文件一致 | 一致 |

### 文件归属原则

| 类型 | 位置 | 说明 |
|------|------|------|
| 板级代码（依赖 ESP-IDF） | `main/` | 入口、状态机、外设、BLE、WiFi |
| 领域层（纯 C，可 host 单测） | `components/bikepower_domain/` | 功率/心率/踏频/配置算法 |
| AI 规则 | `.trae/rules/` | 约束层 + 治理规则 |
| host 单测 | `test/test_domain` | 领域层单元测试 |
| 设备测试脚本 | `tests/` | bleak 自测 + 串口抓取 |
| 人类文档 | `docs/` | 重写方案、部署指南 |
| 进度 / 变更 | PROGRESS.md / CHANGELOG.md | 阶段进度 + 变更记录 |

---

## 二、文档内容审查

### 重复内容检测

| 检查方法 | 通过标准 |
|---------|---------|
| docs/ 与 .trae/rules/ 是否大量重复 | 规则放 rules/，方案/教程放 docs/，不重复 |
| README.md 与 PROGRESS.md / CHANGELOG.md 是否重复 | README 放概览，PROGRESS 放进度，CHANGELOG 放变更 |
| `.trae/memory/project-context.md` 与 general-rules.md 是否重复 | MEMORY 放用户偏好+凭证，rules 放技术规则 |
| 各规则之间是否重复（如多处定义命名规范） | 单一来源，其余用链接引用 |

### 过时内容检测

| 检查方法 | 通过标准 |
|---------|---------|
| README.md 配置持久化描述是否与 NVS 实现一致 | 一致 |
| README.md / 文档按钮时长是否与 config 头文件一致 | `SHORT_PRESS_MS` / `WIFI_BTN_HOLD_MS` / `CONFIRM_WINDOW_MS` 一致 |
| 文档可调参数是否与 config 头文件一致 | 无已删除常量 |
| docs/ 中版本号、参数值是否与当前一致 | 一致（`FIRMWARE_VERSION`） |

---

## 三、内存与中断优化审查

### 堆内存

| 检查方法 | 通过标准 |
|---------|---------|
| BLE 通知 / GATT 回调中是否分配大缓冲 | 必须预分配，回调内不重分配 |
| WiFi / HTTP 处理是否创建不必要的大对象 | 分块发送，避免整页驻留峰值 |
| 大缓冲是否在循环 / 回调中重复 malloc | 否，预分配复用 |
| NimBLE host 启动后空闲堆 | > 80KB（低于告警） |

### 栈与任务

| 检查方法 | 通过标准 |
|---------|---------|
| 每个 `xTaskCreate` 栈大小是否经实测确认 | 是，不随意缩小 |
| 主循环及长任务是否覆盖 Task WDT 喂狗 | 是，`esp_task_wdt_reset()` |
| 任务间通信是否走 Queue / 信号量 | 是（app_queue + button wake 信号量） |

### 中断安全

| 检查方法 | 通过标准 |
|---------|---------|
| ISR 内是否只用 `*FromISR` API | 是 |
| ISR 内是否调用阻塞 / 日志 / BLE API | 否 |
| 去抖 / 计时 / 自动重复是否在任务上下文 | 是（button_task） |

---

## 四、健壮性审查

### BLE 稳定性（最高优先级）

| 检查方法 | 通过标准 |
|---------|---------|
| BLE 通知异常是否被捕获（不影响主循环） | 是 |
| BLE / NimBLE 初始化失败是否走可控恢复 | 是，记录 + 必要时 `esp_restart()` |
| BLE 通知缓冲是否预分配 | 是 |
| BLE 连接数是否限制 | 是，从 config 读取 |
| BLE 断连后是否自动重新广播 | 是 |

### 主循环健壮性

| 检查方法 | 通过标准 |
|---------|---------|
| 数据更新异常是否兜底 | 是 |
| 按钮事件是否阻塞 BLE 通知 | 否，按钮处理走 Queue 不阻塞 |
| WiFi 触发失败是否影响主循环 | 否，仅记录错误日志 |
| 看门狗是否正常喂狗 | 是，主循环周期 `esp_task_wdt_reset()` |

### WiFi 健壮性

| 检查方法 | 通过标准 |
|---------|---------|
| WiFi 启停是否走 radio_ctrl 统一入口 | 是（radio_ctrl_enter_wifi / radio_ctrl_exit_wifi） |
| 共存模式下 BLE 是否保持在线 | 是，配网期间 BLE 继续通知 |
| socket / HTTP 连接是否在出错路径释放 | 是 |
| WiFi 超时/保存后是否不重启回到 BLE | 是，done 回调 + APP_EVT_WIFI_DONE 关 HTTP + SoftAP |
| 共存启动失败是否降级为独占方案 | 是，停 BLE + 重试 + 必要时 esp_restart |
| 配网任务异常是否被捕获 | 是，不传播 |

### 异常兜底原则

1. **BLE 通知异常**：捕获后仅记录日志，不中断主循环。
2. **BLE 初始化异常**：记录 + 必要时 `esp_restart()` 重启设备。
3. **WiFi 启动异常**：捕获后仅记录日志。
4. **配置保存异常**：捕获后仅记录日志，运行时值已更新。
5. **任务异常**：每个任务入口做兜底，不传播导致整机崩溃。

---

## 五、文档同步审查

> 每次代码修改后，必须逐项检查以下文档是否需要同步更新。

### 代码→文档映射表

| 修改了什么 | 必须检查的文档 | 同步内容 |
|-----------|--------------|---------|
| `bikepower_config.h` 常量 | README.md, hardware-constraints.md | 参数值、范围、默认值 |
| `domain_config.h` 常量 | README.md, 领域层校验代码 | 范围、默认值、算法参数 |
| 按钮行为（button.c / app_state.c） | README.md, hardware-constraints.md | 时长阈值、操作动作 |
| BLE 通知格式（ble_bikepower.c） | docs/, 骑行 App 兼容性说明 | 数据格式、通知间隔 |
| WiFi 路由（wifi_portal.c） | docs/, wifi-ble-mutex.md | 路由表、超时时间 |
| 配置持久化（nvs_store.c / config_store.c） | README.md, domain_config.h | NVS key、范围校验、`CONFIG_VERSION` |
| 新增/删除文件 | README.md, general-rules.md | 文件结构树、模块依赖图 |
| 新增规则 | general-rules.md | 文件结构树、强制规则 |
| 任何代码变更 | CHANGELOG.md, PROGRESS.md | 变更记录、阶段进度 |
| 公开接口（.h）变更 | 对应头文件接口注释 | 参数/返回/行为描述 |

### 同步检查流程

```
代码修改完成
  → 对照映射表，列出受影响的文档
  → 逐个检查文档内容是否与代码一致
  → 更新不一致的文档
  → 提交时确认文档已同步
```

### 常见遗漏场景

| 场景 | 容易遗漏的文档 |
|------|--------------|
| 删除常量 | README.md 可调参数列表 |
| 修改默认值 | docs/ 中的默认值描述 |
| 修改函数签名 | 头文件接口注释 |
| 新增文件 | README.md 和 general-rules.md 的文件结构树 / 依赖图 |
| 修改按钮时长 | README.md 按钮操作表、hardware-constraints.md |

---

## 六、代码冗余与结构审查

> 保持核心功能不变的前提下，消除重复代码、验证架构合理性。

### 冗余检测

| 检查方法 | 通过标准 |
|---------|---------|
| 是否存在 80% 以上逻辑重复的函数 | 合并为统一函数，通过参数区分行为 |
| 是否存在重复的 clamp / 范围校验逻辑 | 领域层防御性校验可保留（不信任调用方），业务层重复需合并 |
| 是否存在多处硬编码相同字符串 / 数值 | 提取到 config 头文件或模块常量 |
| 是否存在可复用的错误处理 / 重试逻辑 | 抽取为独立函数 |

### 结构合理性

| 检查方法 | 通过标准 |
|---------|---------|
| 模块职责边界是否清晰（每个模块只做一件事） | 参照 general-rules.md 模块职责边界 |
| 是否无循环依赖 | include 链为有向无环图（DAG） |
| 是否无过度抽象（嵌入式适合扁平结构） | 不引入不必要的中间层 |
| 是否无跨模块隐式耦合 | 通过公开接口交互，不访问其他模块静态变量 |
| 领域层是否保持纯 C（无 ESP-IDF 依赖） | 是，可 host 单测 |

### 冗余原则

1. **DRY 有度**：代码重复少于 ~10 行且语义不同时可保留，避免过度抽象。
2. **合并优先**：HTTP 请求、NVS 读写、JSON 解析等通用逻辑合并为统一入口。
3. **防御性重复可接受**：领域层二次 clamp 输入值（不信任调用方）属正确设计。
4. **结构扁平是美德**：嵌入式不引入 DI 容器、重量级路由表等模式。
5. **领域层隔离**：components/bikepower_domain 不得 include ESP-IDF 头文件，保持 host 可测。

### 架构一致性检查

> 对照 general-rules.md「模块依赖图 / 运行时数据流」章节，确认当前代码与架构图一致。

| 检查项 | 验证方法 | 通过标准 |
|--------|---------|---------|
| 模块依赖图 | 对比 include 与依赖图 | 链为 DAG，无循环依赖 |
| 启动流程 | app_main 任务创建 → 事件循环 | 初始化顺序正确，WDT 覆盖长任务 |
| 模块互斥约束 | WiFi 启停走 radio_ctrl 统一入口 | radio_ctrl_enter_wifi / radio_ctrl_exit_wifi |
| RF 开关单一入口 | 全局搜索 `esp_wifi_*` / NimBLE 启停 | 仅 radio_ctrl 操作 |
| 公有接口一致性 | 对照头文件公开声明 | 无遗漏、无多余导出 |
| 领域层纯净性 | 检查 components 是否 include ESP-IDF | 无 |

### 架构流程参考

> 完整架构图详见 [general-rules.md](general-rules.md)。本节仅列审查时重点关注的要点。

**启动链路**：`app_main.c`（任务创建 + 事件循环 + 连接轮询 + WDT）

**运行时数据流**：
```
按钮中断 → button_task → app_queue → app_state_handle_event
                                          ├→ 调功/确认窗口/配网触发
                                          └→ led_set_pattern
power_engine.snapshot → ble_bikepower_set_data → BLE 通知
连接轮询(app_main) → APP_EVT_CONN_CHANGED → app_state
```

**配网链路（共存模式）**：
```
app_state ST_WIFI_CONFIG → radio_ctrl_enter_wifi（BLE 不停）
    → radio_ctrl_start_wifi_ap（coex 仲裁）→ wifi_portal_start（HTTP 配网，配置实时生效到 BLE 通知）
    → 保存/超时 → wifi_portal done 回调 → APP_EVT_WIFI_DONE
    → app_state → radio_ctrl_exit_wifi（关 HTTP + SoftAP）→ ST_BLE_ACTIVE
```

**关键交互点**（模块间仅通过这些接口通信）：
- `app_state` → `ble_bikepower`: `ble_bikepower_set_data()` 通知
- `app_state` → `led`: `led_set_pattern()`
- `app_state` → `radio_ctrl`: `wifi_trigger` 触发配网 / `wifi_exit` 退出配网
- `radio_ctrl` → `wifi_portal`: 启动 HTTP 配网服务
- `wifi_portal` → `app_main`: done 回调投递 `APP_EVT_WIFI_DONE`
- `app_state` / `power_engine` → `nvs_store`: 配置保存 / 恢复
- 上层 → `components/bikepower_domain`: power_engine / hr_model / profile 数据接口

---

## 七、审查执行流程

### 执行前分批计划

开始项目整体优化前，必须先输出分批计划，不能边改边把所有内容混成一个提交。

| 批次 | 适用内容 | 提交要求 |
|------|----------|----------|
| 结构整理批 | 文件移动、目录归档、删除冗余文件 | 单独提交，不混入业务逻辑 |
| Bug 修复批 | 明确缺陷和对应代码修复 | 单独提交，必须有相关测试 |
| 测试工具批 | 测试脚本、host 单测 | 单独提交，必须跑测试 |
| 文档规则批 | README、docs、MEMORY、rules、AGENTS.md | 单独提交，只记录文档验证结果 |
| 发布产物批 | 固件 bin、版本号、tag | 单独提交，必须校验构建产物；固件必须 `merge_bin` 合并为单文件，禁止分发分散 bin |

分批防呆：

1. 修改前先运行 `git status --short`，确认基线干净或明确已有改动来源。
2. 每个批次完成后运行 `git diff --name-status`，确认只包含本批范围。
3. 每个批次使用指定路径 `git add <files>`，禁止在多主题工作区中直接 `git add -A`。
4. 每个批次提交后立即 push；下一批开始前重新检查工作区。
5. 如果同一轮任务必须跨多个主题，先在回复中列出批次，不允许最终才合并解释。

### 审查步骤

```
1. 项目结构审查 → 删除冗余文件、修复命名
2. 文档内容审查 → 修复过时描述、消除重复
3. 内存与中断审查 → 堆/栈/ISR 安全、预分配、WDT 喂狗
4. 健壮性审查 → 异常兜底、BLE 稳定性、WiFi 安全
5. 架构一致性审查 → 对照 general-rules.md，校验依赖图/互斥约束/领域层纯净性
6. 文档同步审查 → 对照映射表，检查代码→文档一致性
7. 代码冗余审查 → 消除重复、验证结构，保持核心功能不变
8. 分批提交审查 → 确认 diff 未混入其他主题
9. 编译检查 → idf.py build 通过 + host 单测通过
10. 真机测试 → 编译 → 烧录 → 串口确认 → BLE 自测脚本
11. 更新 CHANGELOG.md / PROGRESS.md
```

### 输出报告格式

项目整体优化结束时，必须输出以下报告：

| 项目 | 内容 |
|------|------|
| 优化范围 | 本轮检查了哪些目录、模块、文档 |
| 问题清单 | 按严重度列出发现的问题 |
| 修复清单 | 每个问题对应的修复文件和验证方法 |
| 分批提交 | 每个 commit 的主题、哈希、是否已 push |
| 测试报告 | 命令、结果、跳过项和原因 |
| 残余风险 | 未修复问题、硬件限制、需人工验证事项 |
