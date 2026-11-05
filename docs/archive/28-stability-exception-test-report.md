# 当前功能稳定性与异常验证报告

> 归档于 2026-11-05：标题中的“当前”指报告验证时的基线；原 CI、镜像与未覆盖项保持历史口径。当前状态见 [PROGRESS](../../PROGRESS.md)。

日期：2026-11-05。验证基线：`5c97e96`，测试时工作区包含本轮改动；修复与门禁已分别以 `d3c9b8d`、`4c3e6ac` 提交并推送至 `main`。目标是补齐现有功能异常路径，不增加传感器功能、不打 tag、不发布固件。

## 发现与修复

| 问题 | 基线复现 | 修复 |
|---|---|---|
| Pages BEGIN 写失败后，响应 Promise 在超时产生未处理异常 | 新浏览器用例 `begin-write` 捕获 `等待设备保存结果超时` pageerror | 等待器可取消，传输异常立即清理；提前注册拒绝处理；断连释放监听器 |
| Pages BEGIN 丢响应后未取消已建立的事务 | `begin-timeout` 回归缺少 CANCEL，断言失败 | 已尝试 BEGIN 且结果未知时尽力 CANCEL；明确拒绝不发送 CANCEL |
| Pages COMMIT 发出后丢响应/断连，仅提示普通失败 | 实际事务回归要求不假更新 current/revision，并提示核实 | 未取得有效提交结果时提示重新连接读取配置；组失败/CANCEL 失败后可显式重试 |
| HTTP 小数 revision 被截断，绕过整数约束 | 生产 handler 接受 `revision=1.5`，新 C 回归失败 | 范围校验后要求整数，拒绝小数 |
| 回调报 SUCCESS 但缺少 applied，runtime 仍报成功 | C 回归要求 SAVE_FAILED，实际结果为 SUCCESS | 无 applied 的成功结果规范化为 SAVE_FAILED，配置和 revision 不变 |
| 门户成功保存但 HTTP 响应丢失，仅显示 Failed to fetch | 请求先转发成功再丢响应，浏览器断言失败 | 提示结果待核实，回读配置保留草稿与提示，不自动重复提交；明确拒绝不标未知 |
| HTTP 200 携带 ok=false 时，普通忙拒收误标保存结果未知 | 浏览器注入业务拒收，出现多余“待核实”提示，断言失败 | 以已解析业务结果区分明确拒收和传输/解析失败 |
| device gate 传了板型却未传给直接 BLE 自测 | 根 sdkconfig 为 M5，通用板实际 DIS 与错误预期不符；离线命令契约失败，显式环境板型后真机通过 | 直接自测传递 BIKEPOWER_EXPECT_BOARD，新增双板/双 profile 编排契约并接入门禁 |

修改范围：`main/wifi_portal.c/.h`、`main/web/app.js`、`tools/web/ble-monitor.html` 和相应测试/门禁/文档。配置字段、UUID、NVS schema 与通知格式不变。

## 新增覆盖

| 层 | 场景与检查 | 结果 |
|---|---|---|
| 门户实际压缩 DOM | 320/390/1440：首次 503/非法 JSON、轮询断网/恢复、外部 revision 与草稿保留、保存拒绝/重试、成功响应丢失、重复点击、异事务等待超时、扫描失败/取消、关闭失败/重试；零 pageerror | PASS |
| Pages 实际 DOM/编码/响应匹配 | success、wrong-response、notify-reject、begin-reject、begin-write、begin-timeout、group-write、cancel-write、commit-reject、commit-write、commit-timeout、malformed、disconnect；current/revision 不假更新、草稿保留、监听器释放、失败后显式恢复重试、零 pageerror | PASS |
| 生产 HTTP handler | 实际 receive→submit→回调→runtime；请求缓冲分配失败、空/非对象/尾随垃圾/非法类型/超长/截断/分段/超时、非法和过期 revision、pending 拒收、错事务、保存失败/重试/重复结果、关闭失败/成功、缺失 applied | PASS，ASan/UBSan |

浏览器使用传输替身或本地 mock；HTTP 回调/NVS 结果可控注入。测试执行生产实现，但不是完整实物无线链路的证据。

## 门禁与构建

- 新浏览器门禁接入 quick/full 操作门禁，生产 cJSON 故障回归仅 full；完整质量门禁包含三项。
- 操作矩阵增加 `OP-STAB-003/004/005`，检查器要求相应 ID 与命令；覆盖规则、测试 README 与覆盖分析已同步。
- 最终完整专项质量门禁：110 PASS、2 WARN、0 FAIL，构建独立执行；WARN 为 dirty 工作区及其历史 whitespace 检查跳过。
- full 操作门禁全部通过，包含 quick、host 6146/0、协议/运行时与 3 分钟 17 轮核心流程耐久。追加的 BEGIN 超时/订阅失败、请求内存不足和最终 30 项操作矩阵已单独复验通过。
- 既有 Pages 桥接浅/暗主题回归、36 项页面/刷写生命周期、ESP Web Tools 实际组件浏览器与浅/暗主题六屏宽布局回归通过。
- 双板独立正式配置构建通过，`PORTAL_TEST_AUTO_CONFIG=OFF`；app `0x10e400` bytes，factory 剩余 73%；内联网页 72386 bytes。
- 复用旧构建目录时发现 Git 标识缓存，已显式 `idf.py ... reconfigure build` 重建双板；最终日志均为 `BIKEPOWER_GIT_SHA=5c97e96`，构建时间 2026-11-05 00:48:57 CST。初轮与最终设备证据分目录保存，避免标识混用。
- 基线远端 CI `41867471119` 已成功；修复与门禁已提交推送，本轮最终提交的远端 CI 另行核实，不能沿用基线成功结论。
- README 与 general-rules 的目录说明已复核，新增文件位于现有测试/文档目录，不改变模块依赖；详细测试资产在 tests/README 与本轮矩阵维护。

