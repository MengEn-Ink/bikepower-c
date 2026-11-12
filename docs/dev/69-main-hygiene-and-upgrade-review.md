# BikePower C：main 卫生、开源对比与工程升级评审

审查日期：2026-11-12。实现基线：`b4e4a09be02b510ec61cbcf34a9037bfdcdbed5c`，固件版本 `4.4.1`，ESP-IDF `5.5.4`，配置 schema 14。

## 一、结论先行

> 后续用户已授权自动推进。下文保留首次评审基线与18项原排序，不自动改写历史成绩；当前JSON/adapter/测试前置已实施、门户部分改善、SDK/coredump隔离证据与剩余项见[自动工程改进](70-automated-engineering-followups.md)。人工/实物验收延后，未升级生产SDK或发布固件。

**优先做“可信验收与可维护性”，而不是继续堆服务、上云或重写框架。** 当前工程已具备纯 C 领域层、主任务配置事务、双槽持久化、逐连接订阅/响应隔离、固定版虚拟开发板和浏览器异常回归。它不是缺少基础架构；下一阶段的主要收益来自补齐实物证据、收敛大型模块与协议来源、改善资源余量，以及让升级和发行身份可验证。

本轮执行清理与低风险卫生修复；新功能、SDK/模拟器升级及大规模架构拆分仅评审，不实施。不改 BLE UUID、通知频率、广播策略、配置 schema、NVS、eFuse 或实体设备；不生成新固件 Release。

“软件测试通过”“实际固件虚拟运行”“目标客户端无线验收”“正式 Bluetooth 认证”是不同证据层级，不互相替代。当前硬件待办仍以 [PROGRESS 顶部](../../PROGRESS.md) 为准。

## 二、main 与清理结果

### 2.1 主线核对

- 本机仓库定位后，工作区没有已跟踪或未跟踪的在途源码；仅忽略目录包含配置、证据、工具依赖与构建缓存。
- 原本地 main 为 `757a687`，落后实时 `origin/main` 25 个提交、没有本地独立提交。仅执行 `fetch --prune` 与快进合并，更新到 `b4e4a09`，未重写历史。
- 本地初始已只有 main，且只有一个工作树；实时远端还存在 `dependabot/github_actions/github-actions-6d1c06d137`，head 为 `7fff54f`。
- 用户明确选择删除本地及远端全部非 main 分支。先保存全引用 Git bundle 并通过 `git bundle verify`，再用旧 head 条件保护删除该远端分支。再次查询远端 heads 与本地分支，均只剩 main；tags 保留。
- 被删除分支的独立变更是两处 `actions/cache@v5 → @v6`。**删除分支不等于合并升级**；该建议已纳入待办，备份保留其内容。

### 2.2 本地文件处置

按已确认的“构建缓存和明确临时文件”范围处理：删除 `.cache/`、`build/devcache/` 中无持有进程的旧构建元信息和 `docs/dev/.DS_Store`。完成验证后清理由本轮产生的 host 二进制与 Python 字节码；不使用无差别 `git clean -fdx`。

保留 `.env`（不读取/输出凭证）、`tests/reports/`、设计素材、截图及编辑器状态；保留可复用 Node 测试依赖，避免下一次回归立即失去运行环境。历史实物证据不是普通缓存，即使被 Git 忽略也不能直接删除。独立构建的 ELF/app 用于本轮证据身份，不当作正式可分发固件。

恢复分支可从交付目录的 `backups/before-branch-cleanup.bundle` 中取回对应引用；`refs-before.txt` 保存删除前原始引用。不把备份分支重新留在日常分支列表。

## 三、本轮卫生修复与文档矫正

### 3.1 内联生成器误覆盖源文件：已修复

[inline_static.py](../../scripts/inline_static.py) 过去允许将 `--output` 指向 `main/web/index.html`，成功返回却把输入模板覆盖成 C 头文件。这类误用已造成过构建失败，不能只靠操作提醒。

新增写入前检查：输出必须为 `.h`；不得与 HTML/CSS/JS 输入相同，也不得通过符号链接或硬链接别名覆盖源文件。合法 CMake 输出保持一致，不改变压缩算法或生产页面字节。

[维护工具测试](../../tests/test_maintenance_tools.py) 覆盖三种源文件、符号链接、硬链接、错误扩展名及合法生成；先运行旧实现负例确认失败，再修复并复跑。每个负例重新恢复夹具，避免前一个误覆盖污染后续用例。

### 3.2 Git 重命名误分类：已修复

