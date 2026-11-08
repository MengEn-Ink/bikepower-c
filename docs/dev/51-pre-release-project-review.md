# 发版前项目复核与优化清单

## 后续实施状态（v4.4.0，仅 generic-c3，未正式发行）

用户已授权按三组建议实施，版本和板型已明确。R1/R2/R3修复已写入生产及正式测试入口，旧审查/失败原件在下方保留，不能把历史“未修”当成当前状态。

- stop先取生命周期锁，超时完整保持运行态；Host stop失败保留未就绪并禁止清理失败后init。正式生产函数ASan/UBSan专项通过。
- ZIP条目size/SHA256/CRC/重复名与内部manifest前置映射核验已通过原样例及四新增破坏负例。
- 正式兼容性23 Node及browser、配置单/双轮通过；同进程耐久600.6秒/47轮、3次周期host和末尾关闭拒绝通过。
- 完整55次专项首轮53/2：5处当前版本文档未同步已修、299/0复验；门户续期toast原断言三宽复跑与独立闲置三宽通过，首FAIL保留，不抹除偶发边界。
- 新候选app1165856字节、SHA256 `b30c06371ebd403c2a7f500cf02ac8aaad8ab5b6c5c008e567c904f66adbfbeb`；三链路实际NimBLE9PASS，原安全/跑步PASS/OPEN1，SC仍NOT_OBTAINED。
- COM205刷前WinRT整表枚举E_UNEXPECTED，ROM复位仍失败，定向服务发现Unreachable；未清配对或修改系统蓝牙。ROM NVS双读24KiB一致、65有效记录CRC及分区布局一致。
- 在保留NVS的前提下仅应用区新候选刷写、独立verify digest matched、20秒ROM启动无致命异常通过；当前版本BLE/P0完整证据仍待取得，不能发布。
- 当前源码将在白名单分批提交后隔离构建；现有候选内嵌Git仍92e2ffd。当前版本BLE/P0无法取得，最终tag和Release不执行。刷后NVS双读一致，65有效记录与刷前无差异。
- 当前版本Pages47项Node和11个browser入口全通过，docs79/2WARN/0FAIL；依赖IDF的dirty标识保留，大部分文件模式变化，非参与构建示例/RIOT文本修改未清理。

## 历史审查结论

**当前业务主线可以进入发版收尾，但尚不建议直接发布。** 用户最新反馈桥接已正常；该反馈按“用户确认当前场景正常”记录，不扩展成具体设备矩阵、时长、WiFi共存或全部故障验收通过。

本轮只做源码审查、独立构建、离线/浏览器/虚拟栈验证及报告更新。没有修改生产C代码、既有测试、版本号、实体设备配置或固件，没有清绑定、commit/push、tag、PR、Release。此前COM205候选保持不变。与上次交付审查包比较，27个生产/测试/脚本白名单文件SHA256无差异。

建议发版前分三批小范围处理：

1. 生产异常路径修复：BLE停止锁超时完整回滚，补失败注入。
2. 发布工具修复：ZIP实际内容一致性、重复项/CRC校验；测试夹具及耐久前置修复。
3. 发布记录收尾：确定发布板型/版本，提交明确白名单并从提交版本干净构建，收录精确镜像验收证据，再核验远端CI、tag、merged bin、SHA256、manifest和总包。

以上是建议，未在本轮擅自实施。没有必要为此次发版增加上游控制、重设计GATT或扩大到未验板型。

## 审查范围

- 生产：`main/ble_bikepower.c`客户端槽、广播、订阅、响应FIFO、Host调度、FTMS/RSC控制与stop/start；`ble_config_service.c`事务归属；`ble_telemetry_service.c`定向链路；`app_main.c`配置保存、布局冻结、主循环就绪检查；上游`ble_power_bridge.c`及`radio_ctrl.c`。
- 发布：`scripts/build_all.sh`、`scripts/tools/check_release_artifacts.py`、`publish_github_release.py`、`device_acceptance.py`、版本一致性与CI接线。
- 回归：quality_gate完整专项清单、Pages全单元及11入口浏览器、独立ESP-IDF构建、实际NimBLE三链路/原协议、核心流程耐久尝试。
- 排除：没有操作COM205/其他端口、系统蓝牙、WiFi、文件清理、旧报告删除、正式发布资产覆盖。私有NVS、密钥和运行Flash不交付。

