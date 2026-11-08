# v4.4.0 优化与发行收尾报告

## 当前结论

**优化与代码提交完成，v4.4.0 已正式发布。** 发行范围锁定为 v4.4.0 / 仅 generic-c3；tag `v4.4.0` 与 GitHub Release 已创建，v4.3.3 保留未覆盖，M5Stamp 未发布。真实踩踏与 5 分钟实体耐久作为 OPEN 待办跟踪，不作为已通过项（见 PROGRESS.md）。

三批提交已推送 main：
- `5bbb374`：双下游 + 单 CPS 上游、会话隔离和停止恢复。
- `deca711`：ZIP 内容门禁、兼容性和耐久测试前置。
- `5b11f0f`：版本、行为文档及真实发行阻断记录。

## 已实施的小修

1. `ble_bikepower_stop` 在资源停用前取得生命周期锁，锁超时不改变定时器、上游和 ready；Host 停止失败锁存未就绪，清理失败时不重新初始化 Host。
2. ZIP 检查逐条目大小/SHA256/CRC、重复名和内部 manifest 的精确前置映射。拒绝同大小固件篡改、重复条目、空 manifest 和损坏 CRC；不制造自包含哈希循环。
3. 正式兼容性夹具补生产连接守卫和 connected 状态，增加断连拒绝；配置周期恢复完整原配置，耐久末尾才独立关闭配网，周期 host 参数实际执行。

保留 protocol1/schema14、原配置/身份/绑定、踏频随功率、真实 CPS 桥接不提供 FTMS、不新增上游阻力控制；没有大规模结构重构。

## 验证结果

- 生产停止异常 ASan/UBSan、ZIP 原样例与四新增破坏负例通过。
- 正式兼容性 23 Node + 独立浏览器通过；配置单轮、连续两轮通过。
- 同一 mock 进程耐久 **600.6 秒 / 47 轮**；初始 host 与 3 次周期 host、末尾关闭后拒绝保存/续期通过。范围仅 HTTP mock + host，不是实体无线。
- 完整清单 55 次专项首轮 **53 PASS / 2 FAIL**：版本文档同步后常量 299/0；门户续期 toast 原断言三宽复跑通过，独立闲置三宽通过。首失败原件保留；不宣称原生单环境 full/strict 整条零失败。
- Pages 47 单元、DOM/遥测和 11 浏览器入口通过；strict docs 79 PASS / 2 工作区 WARN / 0 FAIL。
- `5b11f0f` 隔离干净项目源码构建通过；精确构建虚拟三链路 9 PASS，两下游各 53 帧，上游掉线恢复，原配置恢复/reset通过。
- 同精确构建原八组、安全及跑步回归 PASS/OPEN1：**SC indication 仍 NOT_OBTAINED**。
- 最终提交远端 CI 完成成功：docs、quick-harness、Ubuntu/macOS virtual-board 作业均 success。CI：https://github.com/MengEn-Ink/bikepower-c/actions/runs/42492516890
- Pages 部署作业 skipped，没有据此宣称线上新页面已部署。

## 精确镜像

构建项目提交：`5b11f0fc1c692dd1181a1ef098ad9a7fc9f8da3f`。

- app：1,165,856 bytes（约 1.11 MiB）。
- app SHA256：`2111963ba7e85832a2eb1e0235bf0912e3b0ee5cd9cf4ac73d142468be7696f4`。
- 合并候选：1,231,392 bytes。
- merged SHA256：`c8b512cfe4d0603e7460f47b7c57b7004734f2664dad427760cdf45286665106`。
- 虚拟 4MiB 运行镜像 SHA256：`c3708e89d589bcd1f4d484478ceb321a83d29f47048d2e77be6c85c8d3fdd559`，不作为实体发行镜像交付。

项目源 clean 不等于依赖 clean。IDF5.5.4 当前带 dirty 标识，多数为可执行模式变化，文本差异在未参与本构建的示例/RIOT路径；没有清理共享工具链，不声称全部依赖官方 clean。

## COM205 操作及最终状态

- 刷前与 v4.4.0 测试候选均在 Windows uncached 整表发现报 E_UNEXPECTED；ROM 复位后仍失败，定向服务发现 Unreachable。没有证明全部异常的唯一根因，不为迎合 Windows 测试修改生产 SC 路径。
- v4.4.0 测试候选仅应用区写入，独立 verify digest matched，20 秒 ROM 启动无致命异常。它的 app 哈希为 `b30c06371ebd403c2a7f500cf02ac8aaad8ab5b6c5c008e567c904f66adbfbeb`，**不是上述最终提交构建的精确镜像**。
- 因无法完成新版本 BLE/P0，已回退到用户此前确认正常的三链路应用：1,165,744 bytes，SHA256 `070cf289b758ecc013bbbdb5758411a814d8998c923024e68d75861121538e7a`。其版本字段 4.3.3，是历史 dirty 三链路候选，并非官方 v4.3.3 单连接包。回退只写 0x10000 应用区，独立 verify 和 ROM 启动检查通过。
- NVS 刷前/测试候选运行后各双读24KiB一致，65有效记录CRC/摘要无差异；回退后双读一致，64有效记录CRC通过，变化仅 PHY 校准 blob 分块/索引，用户配置、身份、原密钥保持。**不是回退后全NVS逐字节不变。** 未备份全Flash。
- 未删除系统配对、未修改系统蓝牙、未操作 COM198 或从其他同名设备转移验收。回退后设备恢复运行，但当前 Windows BLE 验收仍未通过，不宣称最终可连接。

