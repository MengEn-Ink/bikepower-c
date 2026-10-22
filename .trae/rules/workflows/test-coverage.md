# 测试覆盖策略（BikePower C）

<!-- stability: tier-2 (稳定，随测试资产调整) -->
<!-- rule-id-prefix: TC -->

> 本文件定义“改什么，测什么”。它不替代 `device-testing.md`，而是说明覆盖层次和最低要求。

## 覆盖分层

### `TC-1`：host 单测

- 目标：覆盖领域层纯 C 逻辑
- 入口：`test/build/test_domain`
- 源文件：`test/test_domain.c`
- 覆盖对象：
- `profile.c`
- `domain_rng.c`
- `hr_model.c`
- `power_engine.c`
- `running_engine.c`
- `config_store.c`
- `device_template.c`
- `device_analysis.c`
- `ble_payload_codec.c`（CPS/CSC/HRS/FTMS/RSC 生产 payload 编码）
- `json_miniparse.c`（配网请求体 JSON 字段提取）
- `button_logic.c`
- `button_debounce.c`
- `led_pattern.c`
- 当前断言规模：`5892` 条（2026-10-22 本轮基线，0 失败）
- 适用改动：
- `components/bikepower_domain/` 下的逻辑变更
- 配置 schema、范围校验、模式计算、心率模型变更
- JSON 解析、按钮语义、LED 映射等纯逻辑变更

### `TC-2`：BLE 协议与真机自测

- 目标：分层验证对外 BLE payload、互斥 GATT profile、控制点会话和断开重广播
- 离线入口：`tests/test_ble_protocol_vectors.py`、`tests/test_ble_reconnect_harness.py`、`tests/test_ble_running_harness.py`、`tests/test_device_profile_harness.py`、`tests/check_ftms_control_contract.py`
- 真机入口：`tests/ble_selftest.py`、`tests/device_profile_harness.py`、`tests/stage3_conn_test.py`
- 覆盖内容：
- 扫描并连接 `BikePower`
- cycling 发现 `CPS/HRS/CSC/FTMS/DIS`；running 发现 `RSC/HRS/FTMS/DIS`，不得要求互斥 profile 同时出现
- 读取 DIS 厂商、型号、固件版本
- 用独立 golden vectors 验证生产 C encoder 与无 bleak Python parser，包括 CPS 8B、FTMS 可变长度、RSC 0.1m 和 Treadmill 10B
- cycling 接收功率相关通知并观察 ERG 会话；running 使用 `--control` 覆盖 SetSpeed、Start/Stop/Reset
- `stage3_conn_test.py` 执行两次独立扫描 → 连接 → 显式断开，第二轮不得复用第一次扫描结果且必须命中同一设备 identifier
- `--run-portal` 时通过 profile harness 切换两种 GATT 表，测试阶段强制 `broadcast_mask=7`，并在 finally 恢复原 profile/mask 和完整 running 持久配置
- running `--control` 逐 opcode 等待 indication，按 moving → Stop → paused Reset → Resume 四阶段采集多样本，严格检查 RSC/Treadmill 累计增长与冻结
- 适用改动：
- `main/ble_bikepower.c`
- `components/bikepower_domain/ble_payload_codec.c/.h`
- `app_main.c` 中 BLE 相关接线
- `bikepower_config.h` 中 BLE UUID、设备名、版本号等常量

离线 vectors/fake harness 只证明编码和编排契约，不能替代 NimBLE GATT、射频链路或标准客户端真机验收。

### `TC-3`：配网自测

- 目标：验证 WiFi 门户与 HTTP 路由
- 入口：`tests/stage5_portal_test.py`
- 覆盖内容：
- 访问首页
- 读取 `/api/state`
- 读取 `/api/time`
- 校验 `/api/mem_stats`
- 校验 `/favicon.ico`
- 可选通过 `POST /api/renew` 校验续期接口
- 适用改动：
- `main/wifi_portal.c`
- `main/radio_ctrl.c`
- `main/app_main.c` / `main/app_state.c` 中 WiFi done 接纳与清理链路
- 按钮进入配网的状态机链路

### `TC-3a`：门户 JS 静态检查（**最高优先级，每次改动必跑**）

- 目标：在烧录前拦截 JS 语法错误（如 `}}}` 多括号、缺括号导致整个 `<script>` 块失效，按钮点击无反应）
- 入口：`tests/check_portal_js.py`
- 覆盖内容（v3.2.0 重构后）：
- 直接对 `main/web/app.js` 跑 `node --check`，不再从 C 字符串提取 `<script>` 块
- 校验文件非空、node 可用
- 适用改动：
- `main/web/app.js` 任何改动
- `main/web/index.html` 或 `main/web/app.css` 改动后建议同步跑
- 历史教训：v3.1.6 `/ota` 页 `pollStatus` 结尾 `}}}` 多一个 `}`，导致 `setInterval(function(){...},2000)` 的 `)` 失配，整个 script 块报 `missing ) after argument list`。v3.2.0 起 JS 独立成文件，此类问题由 `node --check` 直接拦截；v4.0.0 已永久放弃设备内 OTA，但 JS 静态门禁继续保留。