## 发版前应修复

### R1：BLE停止锁超时没有保持原运行态（P1，生产异常路径）

位置：`main/ble_bikepower.c:2421–2437`，`ble_bikepower_stop()`。

当前顺序先停止上游、设置stopping、停止并删除通知定时器，再等待通知锁。若等待超时，函数只撤销stopping并返回false，`g_ready`仍为true，通知定时器却已NULL，上游也已停止。

已使用真实生产stop函数做ASan/UBSan host故障注入：强制锁获取失败后确认返回false、ready仍true、定时器已删除且没有恢复。这不是正常桥接实测异常的归因，也不意味着每次运行会触发；它是确定的失败回滚缺陷。GATT重建调用方会重试，RF降级调用方会重启，但失败后仍有不一致运行态窗口。

建议先取得生命周期锁再做不可回滚资源停用，或保存并恢复通知定时器和上游期望状态；需要避免Host事件与stop持锁等待互相阻塞。测试覆盖锁超时、`nimble_port_stop`失败、正常停止及再次启动，确认“返回失败”的语义和ready/定时器/上游状态一致。

证据：`build/pre_release_stop_probe.py`，输出 `REPRODUCED: stop返回失败但ready仍true，通知定时器已删除且未恢复，上游先行停止`。该文件是临时验证夹具，未替代正式测试门禁。

### R2：发布ZIP仅核对文件名，不核对条目内容（P1，发布可靠性）

位置：`scripts/tools/check_release_artifacts.py:255–262`。

外部固件、sidecar及manifest已有哈希检查，但ZIP目前只把`namelist()`转set，确认文件名集合。集合会丢失重复项信息，也没有检查CRC或条目字节与外部已校验文件是否相同。

已在独立临时样例中把ZIP内generic固件替换成不同内容，保留外部固件不变，并更新ZIP自己的sidecar和manifest大小/hash；当前门禁仍放行。说明“顶层附件正确”不能保证下载总包里的固件正确。未篡改任何真实发行包。

建议流式核对每一ZIP条目的大小/hash与已验证外部文件，拒绝重复条目和损坏CRC。`manifest.txt`在打ZIP后追加ZIP本身记录，不能机械要求包内manifest和外部manifest字节完全相同，否则产生自包含哈希循环；应验证包内manifest对应的前置文件映射是外部manifest的精确子集，并明确允许外部新增ZIP及sidecar记录。

增加“内部固件不同但ZIP哈希有效”、重复条目、CRC损坏、缺失/额外文件负例；不要仅检查ZIP自身SHA。

证据：`tests/reports/pre-release-review-zip-probe.json`，`gate_rejected=false`；复现脚本 `build/pre_release_zip_probe.py`。

### R3：完整门禁有过时夹具与耐久前置（P2，发版测试阻断）

#### 兼容性两个入口

- `tests/test_ble_compatibility.cjs:181–194`提取生产subscribe函数却漏装新增`assertGattConnected`依赖，并且伪server没有connected属性，原测试报ReferenceError。
- `tests/test_ble_compatibility_browser.cjs:85`传入的枚举server没有connected属性，生产连接守卫正确拒绝，而夹具原以为仍可枚举。

在内存中补齐生产连接检查函数与connected=true后，原23项Node及独立浏览器入口通过；没有改生产页面、原测试文件或业务断言。这只证明定位正确，不等于原门禁已经修复全绿。

建议正式修夹具，并补connected=false拒绝用例，保持生产断连保护不变。

#### 核心流程耐久

