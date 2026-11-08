# BLE 三链路实施与验证

## 结论与范围

基线为 `92e2ffd`，本轮在其上实现 **最多两个下游 BLE 客户端 + 一台上游真实 CPS 功率计**。上游主备表仍可保存三台，但同时只接入一台输入设备。COM206 不在线，经用户明确授权改用 COM205 验证。

代码已实现，Windows ESP-IDF 构建、相关离线回归及实际 NimBLE 虚拟三链路通过；实体完整三链路和 WiFi 峰值仍未验，不能据此宣称所有目标终端兼容或正式发行验收完成。

保留既有配置及绑定、protocol1/schema14、踏频随功率策略、真实桥接不提供 FTMS、WiFi 手动长按及二次确认开启。没有修改功率模型、增加上游阻力控制、重设计服务表、清系统配对、升级版本、创建 PR、commit/push、tag 或 Release。工作区原有无关文件保持。

## 后续用户反馈与发版复核

用户后续确认桥接当前已正常；此为用户场景反馈，不虚构两台设备型号、持续时长或WiFi峰值。历史审查发现的stop/ZIP/测试前置问题已在v4.4.0发版收尾阶段实施小修；对应最新证据与未完成的实体P0见[发版前项目复核](51-pre-release-project-review.md)。下文保留最初三链路候选的精确镜像和验证边界，不迁移为最终发行结论。

## 实现

- NimBLE 总连接容量3，应用最多两个下游，保留一个上游名额；控制器活动容量6，绑定容量4，持久 CCCD 容量48（每绑定按12条预留）。绑定满时不自动删除旧记录。
- 两个静态客户端槽分别保存订阅、8条 indication FIFO、在途状态和连接代次。第一下游连入继续广播，第二台连入停止接纳新下游；断开只释放对应槽。Host 验证 peripheral/slave 角色，保留 status26 与早到订阅兼容。
- 同一轮运动快照只编码一次，再分别通知订阅者，功率随机值与曲柄累计不因客户端数量重复生成。遥测保持20字节协议，设备级统计共享，链路参数和订阅按客户端区分。
- 模拟 FTMS 全局单控制者、全局单操作 mailbox；非控制者独立收到拒绝，不覆盖原操作票据。操作实际应用后才确认。RSC 记录发起连接。真实 CPS 桥接不注册、广播或通知 FTMS。
- 配置全局单写事务，BEGIN/分组/COMMIT/CANCEL 绑定发起连接，其他客户端可读但不能写入/取消。非发起者断开不清其他事务；异步响应 token 对应有效连接代次，避免句柄重用串会话。事务忙时拒绝独立 DIS 写和 WiFi 提交。
- GATT 布局变化保存前，BLE 只允许发起者留下，WiFi 要求下游全断开；实际 NVS 写入前冻结新增下游，写失败解除冻结，成功走原等待断连重建路径。
- 通知与应用响应发送统一投递到 NimBLE Host 队列，数据待发送标记与响应调度分开，控制响应不会增加运动通知频率。发送前检查真实每连接 `indicate_val_handle`，有系统 Service Changed 在途时保留 FIFO 待后续推进；同步 busy 事件不误标整条连接失败。
- 为读取实际栈在途状态，`main/CMakeLists.txt` 显式加入固定 ESP-IDF5.5.4 的 NimBLE Host 私有头目录。这是明确的版本耦合；升级 IDF 时必须复核结构和发送路径，不修改共享 IDF 源码。

## 本轮发现及处理的问题