### `TC-3d`：门户结构静态检查

- 目标：在烧录前校验门户信息架构、HTML 占位符、前后端 API 字符串和后端路由表一致
- 入口：`tests/check_portal_structure.py`
- 覆盖内容：
- `main/web/index.html` 含 HTML 基础结构与 `<!-- INLINE_CSS -->` / `<!-- INLINE_JS -->`
- `main/wifi_portal.c` 注册前端调用的核心 `/api/xxx` 路由
- `main/web/app.js` 调用 `/api/state`、`/api/time`、`/api/renew`、`/api/config`、`/api/close-wifi`
- 底部导航保持首页/设置两 Tab，旧更多/关于视图并入设置页
- 适用改动：
- `main/wifi_portal.c` 路由表改动
- `main/web/index.html`
- `main/web/app.js`
- `main/web/app.css`

### `TC-3h`：配网页保存 mock 测试

- 目标：无需真机验证网页端保存配置后 `/api/state` 即时回显
- 入口：`tests/test_portal_config_save.py`
- 覆盖内容：
- 自动启动 `tests/mock_server.py`
- `POST /api/config` 保存功率、踏频、心率锁定、浮动幅度、按钮步进、用户资料、自定义配速档、LED 配置
- `GET /api/state` 验证保存字段与 custom pacer 展示同步
- 适用改动：
- `main/web/app.js`
- `main/wifi_portal.c`
- `tests/mock_server.py`
- 配置字段、用户资料、LED 配置、配速档字段变更

### `TC-3i`：核心流程耐久测试

- 目标：无需真机连续验证核心业务流程，默认运行 3 分钟
- 入口：`tests/test_core_flow_endurance.py`
- 覆盖内容：
- 自动启动 `tests/mock_server.py`
- 循环模拟按钮短按减功率、长按连续加功率
- 改变功率数值并观测心率随动趋势
- 网页端首页参数、用户资料、设备名称、按钮步进、自定义配速、LED 灯效和静默模式保存
- 设备模板扫描、读取、保存、应用、删除
- 配网续期、倒计时、保存并关闭 WiFi
- 输出 `tests/reports/core_flow_endurance_*.md`；固件生成时输出 `build/release/core-flow-endurance.md`
- 适用改动：
- 固件生成前强制执行
- `main/web/`、`tests/mock_server.py`、按钮/心率/配置字段相关改动

## 选择规则

- `TC-4`：纯治理文档或规则改动可不跑测试，但要做一致性复核。
- `TC-5`：领域层改动至少跑 `test/build/test_domain`。
- `TC-6`：BLE payload 改动必须跑 `tests/test_ble_protocol_vectors.py`；BLE 行为改动还必须按实际 profile 跑 `tests/ble_selftest.py`。running 会话使用 `--control`，断开重广播使用 `stage3_conn_test.py` 两轮独立扫描；无设备时必须明确真机项 `NOT_RUN`。
- `TC-7`：配网改动至少跑 `tests/stage5_portal_test.py`，并复核 BLE/WiFi 射频互斥链路。
- `TC-7a`：**任何 `main/wifi_portal.c` 改动必须先跑 `tests/check_portal_js.py`，JS 语法全绿才能继续**。这是硬性前置门，避免 `}}}` 类低级 bug 烧录后才发现。
- `TC-7c`：**常量一致性检查（每次改动必跑）**：跑 `tests/check_config_consistency.py`，验证 `bikepower_config.h` / `domain_config.h` / `README.md` / `docs/` 之间的常量值（版本号、引脚、阈值、超时、栈大小）一致，防止文档与代码漂移。
- `TC-7c-1`：修改 FTMS 控制点必须跑 `tests/check_ftms_control_contract.py`；修改 WiFi 完成通知必须跑 `tests/check_wifi_done_contract.py`。静态契约通过后仍须按影响面补真机验证。
- `TC-7d`：**配网页保存字段改动必须跑 `tests/test_portal_config_save.py`**，确保 mock 保存、状态回显和 custom pacer 展示不漂移。
- `TC-7e`：**固件生成前必须跑 `tests/test_core_flow_endurance.py`**，默认 3 分钟；如果因时间原因仅快速验证，必须显式写明 `--cycles` 结果不能替代正式固件生成门禁。
- `TC-7f`：**配置字段增删必须覆盖端到端同步面**：
  - 领域层：`domain_config.h`、`config_store.c/.h`、相关 engine 结构和 setter。
  - NVS/API：`main/bikepower_config.h`、`main/nvs_store.c`、`main/wifi_portal.c`。
  - 前端/mock：`main/web/index.html`、`main/web/app.js`、`tests/mock_server.py`。
  - 回归：`test/test_domain.c`、`tests/test_portal_config_save.py`、`tests/check_config_consistency.py`、`tests/check_portal_structure.py`。
  - 删除字段时，必须补一条“旧输入可忽略”的兼容测试，避免旧 JSON/NVS 残留导致保存失败；同时补静态门禁防止旧 UI/字段回归。