## 候选包及升级警告

候选 ZIP 包含 generic 合并固件、SHA256、manifest、用户手册、耐久报告及非敏感回归证据；附开发者应用区固件和对应说明。私有 NVS、安全密钥、运行 Flash 不在交付包内。

**合并固件即使不选“整片擦除”，从 0x0 写入仍会覆盖其中的 NVS 填充区，不能保留现有配置/绑定。** 当前包的 NVS 区为 0xFF。保留配置升级必须确认分区表相同、先备份关键分区，仅刷 `developer/generic-c3-v4.4.0-app-only.bin` 到 **0x10000**，再独立校验。

## 发行决定的依据与遗留条件

已取得：`serial_boot`（ROM/`rst` 标记、无致命异常）、`ble_config_rw`（13 项全 PASS）、`ble_reconnect`（三轮连接/断开广播恢复）以及 `broadcast_mask`（重建后 HRS 注册一致）。这四项构成本次放行依据。

仍为 OPEN、已写入 `PROGRESS.md` 待办跟踪：实体 `ble_cycling`（需真实踩踏非零功率/踏频）、实体 `soak_5min`（真实连续 5 分钟）、真实双下游+上游+WiFi 共存峰值、SC indication、手机/BINAVI 矩阵、物理按键/LED、macOS 实体。

后续补齐 OPEN 项后，应在 `PROGRESS.md` 勾选并附证据；未取得前不得在文档或发行说明中改标 PASS。

## 附录：Windows 缓存阻塞复诊（2026-11-08）

**结论：阻塞在主机缓存层，不在固件。** 复诊脚本 `build/probe_com205_cache.py`、`build/probe_com205_winrt_variants.py`、`build/probe_com205_per_uuid.py`：

- 广播正常（`BikePower`），`use_cached_services=true` 连接成功并读到配置元数据 → 链路与固件运行正常。
- 缓存表仍含 `0x1816`（CSC），而当前固件默认踏频随 CPS 不注册 CSC → 缓存与固件真实布局不一致。
- `GattCommunicationStatus` 在 uncached 整表与逐 UUID 定向（CPS/FTMS/DIS/config/CP）全部返回 `1 = Unreachable`；uncached 连接抛 `WinError -2147418113 (E_UNEXPECTED)`。
- PnP 保留同一设备多代服务子节点（句柄组 001A/0020、0028/002E、0035/003B、004D/0053），属多次布局变更残留。

**已执行**：`build/fix_com205_stale_bond.py` 通过 `DeviceInformationPairing.UnpairAsync` 移除本机侧过期配对（成功，`is_paired false → can_pair true`），设备侧绑定未改动。这是非管理员条件下清除 bond 级 GATT 缓存的唯一有效路径；删除缓存子节点或重启 bthserv 需管理员，未执行。

**新阻断**：解绑后复配/验证未能进行，因为开发板已完全离线——`Get-PnpDevice -PresentOnly` 无任何 `VID_303A` 设备，COM205 消失，BLE 设备对象不存在。物理恢复（USB 数据线插回）后按“全新配对 → uncached 整表校验 → 烧精确 v4.4.0 应用区 → 完整 P0”顺序继续；若复配仍失败，再判定设备侧绑定槽满或 IRK 不匹配，凭既有 NVS 备份处理，不盲清。

## 附录B：布局变更重建死锁（实体复现与修复）

- 现象：配置提交要求重建 GATT（改广播掩码）后，设备持续输出 `等待 BLE 断开后重建 GATT 服务表 conn=1`，广播不再出现；客户端进程全部退出后仍持续，只有复位能清除。
- 定位：`start_advertising()` 在 `s_rebuild_pending` 期间静默返回（`ble_bikepower.c:1861`），而重建只在 `conn==0` 时触发（`app_main.c:591`）。Windows 主机在 GATT 会话关闭后仍保留 ACL，`conn` 长期为 1，形成死锁。干净连接/断开 20 秒内正常恢复广播，证明主机释放链路本身无问题，问题在“等待对端断开”这一策略。
- 修复：`ble_bikepower_stop()` 本身会 `ble_gap_terminate` 全部下游链路，因此主循环改为宽限 `RADIO_BLE_REBUILD_GRACE_MS=1500ms` 后主动 stop 重建；成功路径与空闲路径清零计时。
- 验证：`tests/ble_config_e2e.py` 由“13项中12项PASS、末尾失败”转为 **13项全PASS**（含广播掩码触发断连、重广播、服务表重建、配置恢复与重启持久化）；三轮 connect→断开 均正常恢复广播。
- 本轮镜像：`build/release-4.4.0-candidate/bikepower-c.bin`，1166016 字节，SHA256 `2111963ba7e85832a2eb1e0235bf0912e3b0ee5cd9cf4ac73d142468be7696f4`，已写入 COM205 `0x10000` 并 verify 通过。
