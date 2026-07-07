# BikePower C 项目记忆

AI 助手在执行任务前必须先回顾此文档，严格按照历史要求执行。
代码风格和硬件约束详见 `.trae/rules/coding-style.md` 和 `.trae/rules/hardware-constraints.md`。

> 本文件是长期记忆唯一权威来源：用户偏好、项目约束、架构决策、踩坑经验。
> 踩坑标注 `[CRITICAL|WARNING|INFO] [YYYY-MM-DD]`。

---

## 一、项目背景

- BikePower 从 MicroPython（仓库 esp32-power）**完全重写**为 C + ESP-IDF + NimBLE。
- 用户授权"全程你来设计开发"，分阶段（0-6）推进，每阶段编译 → 烧录 → 真机验证后提交推送。
- 项目位置 `/home/mengen1014/work/bikepower-c`，ESP-IDF v5.5.4 本地工具链，当前固件 v4.0.1。
- MicroPython 专有约束（mpremote/gc/_thread/const/.mpy/部署 .py）**不适用**本项目。

---

## 二、构建与部署要求

1. **构建**：`idf.py build`（输出 build/*.bin）。
2. **烧录 + 监控**：`idf.py -p <port> flash monitor`。
3. **串口地址不写死**：自动扫描 ESP32-C3（USB VID/PID 智能识别）。
4. **跨平台**：macOS / Linux / Windows，注意 macOS 无 `timeout` 命令。
5. **部署后必须测试**：每阶段烧录后真机验证并记录结果。
6. **固件产物必须带文档和总包**：每次生成可分发固件时，`build/release/` 必须同时包含双板型 merged bin、`.sha256`、`manifest.txt`、`test-report.md`、`core-flow-endurance.md`、`user-manual.md`、`images/` 和 release 总包 zip。
7. **测试报告必须真实**：报告需列出编译、host、mock、10 分钟核心流程耐久、静态门禁、真机、人工项结果；未执行项必须写明原因，不得默认通过。
8. **用户手册随固件更新**：功能或使用流程变化时先更新 `docs/user/`，再由 `scripts/tools/generate_release_docs.py` 生成随附 `user-manual.md`。

---

## 三、Git 提交与发布要求

1. **commit message 格式**：Conventional Commits，标题简体中文概述。
2. **详细变更记录在 CHANGELOG.md**。
3. **版本号**：SemVer 三位，commit/tag/Release 三者一致。
4. **分批提交**：按功能分批，不混提。
5. **测试无误后自动提交并 push（用户已授权，无需逐次询问）**：相关测试（host 单测 / 编译 / 适用时真机）通过即 commit 并立即 `git push`，无需等待确认。
6. **提交前 diff 自检**：链路完整、无残留调试、文档已同步。
7. commit message 末尾附 `Co-authored-by: AI <ai@trae>` + `Model:`。

---

## 四、用户偏好

1. **语言**：中文沟通，代码不加行内注释（除非要求）。
2. **操作风格**：需求三阶段——分析影响范围/内存/复杂度/可行性 → 给方案对比 → 等确认 → 执行。严禁直接动手。
3. **测试优先**：每阶段烧录后必须真机验证。
4. **记录习惯**：所有约定记录在此文档。
5. **单位换算**：文件大小用 KB/MB。
6. **AC 质疑规则**：硬件约束冲突/跨模块影响/更优方案/安全风险时必须主动质疑，不谄媚。

---

## 五、凭证信息

- **Gitee Token**：放 `.env`（`GITEE_TOKEN`），脚本 `os.getenv` 读取。
- **Gitee 仓库**：`mengen4jv/bikepower-c`。
- 不在代码/日志/commit 中明文写凭证。

---

## 六、已验证的架构决策

| 决策 | 结论 | 日期 |
|------|------|------|
| 重写技术栈 | C + ESP-IDF v5.5.4 + NimBLE peripheral | 2026-06-13 |
| 领域层独立组件 | components/bikepower_domain 可 host 单测（test/test_domain，断言数随用例增长） | 2026-06-12 |
| BLE 五服务 | CPS+HRS+CSC+FTMS+DIS，FTMS 控制点 ERG（RequestControl/StartResume/SetTargetPower） | 2026-06-12 |
| 按钮中断驱动 | ISR 仅 `xSemaphoreGiveFromISR`，去抖/计时在 button_task | 2026-06-12 |
| 事件总线 | app_queue（Queue）+ button wake（二值信号量） | 2026-06-12 |
| 状态机 | 7 态：BOOTING/BLE_ACTIVE/WIFI_CONFIRM/WIFI_CONFIG/SAVING/REBOOTING/ERROR_RECOVERY | 2026-06-12 |
| WiFi 射频共用 | 配网默认 coex 共存（BLE 不停、不重启）；v4.0.0 已移除在线 OTA 与家庭网络配置，仅共存初始化失败时由 radio_ctrl 停 BLE 兜底 | 2026-06-28 |
| 跑步模式核心闭环 | 当前跑步 P0 以 RSC 速度 + 步频 + 心率为核心；坡度不是必要参数，已从当前配置/API/网页/NVS 中移除，未来 FTMS Treadmill 再按需求恢复 | 2026-07-07 |

---

## 七、按钮语义（与 MicroPython 行为一致）

| 操作 | 时长 | 动作 |
|------|------|------|
| 短按 | <500ms | 功率 -20W |
| 长按调功 | ≥500ms | 每 200ms +20W（仅 steady 模式） |
| 长按确认 | ≥2000ms 释放 | 回滚本次自动调功，进确认窗口（LED 快闪） |
| 二次确认 | 窗口内再按 | 触发 WiFi 配网 |
| 窗口超时 | 3000ms | 取消，恢复 BLE_ACTIVE |

---

## 八、技术踩坑记忆

### BLE / NimBLE
- [INFO] [2026-06-12] NimBLE 比 Bluedroid 省 ~50KB RAM；空闲堆基线约 200-220KB。
- [CRITICAL] [2026-06-12] BLE 通知/GATT 回调、ISR 中禁止重分配大缓冲或阻塞。

### WiFi / RF
- [CRITICAL] [2026-06-28] WiFi 与 BLE 共用射频；配网默认 coex 共存且 BLE 不停，所有 WiFi/SoftAP 启停必须经 radio_ctrl；v4.0.0 已移除在线 OTA 与家庭网络配置，仅共存失败兜底时由 radio_ctrl 停 BLE。
- [CRITICAL] [2026-06-12] 禁止开机自动启 WiFi，否则无限重启风险。
- [WARNING] [2026-06-26] 配网页面必须设置 `Cache-Control: no-store`，否则浏览器缓存导致用户从"更多配置"返回首页时显示旧数据，表单值丢失。
- [WARNING] [2026-06-26] 历史多页表单的每个页面隐藏表单必须包含全部可变字段，否则提交到 `/draft` 时会丢字段；v3.2.0 起门户为 SPA，当前保存入口统一收敛到 `/api/config`。

### 中断 / FreeRTOS
- [CRITICAL] [2026-06-12] ISR 内只用 `*FromISR` API，禁止日志/BLE/阻塞调用。
- [INFO] [2026-06-12] button_task 栈 2560、led_task 栈 2048 实测够用。

### 工具环境
- [WARNING] [2026-06-12] macOS 无 `timeout` 命令，串口抓取改用纯 Python 脚本。
- [CRITICAL] [2026-06-12] heredoc（`python - <<'PY'`）在工具执行环境被注入污染导致 SyntaxError，必须写成脚本文件再运行。
- [INFO] [2026-06-26] ESP-IDF 本地路径 `/home/mengen1014/work/esp32-power/esp-idf`（非 `~/esp/esp-idf`），source 其 `export.sh` 后才可用 `idf.py`。

### 跑步模式 / 配置 schema
- [INFO] [2026-07-07] 跑步模式可用性的最低闭环是速度/配速，不是坡度或 FTMS 控制；新增跑步能力先保持 RSC 路径简单，FTMS Treadmill Data / Control Point 只在目标 App 明确需要时再做。
- [WARNING] [2026-07-07] 配置字段增删必须同步 `domain_config.h`、`config_store.c/.h`、`power_engine.c/.h`、`nvs_store.c`、`wifi_portal.c`、`main/web/*`、`tests/mock_server.py`、`tests/test_portal_config_save.py`、`tests/check_config_consistency.py` 和 `tests/check_portal_structure.py`；漏任一层容易造成网页回显、NVS 或静态门禁漂移。
- [INFO] [2026-07-07] 删除配置字段时优先“停止导出/读写并忽略旧输入”，不必为已删除字段保留运行时结构；host 单测应覆盖旧 JSON 带该字段时 deserialize 成功且有效字段仍生效。
- [INFO] [2026-07-07] 对 UI/配置删减，先把静态门禁改成会因旧元素/字段残留而失败，确认 RED 后再删实现；本模式已用于移除 `run_incline_x10`。

### 编译
- [WARNING] [2026-06-12] main/CMakeLists.txt 的 REQUIRES 必须包含 `bikepower_domain`，否则领域层链接失败。
