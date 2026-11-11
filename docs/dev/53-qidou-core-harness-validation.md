# 骑鬥适配与核心测试 harness 验证

## 结论

本轮未满足“全部测试无问题后提交合并并发布”的条件，生产补丁与 harness 原地保留，未 commit、push、tag 或创建新 Release；固件版本仍为 v4.4.0。目标客户端心率接入与加密训练控制仍未完成。

## 测试范围与镜像身份

- 实体为当前 USB 连接的通用 ESP32-C3。前阶段临时配置恢复并独立回读通过；本轮续测发现当前已为模拟source0/mask15，与历史桥接基线不同，维护及收尾均保留当前配置，详情见续测段。
- 实体已验证候选应用 SHA256：`46b3ee6fad558d69253bfa372a888625fe62f5a5ea3844375a0c7784640f37cf`；其板端遥测 Git 为旧构建缓存 `92e2ffd`，是 dirty 候选，不冒充提交级发行证据。
- 重新 reconfigure 当前源码后，独立应用 SHA256：`7c2b1bf0d6116609cd5f16abef02320ef9231b7241b79a1ceffb34950a927616`，约 1140 KiB。实际 NimBLE 虚拟运行身份为 `eb318fe`。此重建镜像没有重新烧入实体，不把旧实体 PASS 迁移到该镜像。
- 两个镜像均不升版本；没有打包、上传可分发新固件。

## 已执行结果

| 层级 | 结果 | 证据与边界 |
|---|---|---|
| 生产 Host | PASS | 双客户端容量/独立订阅/响应/冻结、标准控制/广播及领域完整 6246/0；不证明射频与 App |
| 五轮实体数据 | PASS | 五次独立连接，每轮60秒；mask15/7交替，CPS与HRS每轮各60帧，非零功率、CPS曲柄踏频和心率都通过 |
| 八组实体配置E2E | PASS | stale revision、CANCEL、不完整/非法事务、修改、重连、ROM持久化、掩码GATT重建、原配置恢复全部通过 |
| 移动门户 | PASS | 320/360/390/430/844五宽度，触屏输入聚焦、短高度、运动/来源切换；缩放1、无横向裁切、可见输入>=16px。重复两轮通过，不等价Safari/iOS WebView |
| 门户异常/桥接 | PASS | 320/390/1440三宽度生产压缩页面；失败重试、丢响应、重复保存、扫描取消、关闭及桥接流程两轮通过 |
| 同进程mock耐久 | PASS | 601秒45轮，末尾关闭并拒绝保存/续期；该轮显式skip-host，领域Host另执行，不冒充无线耐久 |
| 实际NimBLE虚拟安全/跑步 | PASS / OPEN | 配置、重启、绑定复连、跑步通知、恢复通过；Service Changed直接指示仍NOT_OBTAINED，不计PASS |
| 核心harness负例 | PASS | 最终12项：零值、无踏频、错误ERG、突发/停更、失败轮、恢复失败、独占、依赖、取消、控制略过、错误构建身份拒绝 |
| 最终统一核心suite | PASS | 依赖补齐后新目录完整两轮，共14次执行全PASS；原缺依赖suite仍保留FAIL |
| 文档/常量 | PASS | 文档79PASS/2工作区WARN/0FAIL；常量299/0，手机离线手册已同步 |

五轮实体数据摘要：

| 轮次 | 掩码 | 功率W | 踏频RPM | 心率bpm | CPS/HRS帧 |
|---|---|---|---|---|---|
| 1 | 15 | 227–303 | 84.05–95.03 | 156–168 | 60/60 |
| 2 | 7 | 233–307 | 85.99–97.06 | 165–174 | 60/60 |
| 3 | 15 | 239–296 | 83.99–92.04 | 169–174 | 60/60 |
| 4 | 7 | 231–306 | 84.05–95.03 | 162–173 | 60/60 |
| 5 | 15 | 240–304 | 91.02–96.00 | 152–168 | 60/60 |