`test_core_flow_endurance.py`反复调用`test_portal_config_save.exercise()`。第一轮会修改broadcast_mask、最后进入closing。第二轮仍断言必须是出厂mask15，失败；即使只改这个断言，closing还会使下一轮保存被拒绝。

本次请求10分钟，实际14.3秒/1完整循环后第二轮失败，**不算10分钟耐久通过**。`--host-every-minutes`当前参数未被消费，周期host验证也不能宣称已执行。

建议拆分一次性的fresh-default验证与可重复的业务cycle；每轮恢复业务原配置，WiFi close/reopen作为独立有明确定义的会话场景。不要把反复重启mock等同于同一进程耐久。故障注入/清理出错时仍需报告FAIL。

证据：`tests/reports/pre-release-review-endurance.md`，第2轮在`test_portal_config_save.py:46`失败。

## 本轮已验证

### 独立构建

Windows ESP-IDF5.5.4，独立目录`build/pre-release-review-firmware`；生产配置保留WARN、禁止开机自动WiFi、generic-c3、总连接3/CCCD48/活动6，构建通过。

- app：1,165,744字节。
- app SHA256：`07f931bec663f770224d62bc6661986a79e927c13827829b025a5ea59191bb6c`。
- 原COM205留板app SHA256：`070cf289b758ecc013bbbdb5758411a814d8998c923024e68d75861121538e7a`，未重刷。
- 两次构建源码相同但哈希不同；构建内嵌时间且当前工作区dirty，不能把新构建当原COM205镜像或干净92e2ffd发行镜像。
- app分区剩余71%，当前不存在必须压缩固件体积的证据。没有因此缩任务栈、删服务或改变功率模型。
- 日志`build/pre-release-review-build.log`，无本轮新增编译错误。

### 完整专项清单

跨Windows/WSL执行quality_gate完整清单和3项文档检查，共54次专项调用（兼容性拆成C/Node/浏览器3步）。第一次50 PASS / 4 FAIL，无静默依赖跳过。

- 生产C和NVS安全、客户端隔离、控制点、配置、快照、绑定/缓存、遥测、桥接主备、protocol向量等通过。
- 门户runtime/faults两项首失败是未设置真实IDF cJSON路径；指定`IDF_PATH=/mnt/d/workspace/bikepower-c/build/toolchain/esp-idf-v5.5.4`后均通过，不属于生产错误。
- 两个兼容性原入口仍FAIL；内存修正夹具验证通过，未冒称正式门禁全绿。
- `tests/reports/pre-release-review/result.json`和00–53逐项日志保留初始结果；归因后的结果为52项已通过、2个原入口仍需修复，不是原生单环境`quality_gate --level full --strict`整条调用PASS。
- 严格docs门禁79 PASS / 2工作区WARN / 0 FAIL；对外版本一致性6个文件、0不一致；Git diff whitespace通过。工作区WARN不是正式干净发行豁免。

### Pages/浏览器

系统Chrome、Playwright1.64.1，生产页面使用现有测试harness中的传输替身，不使用真实BLE/串口。

- Pages Node 47项PASS，另DOM和遥测解码专项PASS。
- `npm run browser --prefix tests/web-flash` **11个入口全PASS**，包括双主题多宽、连接/取消/握手失败、ESP Web Tools、教学、手机交互、设备状态、桥接、审查；手机原像素差异0。
- 门户320/390/1440三个宽度的真实浏览器render和HTTP异常恢复通过。
- 额外BLE兼容性原入口的旧夹具失败与上述11入口全PASS分别记录，不将两个范围混为全站全绿。

### 实际NimBLE虚拟栈

新独立构建的4MB虚拟镜像SHA256：`1129284390b3cd2c4a21a2af1d346de74c91874b3c85ce414a9d63272aa65f64`。