1. 第一轮虚拟测试的双下游五项通过，但桥接切换触发 stop/start 后，主循环继续在 Host 未 sync 时访问已释放身份上下文，发生 Load access fault，整体 FAIL。已在重建后重新检查 BLE ready，并在 Host reset 时清 ready；修正后实际栈三链路、恢复及 reset 回读通过。原失败报告保留于 `tests/reports/multi-client-virtual/session-1794138400813335965/`。
2. 实际 NimBLE 的 `ble_gatts_indicate_custom` 不是“另有 indication 在途必定返回 busy”的安全接口；栈在发送后断言在途句柄为空。只处理返回 busy 不足以覆盖系统 SC 竞争。已采用 Host 队列串行、发送前真实在途检查，并覆盖同步 busy 回调，未重新添加历史“CONNECT补发SC”补丁。
3. 虚拟非 owner 写入负例原来把任意 Exception 都当拒绝，可能误吞超时。已收紧到 ATT `WRITE_NOT_PERMITTED`，其他错误仍使测试失败。清理阶段逐连接保留错误，后续进程、transport 和报告收尾不被某一断连异常跳过。
4. 原完整虚拟回归需要 CSC，而最新默认 mask15 按设计不注册 CSC。测试显式使用 mask7 全服务 fixture，独立保存真正 original；非法值拒绝比较 fixture，收尾恢复 original 并 reset 回读。初 fixture 比较错误、首次编译缺头、测试宏重定义、串行改动后无用 stub 的 `-Werror` 失败均未放宽生产断言解决。
5. 一次安全回归在合并镜像生成前错误并行启动，因文件不存在失败；待合并完成后重跑通过，不把首次 exit 作为固件失败或后续 PASS 的替代。

## 已取得证据

### 最终候选身份

- 板型 generic-c3，版本仍4.3.3，内嵌 Git `92e2ffd`；本轮修改未提交，属于 dirty 候选，不是该提交的干净发行镜像。
- app：`build/multi-client-firmware/bikepower-c.bin`，1,165,744字节。
- app SHA256：`070cf289b758ecc013bbbdb5758411a814d8998c923024e68d75861121538e7a`。
- 虚拟4MB镜像：`build/multi-client-virtual-serialized.bin`，SHA256 `c116af5b5fa8dfbad41488f56e17e26461e8f3509f3bacc1e339eec7b4f2686b`，仅虚拟验证使用，不作为发行包。
- 构建：Windows ESP-IDF5.5.4，WARN、原4MB分区、不开机自动 WiFi；最终构建日志 `build/multi-client-build-host-serialized.log`。

### 离线及虚拟

- 相关13个离线入口全通过：领域6246/0，配置常量299/0，生产客户端槽、订阅、响应、控制 mailbox、配置、遥测、同轮快照、上游会话/扫描/主备、绑定安全和缓存专项。生产 C host 专项使用 ASan/UBSan。日志 `tests/reports/multi-client-offline/`。
- 新增系统 indication 占位不阻塞另一连接、同步 busy 不误失败、Host 调度生命周期守卫；遥测新增双连接独立链路参数、同序快照、发布中变更、定向断连及重连订阅清空回归。
- 最终实际 NimBLE 三链路 **9项 PASS**：双下游发现、同帧分发、FTMS控制权、配置owner、双在线布局变更拒绝且配置不变、单台断开重连、上游 CPS 两下游各53帧、两下游在线时上游掉线重连及251W转发、原配置恢复并reset回读。
- 三链路证据：`tests/reports/multi-client-virtual/session-1794142397934276424/report.json`。两个不同虚拟下游身份和一个虚拟 CPS 外设，运行实际 ROM/bootloader/app/NimBLE Host，而非只用应用层 mock。
- 最终原安全/跑步回归：`tests/reports/multi-client-original-e2e/session-y_zir6qd/report.md`。全八组、四路通知、错误事务、普通重连、NVS reset、SC Just Works双方LTK核对、GATT重建、绑定恢复加密、原配置恢复、跑步通知及reset恢复通过。虚拟 UART 刷写精确回读覆盖4KiB，不是全Flash回读。
- **Service Changed indication 为 NOT_OBTAINED，不是 PASS**。整体脚本 exit0/OPEN1 不改变该结论。虚拟 Privacy Mode/RSSI 命令不支持，日志存在恢复IRK配置失败；LTK通过不能外推为真实RPA解析、射频AES-CCM、系统缓存或WiFi共存。

### COM205

