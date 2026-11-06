# BikePower C 项目审查与优化

## 后续提交状态

用户在稳定性加固后授权测试通过再提交。本次重新执行严格门禁 117 PASS / 2 工作区 WARN / 0 FAIL、NVS sanitizer 3584 断言、Host/严格 sanitizer 6233/0、ESP-IDF 编译与 15 轮/180.9 秒 mock 耐久，均 exit 0，无依赖跳过。配置 `05a8627`、存储 `2c4b0fc`、测试工具 `ebc26a7` 已分批推送 main，文档批同步本报告；详见 [稳定性加固](38-persistence-stability-hardening.md)。

以下为首轮审查的历史事实，首轮未提交/未修复跨键存储描述不能代表当前状态；没有烧录、打 tag 或发布新固件，实物待验收边界不变。

## 结论与范围

- 本轮基线：`a37f372`（main），从 `ab9062a` 仅快进同步一个远端提交；更新前工作区干净。没有重写历史、提交、推送、烧录、打 tag 或发布。
- 审查覆盖：`main/` 主任务、配置 mailbox、NVS、RF/门户、BLE/遥测与上游桥接；纯 C 配置、跑步与桥接算法；现有生产函数 sanitizer、协议向量和 Pages 浏览器回归。
- 完成三类局部修复，保留现有配置 schema v14、BLE UUID、八组线格式与 UI 功能。不以无依据的重构或缩栈换取表面精简。
- 本轮是源码审查与离线优化，不是新固件实物验收。发现的持久化跨键原子性风险保留为高优先级后续项。

## 已修复问题

### R1：保存完整配置覆盖用户跑步步频（P1）

- 根因：[config_store_apply_to_engine](../../components/bikepower_domain/config_store.c) 先应用 `run_cadence`，随后调用跑步预设选择；预设 setter 又写入模板默认步频。因此用户保存的步频与实际引擎、再次落盘值不一致；固定模式也会受到影响，因为完整配置应用同样遍历预设。
- 修复：跑步预设、范围和模式处理后再应用用户步频；删除前面的重复 setter。
- 回归：[test_config_apply_to_engine](../../test/test_domain.c) 覆盖四种预设 × 两种模式，核对保存结果、模式和预设选择。原实现八个保持断言失败，修复后通过。

### R2：非法名称解析失败被吞掉及 Unicode 转义损坏（P1）

- 根因：三类配置名称忽略 `json_unescape()` 的失败结果；解析器容忍未知转义，且 `\uXXXX` 只取低字节，导致中文/代理对损坏或产生 NUL。超长或截断名称可能恢复默认却仍报告成功。
- 修复：三类名称失败立即返回 false；严格读取四位十六进制、校验代理对并编码 UTF-8；拒绝非法转义、原始控制字符、NUL、孤立代理和容量不足。输出仍只在全部解析成功后替换。
- 回归：[test_config_string_validation](../../test/test_domain.c) 对三类名称使用十种非法输入，精确分配输入长度验证边界；核对失败不污染输出、中文和合法代理对、功率计名称 Unicode。已有双引号/反斜杠往返继续通过。
- 边界：这仍是面向工具生成紧凑格式的轻量字段提取器，不是完整 JSON 语法验证器；本轮没有宣称支持任意缩进、重复键、严格数值 token 或嵌套键语义。

### R3：mock 继承终止信号状态导致测试收尾挂住（P2）

- 实际复现：第一轮三分钟业务测试通过 15 个循环，但 `proc.terminate()` 后 `wait(timeout=5)` 超时，导致报告未生成；完整门禁也在门户浏览器回归的收尾等待。
- 根因：执行环境继承的 SIGTERM 忽略状态/屏蔽掩码。仅注册 handler 的第一次修复仍失败，补充恢复主线程的 POSIX 信号掩码后通过。
- 修复：[mock_server.py](../../tests/mock_server.py) 显式注册 SIGTERM，在 POSIX 解除对应屏蔽；通过独立线程调用 `server.shutdown()`，避免在 serve_forever 主线程直接 shutdown 的死锁。
- 回归：[test_mock_server_shutdown.py](../../tests/test_mock_server_shutdown.py) 人工构造“忽略并屏蔽 SIGTERM → exec”的父进程；修复前等待超时，修复后正常 exit 0。已接入 [quality_gate.py](../../scripts/tools/quality_gate.py)。
- 清理：只停止已核实属于本轮的遗留测试子进程，没有操作用户原有的预览服务。

