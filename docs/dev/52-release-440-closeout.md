# v4.4.0 优化与发行收尾报告

## 当前结论

**优化与代码提交已完成，正式发版未完成。** 发行范围锁定为 v4.4.0 / 仅 generic-c3。当前精确构建没有完整实体 P0 证据，未创建 tag 或 Release；v4.3.3 未覆盖，M5Stamp 未发布。

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
- app SHA256：`7d078fea51f03a30e4f3688cf20dabade5048dd8d679d334eb6f05e0e542510a`。
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

**合并固件即使不选“整片擦除”，从 0x0 写入仍会覆盖其中的 NVS 填充区，不能保留现有配置/绑定。** 当前包的 NVS 区为 0xFF，不得将它作为“保留配置升级”直接刷入已有设备。保留配置的开发者候选验证必须确认分区表相同、先备份关键分区，仅刷应用文件到 **0x10000**，再独立校验；当前候选未获得正式发行验收，不建议普通用户升级。

## 未完成的发行条件

本包精确镜像实体 P0 `serial_boot / ble_config_rw / ble_cycling / ble_reconnect / soak_5min` 均未完成。一般性的用户正常反馈及旧镜像验收不能替代它。

真实两个独立下游 + CPS 上游 / WiFi峰值、SC直接ACK、目标手机/BINAVI完整矩阵、物理按键/LED、M5Stamp 均无新精确镜像完整证据。后续必须在可验证的实际客户端完成当前构建 P0，再生成严格标准发行包、tag及Release。不得把此候选ZIP改名后当正式包上传。