上述为模拟骑行输出，不是人体测量，也不替代上游真实功率计踩踏验收。

## 心率问题分层结论

1. 测试前桥接快照 `core[14]=3`，只启用功率/踏频，HRS没有注册。因此该原配置不显示心率是配置关闭，不是协议缺陷。
2. 临时模拟开启掩码15后，骑鬥v2.6.6功率约247–297W、踏频87–93RPM持续显示；心率搜索能找到同一开发板，但两次选中后停在“正在连接”，未取得心率数值。此项保持FAIL/待排查。
3. 骑鬥连接中的共享只读WinRT探针没有发现Config服务；释放骑鬥后同目标独立探针通过，20秒CPS/HRS各21帧、心率178–181bpm，配置未改变。五轮HRS进一步确认设备能够通知；这不能证明骑鬥正确订阅，也不能确定第三方App是唯一根因。
4. 短时Bthmini/BTHPORT ETW仅取得日志头，没有有效HCI/ATT事件，无法定位订阅、连接数或认证阶段，不把空采当成“无错误”。采集已停止。
5. 仅HRS场景成功提交与回读；旧诊断脚本仍尝试订阅已关闭的CPS而失败，归为测试前置错误。骑鬥单HRS优先/连接顺序对照未进入有效设备验收，不判通过。该临时配置已恢复。

## 发布阻塞

- 骑鬥功率、踏频、心率三项同时连接且连续显示未通过。本轮板端 ATT 订阅实证：骑鬥只订阅 FTMS-Bike/Status/CP，HRS 订阅 0 次；主广播加入 180D 后心率设备可被搜索列出，但点选后仍未建立 HRS 订阅。连接阶段根因（客户端角色占用/未配对）未闭合，纯心率单链路对照未取得干净样本。
- 新核心控制E2E第一轮写FTMS RequestControl收到ATT `0x0F Insufficient Encryption`；只读系统状态为未配对。正常系统配对请求被用户跳过，未自动接受配对、未清绑定、未降低WRITE_ENC。完整控制/ERG五轮未执行成功。
- WiFi在线下游的桥接→模拟保存入口仍缺本轮实体端到端证据；BLE配置测试不替代HTTP入口。Safari、iOS聚焦放大与真实热点未测，Chromium触屏回归不迁移为真机结果。
- 最终提交镜像的严格P0/全量quality、远端CI、升级路径和发布附件审计尚未执行。当前dirty工作区及已测缓存身份不满足提交级镜像证明。
- 原微信两张临时截图路径已失效，已请求重传但未收到；不能核对原图片细节。现有截图和实测独立有效。

## harness 改造

- `tests/core_suite.py`：固定七个核心用例登记、host/browser分层、依赖前置、argv直传、超时、每轮完整日志；有历史suite的目录拒绝覆盖，失败不能被后续PASS冲掉。
- `tests/core_harness.py`：OS独占文件锁、原子报告写入、非零功率/心率/曲柄踏频、时长/停更/ERG及轮次聚合断言。锁仅约束遵守此锁的本机脚本，不保证第三方App/旧脚本/另一台电脑不占设备。
- `tests/core_device_e2e.py`：精确目标、完整八组快照、DIS原快照身份核对、可选Git身份fence、模拟mask15/7重复连接、控制响应及最后五帧ERG；取消与恢复失败分离，恢复后重启独立回读。
- `--data-only`明确输出控制NOT_RUN和`full_acceptance=false`，不可为发布补填控制PASS；`--firmware`记录的是期望哈希，不等价Flash读回。
- `ble_release_control_soak.py`补精确3字节控制响应、真实ERG断言、取消/恢复/释放错误。原先使用hex字符串长度>=6本身不是“至少六字节”错误，本轮只是收紧精确长度；此前口头表述已更正。
- 假帧夹具补实际坏CPS payload、连接释放与模拟控制功率；只加速>=3秒的Host重启/重连等待，不缩短实体测试，也不降低ERG/数值门限。
- quick/full与quality入口接核心管理/编排负例和移动聚焦回归；新入口及边界详见[tests/README](../../tests/README.md)。
- 本轮 `check_ble_advertising_fields.py`、`check_config_consistency.py` 解析广播拼装块的正则原硬编码 `adv_uuids[3]`，容量扩容后会解析为空并误报全部分支 FAIL；两处放宽为 `adv_uuids\[\d+\]`，与数组容量解耦。

