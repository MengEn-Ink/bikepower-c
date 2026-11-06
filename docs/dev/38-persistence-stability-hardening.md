# 持久化稳定性加固

## 本次提交前复验与状态

在用户随后授权“测试没问题后提交”后，对当前工作区重新验证，而非复用上轮全绿结论：

- fetch 确认 main 基线 `a37f372` 未新增；全部 36 个待提交文件在复验期间校验值一致。
- 严格完整门禁 `--no-build --strict`：117 PASS / 2 工作区 WARN / 0 FAIL，exit 0，无依赖跳过；两个 WARN 仅为尚未提交的工作区状态，工作区/暂存区与基线 whitespace 已独立通过。
- 独立 NVS ASan/UBSan 3584 断言；普通 Host 与 `-Wall -Wextra -Werror` ASan/UBSan 均为 6233/0。
- 独立 ESP-IDF `build-review reconfigure/build` 成功，app `0x115320`，factory 余量 72%；不是发布镜像。
- 三分钟核心 mock 15 轮、180.9 秒、exit 0，服务退出并生成报告。
- 源码批 `05a8627`（配置）、`2c4b0fc`（存储）及 `ebc26a7`（测试工具）已分批提交并 push main；文档批同步本报告。原修复阶段“只在本地”陈述保留为历史，不代表当前状态。
- 没有烧录、tag 或固件 Release；下方 `NOT_RUN` 与降级限制不因提交改变。最终文档批 SHA、远端 CI 和清洁状态另在提交交付记录中核对，不预填成功。

## 本轮目标与边界

- 继续上轮 `a37f372` 基线的本地审查修复，不丢弃未提交变更。
- 优先处理多键 NVS 半更新、配置与 DIS 分步提交、迁移与测试门禁可靠性。
- 不修改八组 BLE 线格式、UUID、config schema v14、骑行/跑步模型或 RF 配网入口；不烧录、不提交推送、不打 tag、不发布。
- 稳定性只能由范围明确的证据支持。本报告的掉电是生产调用路径上的故障替身，不等同于真实 flash 掉电验收。

## 根因与方案

原 `nvs_store_save()` 连续写多个 NVS 键，某个键失败后，之前成功的 setter 已写入存储。测试使用 immediate-write NVS 替身执行生产 C：功率新值、踏频旧值，旧代码断言失败。网页复制身份又先写独立 namespace，再保存配置，掉电可留下配置/身份不匹配。

方案为两套提交记录：

1. `snapshot0` / `snapshot1` 保存完整领域配置 + DIS 身份。
2. `commit0` / `commit1` 标记对应槽位与代次，独立 CRC。
3. 修改时只覆盖非活动槽，完成写入、commit、回读验证后再写该槽提交标记并回读。
4. 启动同时检查标记、快照、代次、长度和 CRC，只选最新完整已提交槽，不使用仅有快照的候选。
5. 上一活动槽与标记保持不变，模拟任一中断后恢复完整旧值或完整新值，禁止字段混合。

不使用“最大代次快照即成功”方案，因为未写提交标记的候选不得提前生效；也不镜像写回旧键，否则会重新引入多键半更新。

## 实现

### 纯 C 快照编码

- [config_snapshot.c](../../components/bikepower_domain/config_snapshot.c) / [头文件](../../components/bikepower_domain/include/config_snapshot.h)：16 字节头、显式小端整数、长度前缀字符串、LED、桥接 wire、DIS wire。
- magic `0x42505331`、存储格式版本 1、总长度、uint32 代次；CRC32 覆盖头（CRC 字段除外）和负载，CRC 标准向量 `123456789` = `0xcbf43926`。
- 缓冲上限 640 字节，所有当前最大字段约 540 字节；无 C struct 原样落盘、无新增动态堆分配。输入 schema/范围/身份文本校验后编码，解码失败不替换输出。
- config schema 仍为 14；存储格式与 BLE 协议版本独立，后续 schema/存储格式变化必须加显式迁移，不允许依赖当前布局猜测。

### 存储与失败闭锁

- [persistent_store.c](../../main/persistent_store.c)：单主任务调用，静态 mutex 与预分配缓存；不在 ISR、NimBLE host 或通知回调执行 flash I/O；mutex 串行保护完整存储操作，业务引擎应用在存储返回后进行。
- 候选槽写/commit/回读失败：返回 false，保留原缓存和活动标记；允许之后重试。
- 提交标记写/commit/回读失败：结果可能不确定，保留原运行态并闭锁之后保存。重启重新选择完整槽。失败不承诺新值一定没落盘，但不会产生半套配置。
- 所有槽不可解码或遇到读取 I/O 错误：安全默认配置、全空 DIS 与禁止覆盖，保留错误数据供人工核对。
- 最新槽损坏但上一已提交槽有效：整体回退上一槽，不拆字段拼接。

### 旧配置迁移