- `TC-8`：跨层改动按最高等级执行，不能只挑便宜测试。

## 影响范围评估（**每次代码修改前必做**）

- `TC-8a`：修改任何文件前，必须列出：
  1. 直接修改的文件
  2. 间接影响的模块（include 该文件、调用该函数、引用该常量的模块）
  3. 受影响的测试层（TC-1/TC-2/TC-3/TC-3a/TC-3c/TC-3d）
  4. 需要同步更新的文档（对照 best-practices.md「代码→文档映射表」）
- `TC-8b`：修改 `main/wifi_portal.c` 时必须评估：
  - 是否改 HTTP 路由 → 跑 TC-3 + TC-3d（`check_portal_structure.py`）
  - 是否改 BLE 通知 → 跑 TC-2
- `TC-8b-2`：修改 `main/web/` 下任何文件时必须评估：
  - `app.js` 改动 → 跑 TC-3a（`check_portal_js.py`）
  - `index.html` 改动 → 跑 TC-3d（占位符与视图元素）
  - 路由契约改动 → 同步 `tests/mock_server.py` 与 `tests/check_portal_structure.py` 的 `REQUIRED_BACKEND_ROUTES`/`REQUIRED_FRONTEND_APIS`
- `TC-8c`：修改 `main/bikepower_config.h` 常量时必须 grep 全仓引用，列出所有受影响模块。

## 全绿标准（**功能完成必须测试全绿才能提交**）

- `TC-9`：新功能落地时必须明确归属到以上至少一层测试。
- `TC-9a`：**"功能完成"的定义 = 所有相关测试层全绿**，不是"代码写完了"。任一测试未跑或未通过，不得视为完成，不得提交，不得烧录分发。
- `TC-9b`：测试失败时必须定位根因并修复，禁止跳过、注释、或用 `--ignore` 绕过。
- `TC-9c`：测试因硬件条件无法执行时，必须在提交说明中明确写出"未执行"+原因，并标记为 pending，不得默认通过。
- `TC-9d`：每次生成可分发固件，必须同步生成 `build/release/test-report.md`、`build/release/core-flow-endurance.md`、`build/release/user-manual.md` 和 release 总包 zip；测试报告必须列出已执行、未执行和原因。
- `TC-10`：如果规则、文档或提交说明引用断言数、脚本名、服务数、路由数，必须与当前仓库保持一致。

## 覆盖率目标与现状（目标 90%）

> 详细矩阵见 [docs/dev/03-test-coverage-analysis.md](../../../docs/dev/03-test-coverage-analysis.md)。

### 自动化测试覆盖率口径

- **可自动化覆盖率** = （host 单测覆盖行 + 静态脚本覆盖行）/（领域层行 + main/ 纯逻辑行 + main/ 静态可校验行）
- 不计入分母：纯硬件交互代码（NimBLE GATT 传输、ISR、GPIO、WiFi 连接、HTTP 响应），这些走真机测试（DT 系列）；BLE payload 编解码已由生产 codec + golden vectors 覆盖，不再归入纯真机边界。
- 当前重点：领域层生产纯逻辑保持 100% host 可测；`main/` 的硬件/网络路径通过静态门禁 + 真机脚本兜底。
- 目标：不为覆盖率抽取生产代码不用的并行实现；只抽取 `main/` 会真正调用的纯逻辑。

### Loop 模式测试建立流程（每次改动必走）

1. **识别纯逻辑**：改动涉及的函数中，哪些是不依赖 ESP-IDF / 硬件的纯计算（版本比较、JSON 解析、范围校验、状态转换表）。
2. **抽取到领域层**：把纯逻辑移到 `components/bikepower_domain/`，main/ 改为 include 调用。
3. **补 host 单测**：在 `test/test_domain.c` 加测试函数，覆盖正常 / 边界 / 异常路径。
4. **跑 TC-1**：`make -C test && ./test/build/test_domain`，断言全绿。
5. **跑 TC-3a/3c/3d**：涉及 wifi_portal / 前端 / 常量时跑对应静态脚本。
6. **跑 TC-3h**：涉及网页保存字段时跑 mock 保存回显测试。
7. **跑离线协议/harness**：涉及 BLE payload、控制点、重连或 profile 编排时跑对应 vectors/静态契约/fake harness。
8. **跑 TC-2/3 真机**：涉及 BLE / 配网行为时按 profile 跑真机脚本；重连必须完成两轮独立扫描。
9. **生成固件随附文档**：每次生成 release 固件时同步产出测试报告和用户手册。
10. **更新本文件断言数 + docs/dev/03-test-coverage-analysis.md 矩阵**。
11. **提交前全绿**：TC-9a 硬性门；受设备条件限制的项按 TC-9c 保持 pending。