- 三链路9项PASS：双下游发现、同快照、FTMS控制者、配置归属、双在线布局保存拒绝、单台断连/重连、真实栈CPS上游各53帧转发、上游掉线自动恢复和251W转发、原八组恢复/reset。
- 证据：`tests/reports/pre-release-review-virtual/session-1794147592992306384/report.json`。
- 原全配置、安全、跑步回归PASS/OPEN1：`tests/reports/pre-release-review-original/session-3fkt24re/report.md`；SC指示仍NOT_OBTAINED，不是PASS。
- 虚拟PrivacyMode/RSSI/IRK局限、真实RPA、AES-CCM射频、系统缓存和WiFi共存不覆盖。没有使用本轮虚拟PASS代替用户设备实测。

## 可延后，不建议发版前大改

1. **BLE模块拆分和旧字符串夹具清理（P3）**：ble_bikepower较大且单连接历史注释/未执行字符串夹具残留。后续按生命周期、控制、存储拆分；现在以补准确测试为先，避免结构重构影响已实测路径。
2. **IDF私有头封装与版本guard（P2）**：当前直接用固定5.5.4内部在途结构有明确耦合。后续抽薄适配层、升级时编译失败关闭并复核真实栈；不能简单删除在途保护来“去耦”。
3. **配置事务超时/诊断（P2）**：BEGIN后客户端保持连接却不完成可长期占住全局写事务；这是既有单写策略边界。是否加空闲取消需另定产品语义/超时，不在发布前擅改协议。
4. **三链路CI接线（P2）**：当前virtual-board CI运行原security/running/GDB，但未调用新的`virtual_board_multi_client.py`；Host多客户端已在quick入口。建议新增真实栈三链路作业及仅公开report上传，避免以后仅原单链路回归全绿。
5. **构建脚本隔离（P2）**：`build_all.sh`默认双板，直接清固定release目录、根sdkconfig和临时sdkconfig.board；dry-run也注册EXIT清理。已有本地文件时不应原地盲跑。建议每板独立构建、候选目录生成后原子发布，清理仅本轮创建的临时文件，不清用户原有文件。此风险由脚本确认，未执行删除实验。
6. **发布验收口径（P2）**：现有P0记录包含单客户端骑行/重连/5min，却没有专门的2下游+1上游和WiFi峰值字段。新功能发布应提供额外显式三链路证据，不依赖旧P0全绿推断。
7. **可复现构建（P3）**：内嵌本地时间使同源app哈希变化，仍可通过精确构建hash验收。后续可采用SOURCE_DATE_EPOCH/UTC和dirty显式标记；这不是要求把不同镜像证据混用。

## 正式发布收尾条件

- 当前HEAD仍`92e2ffd`，三链路未提交；生产版本仍4.3.3，现有`build/release`根manifest缺失，当前目录不能直接作为本次待发总包。
- 修R1、R2、R3后，从最终提交版本重建并复跑相关门禁，不能沿用本轮dirty候选PASS。先确定发布板型；没有新M5Stamp实物证据时不要默认双板发布。
- 功能新增建议采用下一个minor版本（例如4.4.0），最终版本由发布范围确定；本轮未改任何版本。
- 用户反馈正常可作为对应人工场景验收，但未提供设备矩阵、测试时长、WiFi压力数据，因此这些保持“未取得具体证据”，不反复要求用户证明已说过正常的同一现象。
- 三链路+WiFi最低堆/CPU/最大块及连续通知、SC直接ACK、实际5分钟以上连续耐久/掉电、全部目标终端、macOS/M5Stamp仍未在本轮验收。SC不得写成已捕获通过；也不得未经因果证据再次增加CONNECT补发补丁。
- 提交明确白名单，保留无关未跟踪工作；从clean提交生成merged bin、SHA256、manifest、测试/耐久/手册、证据及总包，校验每个ZIP实际内容，再执行推送/CI/tag/Release链路。远端动作需在明确授权的发布阶段执行。

## 本轮交付

本报告、更新的三链路记录及非敏感复核证据。临时build脚本不作为新项目生产入口，旧失败日志不删除。没有创建正式固件包或飞书文档。
