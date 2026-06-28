# 测试覆盖策略（BikePower C）

<!-- stability: tier-2 (稳定，随测试资产调整) -->
<!-- rule-id-prefix: TC -->

> 本文件定义“改什么，测什么”。它不替代 `device-testing.md`，而是说明覆盖层次和最低要求。

## 覆盖分层

### `TC-1`：host 单测

- 目标：覆盖领域层纯 C 逻辑
- 入口：`test/test_domain`
- 源文件：`test/test_domain.c`
- 覆盖对象：
- `profile.c`
- `domain_rng.c`
- `hr_model.c`
- `power_engine.c`
- `config_store.c`
- `ota_version.c`（OTA 语义版本比较，防降级核心）
- `json_miniparse.c`（latest.json 解析，OTA 版本检查核心）
- 当前断言规模：`946` 条
- 适用改动：
- `components/bikepower_domain/` 下的逻辑变更
- 配置 schema、范围校验、模式计算、心率模型变更
- OTA 版本比较 / JSON 解析逻辑变更

### `TC-2`：BLE 自测

- 目标：验证对外 BLE 协议面
- 入口：`tests/ble_selftest.py`
- 覆盖内容：
- 扫描并连接 `BikePower`
- 发现五服务 `CPS/HRS/CSC/FTMS/DIS`
- 读取 DIS 厂商、型号、固件版本
- 接收功率相关通知
- 发送 FTMS 控制点并观察 ERG 行为
- 适用改动：
- `main/ble_bikepower.c`
- `app_main.c` 中 BLE 相关接线
- `bikepower_config.h` 中 BLE UUID、设备名、版本号等常量

### `TC-3`：配网自测

- 目标：验证 WiFi 门户与 HTTP 路由
- 入口：`tests/stage5_portal_test.py`
- 覆盖内容：
- 访问首页
- 读取倒计时
- 校验配网页面关键内容
- 校验续期接口
- 校验配置提交后的行为
- 适用改动：
- `main/wifi_portal.c`
- `main/radio_ctrl.c`
- 按钮进入配网的状态机链路

### `TC-3a`：门户 JS 静态检查（**最高优先级，每次改动必跑**）

- 目标：在烧录前拦截 JS 语法错误（如 `}}}` 多括号、缺括号导致整个 `<script>` 块失效，按钮点击无反应）
- 入口：`tests/check_portal_js.py`
- 覆盖内容：
- 从 `main/wifi_portal.c` 提取所有 C 字符串字面量拼接的 HTML
- 定位所有 `<script>...</script>` 块
- 用 `node --check` 验证每个块的 JS 语法
- 适用改动：
- `main/wifi_portal.c` 任何含 `<script>` 的页面（/ota、/more、/、/about 等）
- 任何修改 C 字符串内嵌 JS 的改动
- 历史教训：v3.1.6 `/ota` 页 `pollStatus` 结尾 `}}}` 多一个 `}`，导致 `setInterval(function(){...},2000)` 的 `)` 失配，整个 script 块报 `missing ) after argument list`，"检查更新"按钮点击完全无反应

### `TC-3b`：OTA 全流程自测

- 目标：验证 OTA 检查/升级链路的 HTTP 路由、状态机、JS 语法
- 入口：`tests/ota_flow_test.py`
- 覆盖内容：
- **静态部分（无需设备）**：
  - 提取 `/ota` 页面 JS，验证 `checkUpdate`/`pollStatus`/`startUpgrade` 函数定义存在且语法正确
  - 验证 `x.open('POST','/ota/check')`、`x.open('GET','/ota/status')`、`x.open('POST','/ota/upgrade')` 调用存在
  - 验证按钮 `onclick` 绑定与函数名一致
  - 验证 C 代码中 `/ota/check`、`/ota/status`、`/ota/upgrade` 三个路由 handler 已注册
- **动态部分（需设备进入配网态）**：
  - `GET /ota` 返回 200 且含 `check-btn`、`upgrade-btn`
  - `POST /ota/check` 未配 WiFi 时返回 `{"status":"error",...}`
  - `GET /ota/status` 返回 JSON 且含 `status` 字段
  - 配好家庭 WiFi 后 `POST /ota/check` 返回 `{"status":"started"}` 或 `{"status":"running"}`
  - 轮询 `/ota/status` 直到 `done`/`error`，验证字段完整
- 适用改动：
- `main/wifi_portal.c` 中 `h_ota`/`h_ota_check`/`h_ota_status`/`h_ota_upgrade` 及 `/ota` 页面 JS
- `main/ota_update.c`、`main/ota_downloader.c` 版本检查/下载逻辑
- `main/radio_ctrl.c` 的 `radio_ctrl_sta_connect`/`radio_ctrl_enter_sta_exclusive`