- [nvs_store.c](../../main/nvs_store.c) 仅在没有提交标记时读旧 `bikepower` 键；[device_identity_store.c](../../main/device_identity_store.c) 只读旧 `bp_ident/dis`。
- 继续执行原 v6-v14 migration 与范围校验；读失败/损坏时不把不完整值迁移覆盖。
- 旧字段使用映射表统一读取，LED 在缩窄为 uint8 前处理范围，避免非法大值模 256 后伪装合法。
- 初次快照写入打断而无提交标记：旧键仍在，下次可重试。
- 首次提交标记截断但槽位 0 generation=1 完整：只读恢复旧键并禁止继续写，不能将未提交候选升级为成功；保留故障证据不自动擦除。
- 不删除旧键，不镜像后续新值到旧布局。**降级旧固件只能看到迁移时旧配置，不能读取之后的新保存配置。**

### 联合应用与身份更新

- [app_main.c](../../main/app_main.c)：门户复制身份与完整配置经 `nvs_store_save_with_identity()` 一次提交，去掉先写身份再回滚的两次保存。
- 单独 DIS 编辑也联合保存当前引擎配置，避免用户按钮调功还没周期落盘时，被身份编辑写入旧缓存配置。
- 只有存储完整成功后替换引擎、发布 DIS、更新 revision/结果；失败保留原运行态。
- 清空 DIS 保存全空覆盖，不删除旧迁移键，防止重启后旧身份“复活”。

### 测试门禁

- [test_persistent_store.py](../../tests/test_persistent_store.py) 编译生产快照、存储、NVS 和身份模块，使用 ASan/UBSan 与 `-Wall -Wextra -Werror`。
- [test_persistent_store.c](../../test/test_persistent_store.c)：六个读写/commit阶段 × 三种故障，明确前提交阶段只能旧值；模拟中断后重载；首次迁移相同矩阵；截断/逐字节篡改、最大长度、旧键迁移、身份联合保存/清除、I/O 错误、腐败回退、uint32 代次回绕、1000 次保存重载。
- [quality_gate.py](../../scripts/tools/quality_gate.py) 加 `--strict`：缺 host 工具或专项依赖计 FAIL；host 输出断言零失败还必须 returncode=0；每个专项限制 180 秒，避免无限等待。
- [test/Makefile](../../test/Makefile) 所有领域头加入每个 host 目标依赖，头文件变动必重编；与 `filter %.c` 配合不把头误当链接源。

## 验证结果

本节记录本轮实际执行结果；模拟故障不当作真机证据。

- 原代码 RED：生产多键路径在指定写入失败后重启出现混合配置，断言失败，证据 `nvs-atomic-red.log`。
- 初轮双槽、迁移、回绕与身份测试通过；新增首次迁移矩阵曾发现标记截断后错误回默认，已修复并复验。
- 既有 host 和严格 ASan/UBSan 均为 6233/0；骑行/跑步算法未改。
- 新增生产 NVS/快照 ASan/UBSan 专项 3584 断言通过，含 1000 次保存重载。
- 最终严格质量门禁 `--no-build --strict`：117 PASS / 2 工作区 WARN / 0 FAIL，exit 0，无依赖跳过；构建独立执行。
- 最终 `idf.py -B build-review reconfigure build` 通过，app `0x115320`，factory 剩余 72%；未生成发布镜像。
- header rebuild dry-run：模拟 domain_config.h 更新，五个 host 目标全部重编；不改真实头文件时间。
- 64 份 Markdown 链接与源码事实、diff whitespace 通过。
- 最终三分钟核心 mock 流程：15 轮、181.1 秒、exit 0，服务退出与报告落盘均通过；host 独立执行，mock 不是实际 flash 证据。
- 全局源文件新增 `main/persistent_store.c` 与 `components/bikepower_domain/config_snapshot.c`，源码事实同步生成。

## 实物待验收与残余风险

- `NOT_RUN`：本轮镜像烧录、真实 flash 擦写/断电、NVS 分区满与垃圾回收、WiFi/BLE 共存堆/CPU/时序、M5Stamp C3、手机 Safari、第三方运动 App、按钮/LED。
- 故障替身验证状态机和调用顺序，仍依赖 ESP-IDF NVS 单项记录及 CRC 的介质恢复能力；不会对物理介质不可恢复损坏作保证。
- 在提交标记不确定后的闭锁期间，保存返回失败，用户须重新启动核对；不通过自动擦除/自动重启隐瞒错误。
- 旧固件降级语义见上文；未来存储格式迁移需独立实现与故障回归。
- 领域 JSON 导入仍是紧凑格式轻量提取器，重复键/键作用域/数值溢出全面治理未在本轮范围内；生产 HTTP 先走 cJSON，不据此宣称所有导入路径已严格化。
- FTMS Encryption/配对策略仍待完成，不能因离线门禁通过宣称认证合规。

## 交付

修改只在本地仓库，包含上轮保留修复；本轮完整补丁、稳定性日志和构建证据包交付到会话输出目录。上轮审查报告保留当时事实并增加本报告链接，不用新结果覆盖旧证据。