- 原NVS在刷写前使用ROM/no-stub独立双读，24,576字节一致、81条有效记录CRC通过；原分区表已读并与候选有效布局一致。旧应用回退镜像已核验。没有完整Flash备份，不宣称完整Flash已备份。
- 中间候选及最终候选均只写 `0x10000` 应用区；最终1,165,744字节独立 `verify_flash` 返回 digest matched。未写NVS、bootloader或partition，不清绑定、不改系统蓝牙设置。
- 中间候选启动20秒无致命异常；刷后八组和DIS身份等于刷前基线，CPS22帧，第一下游在线时仍可扫描到目标广播。
- 中间候选刷后NVS双读一致，受保护用户配置、身份和原密钥记录保持。原始NVS字节不完全相同，仅 `nimble_bond/cccd_sec_2` 的blob数据/索引更新，符合本轮订阅行为；不是“NVS逐字节未变”。
- 最终候选启动20秒无致命异常；单Windows下游60秒收到CPS60帧与内存遥测60帧，最低堆151,948字节（约148.4KiB），高于80KiB健康线。八组/Meta观察前后稳定且等于刷前基线，DIS身份保持，第一下游在线仍可发现广播。该内存观测只覆盖单下游、无真实上游、未开WiFi，不外推三链路峰值。
- 最终NVS两次独立ROM读取24,576字节一致，有效记录CRC通过，用户配置、身份和原密钥记录保持；差异仅为 `nimble_bond/cccd_sec_2` 与 `cccd_sec_5` 的blob数据/索引（CPS与内存遥测订阅）。原始NVS字节不完全相同，不声称全NVS未变。
- 最终Windows文档门禁79 PASS / 2 WARN / 0 FAIL；WARN为未提交工作区及最近提交空白检查边界，不替代完整strict/full。最后NVS读取后设备硬复位恢复运行，独立BLE复连10帧通过，八组/身份仍与刷前一致。

## 未验项与验收条件

- 实体两个独立下游 + 真实 CPS 功率计同时运行、上游占位恢复、第三下游拒绝、单台断连不影响另一台、目标手机/BINAVI兼容及长时耐久仍未验。当前只有一个可执行Windows BLE中心端；同一适配器的两个GATT对象不能算两个物理中心端。
- 三链路 + WiFi共存的最低堆、CPU、最大可分配块及通知连续性未实测；不得用单下游或旧堆水位证明80KiB健康线。
- Windows/手机跨GATT表缓存、实体RPA/加密、SC直接ACK、实体掉电、按键/灯效、macOS、整仓strict/full、完整Pages浏览器矩阵及远端CI未在本轮验证。
- 旧单连接测试入口已迁入生产多连接专项，但旧未执行字符串夹具仍有残留；未逐条证明所有旧失败status组合、分配失败、锁忙、失配队列断言完全等价迁移，不声称旧覆盖无损。
- 新清理保护已实现并随正常流程运行；尚未单独做所有断连/transport故障注入，保留为测试深度边界。

## 复验入口与交付

- 离线：`python scripts/tools/multi_client_regression.py`（需可执行host C工具链；Windows构建配合同机WSL）。
- 构建：`scripts/virtual_board.ps1 -Action build -BuildDir build/multi-client-firmware -IdfPath <IDF5.5.4> -IdfPython <IDF Python>`。
- 虚拟三链路：`tests/virtual_board_multi_client.py --emulator <esp-emu> --firmware <merged-bin> --elf <elf> --report-dir <reports> --seconds 60`。
- 只读真机：`tests/ble_multi_client_readonly.py` 需明确精确目标地址、DIS身份、输出和独立刷前基线，不按名称回退；`verify_multi_client_nvs.py` 做记录CRC/摘要比较，不输出payload。

源码原地保留并同步技术参考、用户手册、进度和CHANGELOG。本轮交付审查源码包及此报告，不交付私有NVS、密钥、runtime Flash、旧无关草稿或正式固件发行包；没有创建或更新PR。