## 选择规则

- `TC-4`：纯治理文档或规则改动可不跑测试，但要做一致性复核。
- `TC-5`：领域层改动至少跑 `test/test_domain`。
- `TC-6`：BLE 行为改动至少跑 `tests/ble_selftest.py`，涉及设备行为时再走完整真机流程。
- `TC-7`：配网改动至少跑 `tests/stage5_portal_test.py`，并复核 BLE/WiFi 射频互斥链路。
- `TC-7a`：**任何 `main/wifi_portal.c` 改动必须先跑 `tests/check_portal_js.py`，JS 语法全绿才能继续**。这是硬性前置门，避免 `}}}` 类低级 bug 烧录后才发现。
- `TC-7b`：OTA 相关改动必须跑 `tests/ota_flow_test.py --static`（静态部分），动态部分按真机条件执行。
- `TC-7c`：**常量一致性检查（每次改动必跑）**：跑 `tests/check_config_consistency.py`，验证 `bikepower_config.h` / `domain_config.h` / `README.md` / `docs/` 之间的常量值（版本号、引脚、阈值、超时、栈大小）一致，防止文档与代码漂移。
- `TC-8`：跨层改动按最高等级执行，不能只挑便宜测试。

## 影响范围评估（**每次代码修改前必做**）

- `TC-8a`：修改任何文件前，必须列出：
  1. 直接修改的文件
  2. 间接影响的模块（include 该文件、调用该函数、引用该常量的模块）
  3. 受影响的测试层（TC-1/TC-2/TC-3/TC-3a/TC-3b）
  4. 需要同步更新的文档（对照 best-practices.md「代码→文档映射表」）
- `TC-8b`：修改 `main/wifi_portal.c` 时必须评估：
  - 是否含 `<script>` → 跑 TC-3a
  - 是否改 HTTP 路由 → 跑 TC-3
  - 是否改 OTA 页/handler → 跑 TC-3b
  - 是否改 BLE 通知 → 跑 TC-2
- `TC-8c`：修改 `main/bikepower_config.h` 常量时必须 grep 全仓引用，列出所有受影响模块。

## 全绿标准（**功能完成必须测试全绿才能提交**）

- `TC-9`：新功能落地时必须明确归属到以上至少一层测试。
- `TC-9a`：**"功能完成"的定义 = 所有相关测试层全绿**，不是"代码写完了"。任一测试未跑或未通过，不得视为完成，不得提交，不得烧录分发。
- `TC-9b`：测试失败时必须定位根因并修复，禁止跳过、注释、或用 `--ignore` 绕过。
- `TC-9c`：测试因硬件条件无法执行时，必须在提交说明中明确写出"未执行"+原因，并标记为 pending，不得默认通过。
- `TC-10`：如果规则、文档或提交说明引用断言数、脚本名、服务数、路由数，必须与当前仓库保持一致。

## 覆盖率目标与现状（目标 90%）

> 详细矩阵见 [docs/test-coverage-analysis.md](../../../docs/test-coverage-analysis.md)。

### 自动化测试覆盖率口径

- **可自动化覆盖率** = （host 单测覆盖行 + 静态脚本覆盖行）/（领域层行 + main/ 纯逻辑行 + main/ 静态可校验行）
- 不计入分母：纯硬件交互代码（BLE 通知回调、ISR、GPIO、WiFi 连接、HTTP 响应），这些走真机测试（DT 系列）。
- 当前现状：约 `40%`（领域层 100% + OTA 纯逻辑 100% + wifi_portal 静态部分）
- 目标：`90%`，缺口通过持续抽取纯逻辑到领域层 + 扩充静态脚本填补。

### Loop 模式测试建立流程（每次改动必走）

1. **识别纯逻辑**：改动涉及的函数中，哪些是不依赖 ESP-IDF / 硬件的纯计算（版本比较、JSON 解析、范围校验、状态转换表）。
2. **抽取到领域层**：把纯逻辑移到 `components/bikepower_domain/`，main/ 改为 include 调用。
3. **补 host 单测**：在 `test/test_domain.c` 加测试函数，覆盖正常 / 边界 / 异常路径。
4. **跑 TC-1**：`make -C test && ./test/test_domain`，断言全绿。
5. **跑 TC-3a/3b/3c**：涉及 wifi_portal / OTA / 常量时跑对应静态脚本。
6. **跑 TC-2/3**：涉及 BLE / 配网行为时跑真机脚本。
7. **更新本文件断言数 + docs/test-coverage-analysis.md 矩阵**。
8. **提交前全绿**：TC-9a 硬性门。