### 文档与产物治理（P2/P3）

- 同步技术参考的 4.3.0 固件/修复版本，修正 HTTP 保存并非直接 NVS 写入的旧描述；同步项目规则版本、测试说明、索引和本轮日志。
- 增加 `build-review/` 忽略规则，保留独立编译产物而不污染源码 diff；不删除历史构建与发布包。

## 核心功能核对

| 核心链路 | 本轮证据与结论 |
|---|---|
| 固定骑行、配速机器人、ERG 与心率 | 既有领域单测与固定基准/多种子/24h 等效仿真通过；未改骑行模型参数 |
| 跑步 Start/Stop/Reset、RSC/FTMS 与配置 | 领域与生产 BLE 协议回归通过，新增完整配置步频保持测试；真实 App/码表未验收 |
| 八组配置事务、revision 和错误响应 | 生产 GATT/门户 ASan/UBSan 与 Pages 异常回归；只证明回调/运行态契约，不证明 flash 跨键原子性 |
| 三设备桥接、零功率、过期与恢复 | 领域 wire、校正、主备、生产会话/扫描/运行时 sanitizer 回归；真实上下游无线接管未执行 |
| WiFi/BLE 生命周期 | 检查 RF 单一入口、禁止开机自启、done latch、主任务清理及既有静态/生产门户测试；真实热点共存未执行 |
| CPU、堆、RSSI 四组遥测 | 人工固定字节真值、生产 GATT、网页解码及设备状态双主题回归；当前镜像实际开销未测 |
| 网页刷写与工具台 | 锁文件依赖安装；36 项会话/教学测试、DOM、遥测解码、真实 Chromium+ESP Web Tools、双主题多宽度回归通过；未写硬件 |

## 验证记录

最终主机/构建/浏览器证据如下；完整质量门禁的最终计数在本节末汇总。

- 初始 `quality_gate.py --no-build`：112 PASS / 5 环境缺依赖 WARN / 0 FAIL；host 6115/0。
- 新增业务回归修复前：6132 PASS / 71 FAIL，证明原有测试遗漏真实配置缺陷。
- 修复后 host：增加精确长度输入后为 6233 PASS / 0 FAIL；严格 `-Wall -Wextra -Werror` 与 ASan/UBSan 通过。
- `make -C test run` 同时运行桥接领域、主备状态机和遥测测试；最终门禁重新编译普通 host 二进制。
- ESP-IDF v5.5.4 `idf.py -B build-review reconfigure` 与 `build` 通过；最后代码精简后再次 build 通过。当前根 sdkconfig 为 generic ESP32-C3，MAX_CCCDS=16、运行统计开启。app `0x1157d0`，约 1110 KB，factory 剩余 72%。没有生成可分发 merged bin。
- 全新固件构建仅出现上游 NimBLE `ble_hs_startup.c` 的 `key_rc` 未使用告警，本项目新增 C 代码无严格 host 告警。未修改第三方 SDK。
- Node `npm test`：36 PASS / 0 FAIL / 0 skipped，另有 DOM 和遥测专项通过。
- `npm run browser`：真实 Chromium/ESP Web Tools 不写设备验证通过；工具台两主题六宽度、设备状态两主题四宽度通过。
- 三分钟耐久最终复跑：15 轮，180.1 秒，exit 0，mock 正常退出、报告落盘。第一轮收尾失败保留日志，不计为完成。
- 文档事实与 63 份 Markdown 链接/锚点独立复验通过；当前 HEAD 的 whitespace 也独立检查通过。
- mock 终止回归：1 test / OK，约 0.64 秒。
- 最终 `quality_gate.py --no-build`：116 PASS / 2 WARN / 0 FAIL，exit 0；五项依赖跳过全部补跑通过。两个 WARN 仅为本轮 13 个未提交改动和因此跳过最近提交 whitespace；该提交的 whitespace 已另行验证。构建为上方独立执行，不计入此门禁。
- 依赖只安装到隔离的测试虚拟环境与项目已有 Node 测试目录，不改全局 Python/ESP-IDF 依赖。