## 保留的失败与恢复

- 初始Windows独占锁读取锁区报PermissionError：改为只看文件size后锁定，负例复跑通过，未删除锁强行并发。
- 两次suite分别缺pyserial、bleak，报告保留FAIL；仅给项目隔离test-venv补已声明依赖。独立transport复跑两轮PASS。
- 在前一suite占锁时另起transport-suite被正确拒绝；等前一任务结束后新目录执行通过，未绕过独占。
- 手机离线手册门禁先FAIL，按当前来源生成后79/2WARN/0FAIL；原失败日志保留。
- 第一次控制E2E整体FAIL但恢复与释放PASS；数据-only五轮整体只在对应范围PASS，不代替完整验收。
- 私有原始快照、NVS/绑定备份及原始ETW保留在本地tests/reports，不随公开文档分发。

## 本轮续测：定向绑定维护与前置门禁

- 重新读取历史产物和 IDF/NimBLE 布局，确认 `ble_addr_t` 为 type + 6 字节地址（地址从偏移1开始、小端显示需反序），安全记录首字段为该结构。最初偏移2产生的末字节00不是有效身份依据。
- 已导出NVS双读一致且CRC通过；两条peer中一条与当前Windows默认蓝牙适配器精确匹配，而Windows未配对，双端绑定状态不一致已证实。NimBLE已有bond时会发REPEAT_PAIRING事件，当前应用无重试清旧bond处理；源码解释了可疑路径，但没有有效SMP事件，不单独认定Failed19根因。
- 用户本轮明确授权只定向清本机旧绑定。维护前重新双读NVS，按当前记录位置仅令 `our_sec_1/peer_sec_1/cccd_sec_1/csfc_sec_1/rpa_rec_1` 失效；修改6字节entry位图，只写两个NVS扇区，不写应用区。写后两次独立回读逐字节等于预期；另一peer、本机IRK、用户配置及所有其他有效记录保持。私有原始payload与备份不分发。
- 首次维护因test-venv缺esptool在写前失败，FAIL原件保留；改用现有IDF解释器后执行。`--no-stub --after no_reset run`虽然退出0，实际日志为Staying in bootloader，不能证明运行恢复；随后正常esptool硬复位、15秒ROM启动无致命异常、独立严格八组/DIS回读通过。原维护报告的run_restored布尔不能单独作为恢复证据；本段以随后boot和回读为准。
- 释放本轮骑鬥后发现当前配置已是模拟source0/mask15，与历史桥接source1/mask3不同；维护不覆盖此新状态。最终独立回读source0/mask15、八组与维护前当前基线一致，序列号一致，连接释放。
- 定向清理后标准WinRT Encryption配对仍Failed19、paired_after=false，未自动确认；主桌面授权超时，系统配对接管被跳过，因此正常系统配对/五轮加密控制保持未通过。旧绑定不是该标准API路径失败的全部原因，不继续清记录或降低WRITE_ENC。
- 维护后只读60秒CPS/HRS各61帧，配置未变化；骑鬥重新启动自动连接功率与踏频，观察275W/87RPM，随后254W/94RPM与249W/88RPM。心率可发现精确目标，成功进入正在连接后等待60秒仍无数值。已取消并退出骑鬥，最终独立配置回读通过。清旧绑定不足以解决心率；HRS订阅与客户端握手根因仍未闭合，不将缺少日志计为无错误。
- harness新增设备依赖/Windows配对前置：缺esptool或未配对在连接/写配置/复位前退出，控制NOT_RUN、restore NOT_REQUIRED、full_acceptance=false；报告与按报告名的私有快照均防覆盖，快照补DIS串号供恢复身份校验。8项编排Host用例、2项WinRT门禁通过；四Host用例两轮8次PASS，随后7用例两轮14次全PASS（`preflight-all-repeat/suite.json`），原失败不改写。
- 本轮最终控制NOT_RUN语义后再跑四Host用例两轮8次PASS（`final-preflight-host-repeat/suite.json`）；真实前置报告 `control-preflight-blocked-2.json` 为整体FAIL、控制NOT_RUN、配置未写、restore NOT_REQUIRED。最终docs严格局部门禁79PASS/2工作区WARN/0FAIL，未运行构建或全量quality。安全汇总 `bond-followup-summary-safe.json` 校验维护前后八组与身份保持，并复算60秒样本231–294W、85.99–95.03RPM、163–170bpm（61/61帧）。
- 真实未配对前置拦截没有改配置，不属于控制/ERG测试PASS。实体HTTP桥接→模拟、Safari/iOS、有效ATT订阅证据、最终clean镜像/严格quality/CI/升级和发布附件门禁仍未补齐；当前无commit/push/tag/新Release。