命令与日志：

```bash
python3 tests/portal_exception_browser.py
python3 tests/test_pages_exceptions.py
python3 tests/test_portal_faults.py
python3 tests/operation_gate.py --level full --report build/stability-operation-full.md
python3 scripts/tools/quality_gate.py --no-build
npm test --prefix tests/web-flash
npm run browser --prefix tests/web-flash
```

本机证据：`build/stability-quality.log`、`build/stability-quality-final.log`、`build/stability-operation-full.log/.md`、
`build/stability-generic-build-final.log`、`build/stability-m5stamp-build-final.log`。

## 新镜像设备验证

通用 C3 串口为 `/dev/cu.usbmodem2101`。验证前检查无串口守护进程占用，完整配置备份、新镜像身份、设备门禁、最终恢复记录写入独立 `build/stability-evidence-final/`；初轮缓存标识镜像结果保留在 `build/stability-evidence/`，不作为最终镜像证据。

初轮完整设备门禁：串口、跑步四阶段、六组配置/NVS、双次重连和 303.5 秒耐久通过；
259 条 CPS，扫描/连接/空通知失败均 0，原配置已恢复。骑行因测试板型推断错误失败，
完整诊断分别复现错误预期与显式板型通过，保留初轮 FAIL，不改写为 PASS。

最终镜像重新烧录与完整设备门禁全部通过：

| 项目 | 最终结果 |
|---|---|
| 串口启动 | ROM/复位启动标志完整，无 panic、Backtrace、WDT/coredump 致命日志 |
| cycling | 服务/DIS/Feature、通知、ERG 收敛与 Stop/Pause 通过 |
| running | 四阶段 moving/Stop/paused Reset/Resume 的实际多样本、累计增长/冻结和控制点通过 |
| 六组配置/NVS | 事务错误路径、写入/重启持久化、广播/GATT 重建、完整恢复通过 |
| 双次重连 | 两轮独立扫描、连接和显式断开通过；WARN 日志级别下未观察到连接事件串口文本，不冒充 LED/状态机实物验收 |
| 设备耐久 | 308.2 秒、258 条 CPS；扫描失败/连接失败/通知空轮次均 0 |
| 配置恢复 | config-before.json 与 config-restored.json 六组逐字节一致，恢复后重启持久化回读通过 |

独立证据标识为 `5c97e96-dirty`，实际 app SHA256：

- generic-c3：`542dc1ed18f21ea0a7888fed3740b443b2e4bec11d57d39e3c0dedfb418dcfd0`
- m5stamp-c3（仅构建）：`a20eeabcdba158d5d6bb44201a910ce544c4e6ab11bd57fbfcad8db56d2ba2a1`

build/stability-evidence-final/image-identity.json 还保存 bootloader/分区表及本轮修改的固件源码指纹，已与实际文件校验一致。不引用旧 `cf1b462-dirty` 镜像 PASS，也不能作为 clean HEAD 发布通过证据。

## 验收边界

真实热点 HTTP 保存/关闭、真实功率计与 WiFi/下游三方共存、M5 实物、手机 Safari、物理按钮/LED、标准 App 与原生 Web Bluetooth/Web Serial 授权保持待验收。真实 NVS/flash 写失败与掉电未注入；ASan/UBSan 与回调故障测试不能替代这些实物结果。
本机检查未关联 AirPort 网络，设备未进入配网态；本轮 portal_http、broadcast_mask、portal_exit 等对应真实热点项保持 NOT_RUN。配网态堆峰值/最低堆水位未测量，不据此宣称满足共存时 80 KiB 门槛。

没有运行覆盖率工具，不宣称达到某个百分比或所有异常组合已覆盖。完整相关自动项通过后，本轮稳定性补强可供用户审阅；固件发布仍需用户手动验证并明确批准。

## 文档治理与提交

- `d3c9b8d`：保存结果与事务资源修复，包含对应生产 handler 和实际网页回归。
- `4c3e6ac`：设备板型传递修复与异常操作/质量门禁。
- 文档独立提交：归档 [已执行计划](../archive/16-stability-exception-test-plan.md)，同步覆盖矩阵、规则、测试入口与当前报告；历史 CI 失败保留，并补齐基线后续成功结果。
- 文档治理门禁 `python3 scripts/tools/quality_gate.py --level docs`：78 PASS、2 dirty WARN、0 FAIL；源码事实、相对链接/锚点、配置、工作流契约与 30 项操作矩阵通过。

源码提交不改变设备证据 `5c97e96-dirty` 与实际镜像 SHA256，也不替代从正式发布提交重新构建及验收。