## 未修复风险与建议

### F1：多键 NVS 保存没有整组事务原子性（上轮 P1）

> 后续本地稳定性加固已实现双槽联合快照与模拟故障回归，当前状态见 [稳定性加固](38-persistence-stability-hardening.md)。下方为上轮审查时的事实，保留历史证据。

- 项目路径：[nvs_store_save](../../main/nvs_store.c) 先后 `nvs_set_blob`、多个 `nvs_set_i32` 与 `nvs_set_str`，全部成功后才 `nvs_commit`；失败保持旧运行态，但并不等于 flash 撤销已成功写入的键。
- SDK 证据：已在本地 ESP-IDF v5.5.4 查看 `components/nvs_flash/src/nvs_api.cpp` 的 nvs_commit 及 `nvs_handle_simple.cpp`；typed setter 调用 storage.writeItem，commit 当前没有跨键缓存事务。
- 后果：中途写失败或掉电并重启后，可能出现新旧混合配置。HTTP/BLE 失败注入的 host 替身不覆盖真实 NVS 的这类语义。DIS 覆盖和主配置分 namespace 的两步提交也有掉电窗口。
- 推荐：后续独立设计显式编码的单配置 blob/双槽代次与校验提交方案；保留旧键读取迁移，并加每个写入阶段掉电/失败恢复测试。它涉及存储迁移与兼容性，不能用本轮简单调用顺序调整冒充彻底修复。

### F2：JSON 输入解析器的剩余限制（P2）

- 字段仍通过字符串模式定位，整数仍依赖 strtol 后转 int。未完成整份 JSON 的语法、键作用域、重复键和数值溢出验证；当前生产 HTTP 先经 cJSON，领域导入接口不能视为任意不可信 JSON 的通用解析器。
- 建议：先明确定义领域导入兼容范围，再统一受限解析/严格 token 读取；不要悄悄扩大输入承诺。

### F3：硬件与标准合规缺口（持续待验收）

- 本轮没有烧录、真机 BLE、跑步第三方 App、M5Stamp C3 实物、按钮/LED、手机 Safari、真实 WiFi/BLE 共存、实际掉电和 flash 写失败测试。
- FTMS 配对/Encryption 策略仍为项目已知未完成项；不宣称 Bluetooth 完整认证合规。
- generic 历史发布证据或当前网页部署状态不能覆盖这次未提交源码的固件行为。

### F4：维护性与质量门禁口径（P2/P3）

- `main/ble_bikepower.c` 1742 行、`app_main.c` 702 行，后续可按协议与 lifecycle 拆分，但本轮没有为缩短文件而重构。
- 根 Makefile 在上轮只以 C 源文件为依赖；后续稳定性加固已补所有领域头依赖，详见 [稳定性加固](38-persistence-stability-hardening.md)。
- quality gate 在上轮缺依赖降 WARN；后续增加 `--strict` 和专项超时，严格执行不允许依赖跳过。
- 固件内 Git 身份只包含 HEAD，未提交源码仍显示同一个 SHA；这次是 dirty review build，不能用 SHA 单独证明镜像与已提交源码一致。

## 分批与交付

- 业务修复批：`components/bikepower_domain/config_store.c`、`test/test_domain.c`。
- 测试可靠性批：`tests/mock_server.py`、新增退出回归、quality gate 接线、构建目录忽略。
- 文档批：本报告、技术参考、项目规则、测试说明、索引、PROGRESS 与 CHANGELOG。
- 没有 commit/push，本轮全部修改保留在用户本地仓库；补丁和完整测试日志在本次会话输出目录交付。