## 本轮续测：主广播携带 HRS UUID 对照实验（诊断镜像）

- 出厂默认复核并加固：领域默认 `BROADCAST_MASK_DEFAULT=15`（功率/踏频/心率全开，踏频随 CPS 曲柄字段）；无用户心率时由 `linked_heartrate()` 按功率经 FTP 区间平滑推导（心率随功率）。`power_bridge_config_defaults()` 显式写 `BRIDGE_SOURCE_SIMULATED`，`test_config_store()` 新增“出厂默认模拟来源且 device_count=0”断言。WSL Ubuntu-20.04 host 单测 **6247 通过 / 0 失败**（原 6246 + 新增 1 条默认 source 断言）。
- 板端静态核对：GATT 骑行表 CPS 0x1818 / HRS 0x180D / CSC 0x1816 / FTMS 0x1826 / DIS；HRS 测量 0x2A37 仅 `BLE_GATT_CHR_F_NOTIFY`，无 WRITE_ENC 加密绑定，主服务可见。FTMS 自行车帧 0x2AD2 flags 仅含踏频/功率，**不含心率**；骑行模式下心率只能经独立 HRS 0x2A37 上报。
- 诊断镜像在 SUBSCRIBE 日志把 attr_handle 映射到服务名，并补 CONNECT/DISCONNECT/加密变更分支。两轮 90–120 秒串口观察一致复现：骑鬥以 handle=1 单连接接入，仅订阅 FTMS-Bike(32) / FTMS-Status(38) / FTMS-CP(35) 及未映射的 handle=8(indication)；**HRS(24) 订阅 0 次，CPS(16)/CSC 未订阅**；`enc_change=0`（未加密）。功率/踏频数值来自 FTMS-Bike 帧。
- 对照实验改动（诊断镜像，尚未判定固化生产）：心率开启时把 180D 从扫描响应一并列入主广播 UUID 列表，主广播不再放短名以满足 31 字节预算，完整设备名仍由扫描响应携带；`adv_uuids` 容量 3→4。编译 0 error，启动无 panic、无“设置广播字段失败 rc=”超预算报错。
- 空中扫描：对照镜像 180D 出现两次（主 adv + scan response 各一），并含 1818/1826，FTMS service_data `012000`，rssi -45，全名仍在 rsp。**此前 180D 仅在扫描响应时，骑鬥心率搜索列表为空；主广播加入 180D 后，心率搜索弹窗首次列出 BikePower-E2F（10:20:ba:cc:3a:ee）**——广播可见性这一环节固件侧可改善并已被实测确认。
- 连接阶段仍失败：点选该心率设备后，板端串口仍只有 handle=1 一条 FTMS 连接，无第二条 connect、无 HRS(24) 订阅、无加密变更；弹窗长期停在“正在搜索...心率计”，无心率数值。多轮复现一致。
- 静态门禁 `check_ble_advertising_fields.py` 原正则硬编码 `adv_uuids[3]`，容量改 4 后解析块为空导致 7 项误报 FAIL；正则放宽为 `\[\d+\]` 后 11 项全部 PASS。该脚本兼容改动随 harness 一并保留待审。
- 分层判断（截至本轮）：
  1. 板端 HRS 通知链路正常（历史独立探针 21/21 帧、163bpm；GATT/编码本轮未改动）。
  2. 广播可见性是固件侧可修环节：180D 进主广播后心率设备可被搜索列出；是否固化到生产待三连同显验收后再定。
  3. 连接/订阅阶段阻塞在客户端：同一 MAC 已被 FTMS 骑行台/功率/踏频角色占用，骑鬥心率模块未在现有链路发现并订阅 HRS，也未建立第二条连接。对照证据：独立 bleak 探针在同一板端、同一未配对状态下单连接订阅 2A37 成功并收到 22 帧心率，因此可排除板端加密绑定/GATT/编码/未配对对 HRS 通知的影响；未配对 Failed=19 是另一条独立历史阻塞（影响加密 FTMS 控制/ERG），不影响未加密 HRS notify。倾向骑鬥“单地址单角色占用、心率模块不复用骑行链路”的客户端策略限制。纯心率单链路对照被“板端复位后骑鬥自动恢复 FTMS 连接”干扰，未取得干净样本；骑鬥内部 ATT 日志也未采集到，最终定性仍可由纯 HRS 镜像对照或客户端抓包进一步佐证。