[workspace_audit.py](../../scripts/tools/workspace_audit.py) 按 NUL 切分 Git porcelain，却没有消费重命名/复制记录的第二个路径，会把旧文件名误解析成额外状态条目。

现在消费 R/C 记录的源路径，仅保留目标条目。新增真实临时 Git 仓库回归，验证含空格的重命名与未知文件不会混淆；审计仍只读，不增加删除能力。测试已接入完整 quality gate。

### 3.3 文档事实矫正：已完成

- [安全说明](02-firmware-security.md)：区分 Flash 存储保密与 Secure Boot 真实性；纠正 C3 为 XTS-AES-128、256 位总密钥；加密 coredump 需要改变取证通道，不是必然失效。编译裁剪不是安全保证，也不应把 MIT 开源项目写成靠隐藏代码防抄袭。（[来源1](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32c3/security/flash-encryption.html)；[来源2](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32c3/api-guides/core_dump.html)）
- [架构 Wiki](../wiki/01-architecture.md)：移除已不存在的模板实现节点，修正为 config snapshot；factory 是 `0x3E0000`（3.875 MiB）的应用分区，不是 3.25 MB，也不等于 merged-bin 的全部内容。
- [虚拟开发板说明](48-local-virtual-board.md)：删除“当前没有 macOS 机器”这种失效环境描述；把旧 v4.3.3 发行阻断明确限定为历史阶段，不冒充当前发行状态。
- [PROGRESS](../../PROGRESS.md) 与 [CHANGELOG](../../CHANGELOG.md)：补齐 v4.4.1 已正式发布的身份边界。正式 Release 发布于 2026-11-11；后续 Pages 更新不是新的固件发行，也不能把 main 自动等同于 Release tag 的镜像。（[来源1](https://github.com/MengEn-Ink/bikepower-c/releases/tag/v4.4.1)）
- [文档索引](../INDEX.md)：补充遗漏的本地开发工作流及开发效率入口；旧治理报告添加历史用途与本报告指向。已有重复 48/53 编号暂不重命名，以免无必要破坏历史链接；后续新文档只用递增的唯一编号。

## 四、实际验证与证据边界

### 4.1 已执行

- 初始严格软件门禁：128 PASS / 0 WARN / 9 FAIL，领域层 6,250 断言通过。8 项失败源于 Python 依赖缺失，1 项源于 macOS OpenSSL 头文件/链接路径未提供；没有把这些结果误归为生产固件缺陷。
- 按现有 requirements 在隔离 Python 环境安装依赖，补 Python Playwright 自己的 Chromium；仅为本次命令提供 OpenSSL include/library 路径，不改全局工具链。失败首轮原日志保留。
- 卫生修复后的完整严格软件门禁：136 PASS / 2 工作区 WARN / 0 FAIL；领域层 6,250 断言通过。两项 WARN 是未提交工作区及因此跳过最近提交 whitespace，不是测试跳过。ESP-IDF 构建独立执行，不计入 `--no-build` 成绩。
- Pages：73 项 Node 单元测试通过、0 skipped；DOM 与遥测附加入口通过；17 个浏览器入口全部通过，包括双主题多宽度、草稿恢复、握手故障、刷写生命周期、手机版布局和离线演示。浏览器 mock 不证明真实设备连接或烧录。
- 两项维护工具回归：工作区 7 个测试、维护工具 5 个测试通过。
- ESP-IDF 5.5.4、generic-c3、独立 sdkconfig、`PORTAL_TEST_AUTO_CONFIG=OFF` 构建通过。app 为 1,184,096 字节，factory 剩余约 71%；内联门户 111,911 字节。ELF/app 哈希及当时源码差异身份保存在证据包 `build-identity.json`。
- macOS Apple Silicon 补测使用同一 `b4e4a09` 候选镜像：4 KiB 虚拟刷写/verify/精确回读、启动身份、八组配置、骑行通知、事务拒绝、保存重连/NVS reset、checked LTK、绑定复连、配置恢复、跑步与最后重启回读等通过；Service Changed 指示未取得，明确 OPEN 1，不算完整标准/系统缓存验收。
- 首次虚拟测试完成刷写后退出超时；诊断发现执行进程继承 SIGTERM 忽略与屏蔽。仅在本次测试进程恢复信号、补模块搜索路径后复跑；没有修改用户系统信号设置、固件或测试断言。原始 FAIL 与复跑记录全部保留；测试入口的信号前置条件应纳入后续 doctor/runner。
- 版本一致性、源码事实摘要及手机离线手册来源一致性已核对；最终文档/提交验证以交付证据包为准。

### 4.2 不可扩大宣称

本次不刷实体板，不执行设备配置变更或断电实验。真实踩踏、五分钟实体耐久、双下游+真实上游+WiFi峰值、Service Changed 无线确认、M5按钮/LED、iOS/Android热点及系统 GATT 缓存仍需对应镜像证据。固定版本机虚拟板的本轮执行结果在交付补充记录中单列；不能从代码检查或 Windows 历史 PASS 推断本次 macOS PASS。

## 五、发现但未实施的大项

### 5.1 崩溃取证栈余量：P1，配置风险而非已复现故障

当前 defaults 与本轮 sdkconfig 均为 `CONFIG_ESP_COREDUMP_STACK_SIZE=1024`；官方 v5.5.4 指南推荐独立 coredump 栈大于 1300 字节。建议在隔离镜像比较 1536/2048，并真实触发 panic、校验 dump 完整性与符号化；不能仅改数字就宣称可靠。（[来源1](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32c3/api-guides/core_dump.html)）

### 5.2 门户预算几乎耗尽：P1

111,911 / 112,640 字节（110 KiB）预算，仅剩 729 字节，约 0.65%。这是**内联资源治理预算**，不是 Flash 分区不足或实测堆余量。先审查冗余静态内容、生成过程与可安全压缩空间，不扩大预算掩盖问题；目标建议至少恢复 10% 编辑余量，所有移动/异常/草稿回归必须保持。

### 5.3 两个大型边界模块：P1/P2

审查基线 `main/ble_bikepower.c` 为 2,512 行，`tools/web/ble-monitor.html` 为 7,149 行。大文件本身不等于缺陷，但 BLE 安全存储、广播、服务表、会话/indication 与通知生命周期挤在一起，增加修改时误碰边界的概率。

推荐先抽 **NimBLE 适配边界**，再按会话/服务拆分；Pages 先抽连接会话/配置读写/渲染控制器，保持现有静态站点与测试入口，不引入大型前端框架重写。

### 5.4 SDK 私有结构耦合：P1

`main/CMakeLists.txt` 引入 NimBLE host `src` 私有目录；`ble_bikepower.c` 读取 `ble_hs_conn` 的 `bhc_gatt_svr.indicate_val_handle`，用来避免与系统 Service Changed 的在途指示冲突。它是已有可靠性机制，**不能为了“去私有 API”直接删掉**。

先集中到单一 adapter、明确 IDF 版本边界与锁顺序，补系统/应用 indication 竞争负例，再评估公开 API 或上游改进。任何 SDK 升级都必须重新验证这一机制。

### 5.5 轻量 JSON 导入仍非严格解析：P1/P2

`json_get_int()` 仍按字符串找键，`strtol` 不验证完整 token/溢出；config deserialize 最后迁移与夹紧。生产 HTTP 使用 cJSON 前置，不能因此证明纯 C 导入对任意 JSON 严格有效。

建议定义导入契约，针对错误数值 token、重复键、嵌套作用域、空白、整数溢出、超长字符串与输出原子性做负例，再选受限 token parser。保留纯 C、无堆分配与旧 schema 迁移；本轮不改变其兼容性。

## 六、优秀开源对比：学哪些，不搬哪些

### 6.1 SmartSpin2k：学传感器时序与控制验收，不搬电机系统

SmartSpin2k 是真实阻力执行器方案，有上下游 BLE、FTMS、功率/心率输入与电机调阻；它的 native 与 Python 回归强调生产协议字节、样本新鲜度、固定 workout 回放、失败不跳过及模拟结果和硬件结果分开。（[来源1](https://github.com/doudar/SmartSpin2k)；[来源2](https://github.com/doudar/SmartSpin2k/blob/develop/AGENTS.md)）

适用 BikePower：把值、采样时间、来源、session generation 作为同一快照；增加延迟/重复样本、停止踏频、模式切换、长时回绕与恢复时序回归。真实 ERG 电机调参、homing、扭矩/齿隙模型不适合直接搬给模拟器/功率桥。

软件为 GPL-2.0，BikePower 为 MIT；借鉴方法并独立实现，不能直接复制其实现到 MIT 固件。（[来源1](https://github.com/doudar/SmartSpin2k)）

### 6.2 qdomyos-zwift：学兼容 profile 与功能分层，不做万能固件

QZ 公开提供设备/用户 profiles、心率输入、ZWO/XML workout、GPX、统计及 DirCon；虚拟 bike 实现把 CPS、CSC、FTMS、HRS-only、特定客户端兼容分支及 notifier/write processor 分开。（[来源1](https://github.com/cagnulein/qdomyos-zwift)；[来源2](https://github.com/cagnulein/qdomyos-zwift/blob/master/src/virtualdevices/virtualbike.cpp)）

适用 BikePower：兼容预设必须来源于真实目标端日志与 Feature/特征集契约，而非靠反复改 MAC；把训练计划与导出放在 Pages/桌面层，避免把 UI、网络和大文件解析压进 C3。

QZ 为 GPL-3.0，不直接搬代码。DirCon 依赖长期网络，与本项目“默认 BLE、临时用户触发 WiFi、不恢复 OTA”的现有边界冲突，宜外部网关方案、低优先级单独立项。（[来源1](https://github.com/cagnulein/qdomyos-zwift)）

### 6.3 Espressif ESP Emulator + Bumble：强化真实协议路径，不自欺式 mock

ESP Emulator 当前文档提供真实 C3 镜像、NimBLE HCI、虚拟 UART、GDB 和任务 trace；Bumble 支持 Python ATT/GATT/SMP 与虚拟/真实 HCI，但其项目仍明确提示实验性与生产使用限制。（[来源1](https://github.com/espressif/esp-emulator)；[来源2](https://github.com/google/bumble)）

本项目已固定 emulator 0.51.0、Bumble 0.0.219，已有 checked LTK 与拒绝未知命令的测试策略。优化方向是扩展故障注入、复用生产路径、固定种子与输入镜像哈希，**不是重新创建另一套业务 mock**。main 文档的新能力不自动适用于固定旧二进制。

### 6.4 Zephyr Twister：借鉴能力声明和多层测试编排，不迁移 RTOS

Twister 用测试 scenario、平台/能力/fixture 与 harness 声明进行矩阵编排，区分仅构建与实际执行，支持 emulation、coverage 及多 DUT 场景。（[来源1](https://docs.zephyrproject.org/latest/develop/test/twister.html)）

适用 BikePower：把 quality、operation、release、npm 和设备工具里重复的测试登记收敛为一个可审计 registry；声明依赖、是否修改配置、设备数量、证据等级与恢复责任，生成各入口。先消除名单漂移，不必迁移到 Zephyr、west 或改写 FreeRTOS。

### 6.5 ESP Web Tools：维持锁定快照与生命周期，谨慎升级

ESP Web Tools 提供基于 manifest 的浏览器刷写及按芯片/串口类型选择镜像能力；本项目已固定同源 10.4.0 快照与独立生命周期回归。（[来源1](https://github.com/esphome/esp-web-tools)）

增量优化重点：配置保持式升级和完整恢复式刷写明确分开、错误终态资源释放、原始 checksum/来源；未完成设备验收前不自动追最新上游，不因“100%”或弹窗关闭标成功。

### 6.6 CBMC 与来源证明：针对关键小边界建立保证

CBMC 能对 C/C++ 数组边界、指针及用户断言做有界验证；适合选择小 codec、双槽提交或 failover 状态机，而不是对整个 BLE/RTOS 一次性形式化。（[来源1](https://github.com/diffblue/cbmc)）

GitHub artifact attestations 可绑定产物、构建 workflow、提交身份与 SBOM，并可验证；它证明来源，不保证代码安全或设备兼容。适合给正式 bin/ZIP 增补来源链，而不是给每份文档签名。（[来源1](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations/using-artifact-attestations-to-establish-provenance-for-builds)）

## 七、升级选择

### 推荐先评估 ESP-IDF 同系列补丁升级

官方已发布 v5.5.6，属于 v5.5 bugfix，并列出 WiFi supplicant 等修复。不能把 Bluedroid/其他芯片修复直接当作 C3 NimBLE 项目收益；先筛选实际使用组件。（[来源1](https://github.com/espressif/esp-idf/releases/tag/v5.5.6)）

路线：固定旧 5.5.4 基线 → 隔离安装 5.5.6 → 私有 adapter 编译/ABI 审查 → 双板构建与 codec/存储/系统 indication 回归 → 原固件虚拟路径 → 配置/绑定保持实物升级。旧共享 SDK 不原地修改，不直接跳 major，不把编译成功当作升级完成。

其他依赖分别升级：emulator/Bumble 影响测试可信度，ESP Web Tools 影响刷写生命周期，GitHub Actions 影响 Node runtime 与缓存。每次只改一组，避免多层同时升级无法归因。已删除的 cache v6 分支内容先审核再重新实施，不直接取其版本号为通过。

## 八、优先级排序待办

### 排序方法

P0 为核心链路/发行证据阻断；P1 为高影响可靠性、资源或升级前置；P2 为维护效率与增量增强；P3 为偏离当前边界或投入大的探索。**P0/P1 标号不自动表示生产缺陷**，每项标明“验收缺口/源码风险/优化/特性”。同档按影响、发生/触发机会、证据置信度、解锁作用、工作量和回归风险排序；不虚构收益百分比。

工作量为单人专注工程日粗估，不含硬件等待与发行审批；依赖及验收标准比绝对日期更重要。

1. **P0 / 验收缺口：当前精确镜像真实踩踏与五分钟实体耐久。** 收益最高；证据高；约 0.5–1 日，依赖功率计/人工骑行。验收：非零功率与同源踏频、逐路通知、掉线恢复、最低堆、配置未变，绑定 bin/ELF/source SHA；不能用 host 耐久替代。
2. **P1 / 验收缺口：Service Changed 已绑定无线确认与保留配置/绑定升级演练。** 约 1–2 日；先只读备份与明确分区。验收：旧绑定跨 GATT 表升级、0x2A05 范围/发送/确认、重启恢复，Windows/iOS/Android证据分列；任何读取/确认未取得保留 NOT_RUN/NOT_OBTAINED。
3. **P1 / 配置风险：coredump 栈与完整取证链。** 约 1–2 日；依赖隔离 panic 镜像。验收：1536/2048比较、dump CRC/完整 ELF、正确符号化、任务/堆开销，模拟器和实物结果分列。完成前不缩其他任务栈补偿。
4. **P1 / 验收缺口：双下游+真实 CPS 上游+WiFi峰值。** 约 1–3 日；依赖两个独立中心端与真实功率计。验收：三链路、扫描/恢复、热点保存/关闭、最低堆/最大空闲块、通知抖动与断线；BINAVI 经功率连接取踏频，骑鬥单角色边界独立记录。
5. **P1 / 资源优化：门户预算恢复。** 约 1–3 日，中回归风险。先删除可证明冗余内容、审查静态压缩和派生源；建议回到至少10%余量。验收：资源门禁、手机聚焦/手动缩放、双主题、隐藏草稿/保存失败、设备 HTTP 与离线演示全通过；不把 Flash 空间与堆混作同一指标。
6. **P1 / 源码风险：严格受限 JSON 导入与协议负例。** 约 2–4 日，中风险。验收：完整数值 token、范围/溢出、重复键/嵌套/空白、长度与Unicode、失败原子性、旧schema；明确哪些格式支持。先查实际调用边界，不无条件替换全部解析器。
7. **P1 / 升级前置：单一 NimBLE adapter 与 in-flight 契约。** 约 2–3 日，中高风险。验收：私有类型集中一处、锁顺序与生命周期声明、系统SC/FTMS/RSC/配置竞争负例、旧代次丢弃；行为/UUID保持。
8. **P1 / 补丁升级：ESP-IDF 5.5.4 → 5.5.6 独立候选。** 约 2–4 日，高回归风险；依赖2/3/4/7。验收：双板构建、配置/绑定迁移、完整host/虚拟/实物矩阵，无新warning/时序回归；未满足不切main基线。
9. **P2 / 工程优化：单一测试 registry + 依赖 doctor。** 约 2–3 日；先生成现有入口，不重写测试实现。验收：quality/operation/release/npm 登记一致、显式前置条件、严格无隐藏skip、每步退出码/耗时/证据等级；macOS OpenSSL与Python/Node Chromium检查明确。
10. **P2 / 可观测性：面向原因的轻量遥测。** 约 2–3 日。增加按服务/连接的订阅状态、通知失败分类、queue high-water、重连原因、NVS错误计数、deadline超期；优先复用现有20字节/只读快照或版本化扩展。验收：不逐包INFO、不存原密钥、不向公开包泄露设备身份、资源上限可测。
11. **P2 / 供应链优化：依赖锁、Action SHA、SBOM及正式来源证明。** 约 1–3 日。先审 cache v6和runtime兼容；固定提交而不是移动标签，记录许可证/来源/哈希。验收：干净环境安装、正式产物 attestation验证、ZIP内容门禁保持；不用签名替代功能验收。
12. **P2 / 架构优化：逐步拆分 BLE 与 Pages 巨型文件。** 约 3–6 日；依赖7/9。按服务描述、client session、indication、通知与Pages控制器切片；一次一个边界。验收：行为与生成字节/DOM契约一致、原异常测试不修改预期迎合重构；不同时换框架。
13. **P2 / 特性：配置档案导入导出与预览差异。** 约 2–3 日；依赖6。浏览器本地保存档案，含schema/firmware/来源、应用前差异和显式确认。验收：不导出绑定密钥，非法/旧版拒绝或明确迁移，失败保留草稿，不增加常驻设备缓存。
14. **P2 / 特性：短训练计划及回放。** 约 3–5 日；先 Pages/desktop 解析 ZWO/XML，只发送有界目标，设备保持唯一控制者。验收：暂停/取消/断连终态、模式退出恢复、目标范围、可重复时钟；桥接模式不擅自新增上游阻力控制。
15. **P2 / 特性：外部心率来源桥接候选。** 约 3–5 日，资源/连接风险高；依赖4/7。先验证3连接容量下如何分配，不简单再加一个central；明确真实/模拟来源。验收：过期心率失效、连接抢占拒绝、相同MAC客户端限制不冒称解决，真实目标App验证。
16. **P2 / 验证升级：codec/双槽/failover fuzz 与有界模型检查。** 约 2–4 日；先小范围固定seed。验收：边界/回绕/故障反例可复现、ASan/UBSan、限定状态空间与未覆盖项；不宣称全系统形式化认证。
17. **P3 / 探索：DirCon/网络网关。** 约 5–10 日；与临时WiFi边界冲突。推荐外部桌面/网关承载而不是C3长期联网；需单独明确用户场景、认证与网络暴露范围后立项。
18. **P3 / 暂缓：电机阻力执行、ANT+、RTOS重写、量产eFuse安全。** 均需新增硬件/生态或不可逆升级策略；不作为当前卫生工作顺手实施，也不把已有CPS曲柄模拟包装成物理校准能力。

建议批次：先1–4可信验收；再5–9资源与升级前置；然后10–12工程治理；最后根据需求挑选13–16增强项。可并行准备测试材料，但依赖未满足不发布或扩大能力声明。

## 九、系统工程设计理念：适用，不追热词

1. **Functional core / imperative shell。** 保持纯 C 领域函数，把ESP-IDF/存储/射频/时钟/随机数通过adapter注入；现有分层继续深化，不重新换语言。
2. **Single writer + 有界消息。** 配置由主任务唯一提交，队列有容量/超时/拒绝策略；snapshot把值、来源、时间、revision/session generation一起发布。其意义是消除竞态，不是引入通用Actor框架。
3. **能力驱动与契约优先。** Feature位、服务表、控制权限、通知订阅和网页可见能力来自单一描述；只声明实际可用能力。标准功能和客户端workaround区分，契约须可执行。
4. **Model-based / property-based / fault-first。** 先列不变量及故障，再选host、生产函数、真实NimBLE虚拟路径和实物层；反例与失败日志保留，不靠加sleep或更宽timeout掩盖。
5. **资源预算也是功能需求。** 维护Flash、静态RAM、最大空闲块、任务栈、HTTP缓冲、链路数量、通知时延和门户字节的独立预算；用实际trace/峰值而非经验数值优化。
6. **Evidence-driven delivery。** source/config/toolchain/bin/ELF/test环境与恢复状态组成交付身份；PASS绑定具体镜像，不在文档间自动继承。供应链 attestation只是其中一层，不替代射频验收。
7. **渐进重构而非全面重写。** 一次抽一个边界，保持旧接口、固定数据集和负例，确认等价后再删除旧路径；给依赖升级保留对照与回退。
8. **Local-first / progressive disclosure。** 训练计划、档案和大数据留在浏览器/桌面；C3负责实时协议与有界执行。手机核心数值保持精简，不重复增加二级教程或FAQ。

这些方法并不是全部在2026年新发明；真正“新”的价值是把能力声明、真实协议仿真、自动故障注入、可验证来源及资源预算合成一条可执行工程链，而非贴DDD、微服务、数字孪生或AI-native标签。

## 十、交付及后续范围

维护工具批已提交并推送 `91a5946`；文档批单独验证/提交。详细执行记录、首失败与复跑日志、清理清单、源码规模及构建身份在本次交付证据包。Git bundle单独保存供恢复，不上传到源码仓库。新功能、SDK升级、设备刷写及新Release均不属于本轮已实施内容。