- 本轮板端为含 DIAG 日志与广播实验改动的诊断镜像，非生产发行镜像；工作区 dirty，未 commit/push/tag/Release。释放骑鬥独占后的独立只读探针已补跑：结束骑鬥进程后 `ble_multiservice_probe.py` 20 秒采样 **PASS**，mask=15、bridge_source=0（模拟），CPS 22 帧（功率 228–299W、曲柄计数递增）、HRS 22 帧（心率 176–178bpm，随功率升段升到 178、回落段降到 177，符合 FTP 区间平滑推导），`config_unchanged=true`、未写配置/未复位、链路正常释放；HRS 180D/2A37 仅 notify、无加密绑定属性。
- 本轮离线/host 回归：WSL Ubuntu-20.04 `test_domain` 6247 通过 / 0 失败；`test_portal_config_save` PASS；`test_core_flow_endurance --skip-host` 6 轮 PASS（host gate 由 WSL test_domain 覆盖；本机无原生 make/gcc，耐久内 `make -C test run` 前置在缺 make 环境记环境前置，不记代码 FAIL）；`check_config_consistency` 299/0，`check_board_desc/nvs_schema/tasks_wdt/portal_structure/portal_js/ble_advertising_fields` 全部 PASS。板端诊断镜像 `idf.py build` 0 error、ROM 启动无致命异常、无广播字段超预算报错。

## 后续顺序

1. 完成正常系统配对后复跑五轮控制/ERG；不在轮间清绑定。
2. 在同一有效配置窗口测试心率优先与功率优先顺序，取得骑鬥日志/对应ATT订阅证据，再做最小修复或明确第三方边界；三项同时显示需至少两次独立启动及持续窗口。
3. 通过正式热点HTTP保存验证当前WiFi来源切换补丁，并补手机WebView/Safari聚焦。
4. 上述通过后再按功能分批commit并push；新版本、clean构建、精确镜像烧录、P0/CI及附件门禁全部绑定最后提交，才创建新Release，不覆盖v4.4.0。
