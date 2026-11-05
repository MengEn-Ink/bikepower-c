# 核心门户与真实功率桥接测试报告

> 归档于 2026-11-05：以下结果只对应记录的历史镜像与时间，不证明当前候选通过真机验收；当前状态见 [PROGRESS](../../PROGRESS.md)。

日期：2026-11-04。版本：v4.2.1 候选，验证时工作区基线 Git `f498429`；代码后续提交不改变既有测试固件和证据的构建身份，不能与历史 v4.2.1 发布证据混用。固件须用户手动验证并明确批准后才能发布。

## 实现范围

- 核心门户仅保留明主题、高密度布局、居中按钮与遵守 reduced-motion 的轻动画；固定功率与机器人共用区域，自定义归入机器人。
- 配网默认 300 秒，只有显式续期重置；默认模拟，真实功率计经临时测试后展示校正与身份配置，保存成功后切换实际来源。
- 桥接真实功率和同源曲柄踏频，支持瓦数偏移、比例校正、0W 保护、两有效样本门槛、3 秒过期归零与限时重连。首桥心率默认关闭，可手动开启模拟心率。
- schema v13 与 BLE 第六配置组保留历史配置迁移；可复制有效 DIS 字段，名称支持保留、采用功率计或自定义。
- 固件名：`generic-c3-v4.2.1.bin`、`m5stamp-c3-v4.2.1.bin`；总包：`bikepower-c-v4.2.1.zip`。Git 与构建时间保留在 manifest 和设备详情。
- Pages 配置页补齐骑行来源、已绑定功率计、原值/偏移/比例校正。保留绑定，旧固件按五组降级；首次扫描绑定从设备门户完成。本轮只更新本地源码，线上未部署。

## 已完成的自动化验证

| 验证 | 结果 | 证据 |
|---|---|---|
| 领域层 host | PASS，6146 断言、0 失败，另含独立桥接 suite | `make -C test run` |
| 协议向量 | PASS，13 个人工固定向量 | `tests/test_ble_protocol_vectors.py` |
| 生产上游会话及扫描 | PASS，ASan/UBSan | `tests/test_bridge_session.py` |
| 配置一致性 | PASS，279 项、0 失败 | `tests/check_config_consistency.py` |
| BLE 配置客户端 | PASS，六组编解码、事务及广播布局离线回归 | `tests/test_ble_config_e2e.py`、`tests/check_ble_monitor.py` |
| Pages 配置同步 | PASS，浅暗主题真实 DOM 编辑、六组校正保存、绑定保留、未绑定禁用、旧固件五组保存及移动引导；无页面 JS 错误 | `node tests/test_pages_bridge_browser.cjs` |
| 固件命名兼容 | PASS，无前缀双板附件、SHA256、manifest、历史长名回滚兼容 | `tests/test_prepare_web_flash_site.py`、`tests/test_release_artifacts.py` |
| 压缩生产门户 | PASS，320/390/1440 视口；保存失败草稿保留、校正、0W、过期、心率、跑步、焦点、续期、关闭及身份复制 | `tests/portal_bridge_browser.py` |
| 资源预算 | PASS，72,098 bytes（70.4 KiB），比旧版 116,751 bytes 减少 38.2% | 编译内联门禁，限额 110 KiB |
| 质量门禁 | PASS，101 PASS / 2 WARN / 0 FAIL；WARN 为未提交工作区及由此跳过的历史提交 whitespace 检查 | `/tmp/bikepower-quality-final.log` |
| 完整操作门禁 | PASS，含 3 分钟核心流程耐久 17 轮 | `tests/reports/core-bridge-operation.md` |
| 双板型正式构建 | PASS，合并 bin、SHA256、manifest、手册与 zip；app 分区剩余 73% | `/tmp/bikepower-build-all-final.log` |

构建使用新的独立证据路径，未把旧 Git/旧实现的真机记录当作本轮通过证据。

## 真机验证

设备：generic-c3，串口 `/dev/cu.usbmodem2101`。最终镜像门禁：

| 验证 | 结果 |
|---|---|
| 正式镜像串口启动 | PASS，无 panic/WDT/Backtrace |
| 骑行五服务、四路通知与 ERG | PASS |
| 跑步四阶段控制与累计数据 | PASS，测试后完整原配置恢复 |
| 六组配置、错误事务、NVS 重启持久化、GATT 重建 | PASS，测试后完整原配置恢复 |
| 两轮独立扫描、连接与断开恢复 | PASS |
| 5 分钟扫描/连接/CPS 通知耐久 | PASS，302.2 秒、259 条 CPS 通知；扫描失败、连接失败、通知空轮次均 0 |

证据：`tests/reports/core-bridge-device-acceptance.json`，命令明细：`tests/reports/core-bridge-device.md`。

本轮可执行的六项设备自动门禁全部通过；HTTP、真实上游及人工项保持 NOT_RUN。正式固件已恢复为 cycling / 模拟来源和测试前完整持久配置，自动配网测试开关关闭。

首轮烧录暴露 `main` 栈保护故障：`config_store_validate` / `config_store_apply_to_engine`；反汇编确认 `app_main` 帧为 2592 bytes，配置事务被内联后叠加多份配置、引擎和身份副本。事务只在主任务串行执行，已将工作副本改为静态预分配并去掉冗余 requested 副本，保留配置成功后才切换运行态的语义。烧录脚本同时启用现有 `--check-boot`，避免串口 panic 后仍返回成功。

- 修复后测试固件启动 PASS；骑行五服务、四路通知、DIS 短名称、FTMS ERG 收敛与 Stop/Pause 恢复 PASS（`/tmp/bikepower-ble-coex.log`）。
- 配网测试：设备热点 `BikePower-F54D59` 可被系统 WiFi API 扫描到；networksetup 与 CoreWLAN 关联均返回 `-3900`，主机 IP 仍为原网络地址，`192.168.4.1` HTTP 连接超时。配网 HTTP 和上下游桥接共存均 **NOT_RUN**，不计为通过。
- HTTP 逐组合广播掩码监控、真实设备移动端门户与关闭回到 BLE 亦 **NOT_RUN**；BLE 配置事务里的掩码修改及 GATT 重建已通过，不能替代逐组合数据面验收。
- 修复后双板正式包已重建，两个 bin 均不含自动配网测试标记；generic 镜像按 bootloader/partition/app 三段写入，避开 `0x9000–0x10000` 的 NVS/PHY 区域。
- 前一候选 generic merged SHA256：`0308af1eb4a9b02c3a4a939b53378ac9ef11a10d3e2fdd5489ef619a2edac67c`。
- 前一候选 m5stamp merged SHA256：`f2551b5725039db00e5b19f89b2c4a1ae11cc2554d5bd21f02790167b9ac534b`。

## 无前缀名称与 Pages 补充验证

当前分发固件：

- `generic-c3-v4.2.1.bin`：`19e1d69dcfaa0f5c988514f1f664139429d6c9980dce6353c0b0d5ee751f60c9`。
- `m5stamp-c3-v4.2.1.bin`：`a4296c611b2aa5821f0ba2a15e3dba9724b205468a9df3f0cf0a2bc4b754bf8e`。

本次固件运行逻辑只调整 `board_firmware_name()` 的 DIS/门户显示格式；Pages 表单位于外置工具台。前述六项设备门禁记录对应前一候选镜像，本次不重复跑步、双次重连及五分钟耐久，按名称改动补充烧录、启动、骑行 BLE 和六组配置验证。

| 验证 | 结果 |
|---|---|
| 双板构建与合并 | PASS，两个 app 均剩余 73%；构建前 host、协议、门户交互及 180 秒/17 轮 mock 耐久通过 |
| 构建证据收录 | 拒绝旧证据：`bf32b11` 与当前 `f498429` 不匹配；编译已完成，候选报告和总包独立生成，不将该旧证据计为本次通过 |
| 名称/附件兼容 | PASS，11 项 Pages 生成回归、产物检查器功能测试；支持无前缀名称和两代历史前缀名称 |
| Pages 编辑和事务 | PASS，两主题、校正草稿写入、事务后未修改状态、恢复、模板保留绑定、未绑定禁用与旧固件五组保存 |
| Pages 可见性 | PASS，新增桥接提示文字对比度 ≥4.5:1，键盘焦点可见，390px 手机引导无横向溢出，页面 JS 错误 0 |
| Pages 本地候选 | PASS，`build/pages-candidate/` 包含更新后的配置页、双板同源固件、SHA256、0x0 manifest 和版本索引；线上未部署 |
| 质量门禁 | PASS，101 PASS / 2 WARN / 0 FAIL，提示为未提交工作区；`/tmp/bikepower-quality-short-name.log` |
| 当前 generic 烧录/启动 | PASS，从当前 merged 提取 bootloader/partition/app 写入，保留 NVS；`/tmp/bikepower-short-name-boot.log` 无致命异常 |
| 当前 generic DIS/骑行 | PASS，DIS=`generic-c3-v4.2.1`，五服务、四通知、FTMS ERG/Stop/Pause 与数据解码全部通过；`/tmp/bikepower-ble-short-name.log` |
| 当前 generic 配置 | PASS，六组读取/修改、错误事务、重启持久化及 GATT 重建；完整测试前配置已恢复；`/tmp/bikepower-nvs-short-name.log` |
| 候选产物完整性 | PASS，10 个标准附件、zip 内 31 个文件，双板 bin/SHA256、清单、报告、手册、截图和独立证据一致；未使用完整 P0 真机严格门禁 |

当前镜像独立证据：`tests/reports/short-name-device-acceptance.json`，仅本次实测三项为 PASS，其余 NOT_RUN；不把前一镜像六项记录移作本次完整 P0 验收。用户已授权代码提交与合并，候选固件仍未发布；真实上游/WiFi、M5 实物和人工覆盖限制继续适用。

## 实际覆盖限制

- 附近尚未发现独立真实功率计。macOS CPS 上游夹具可验证通知链路，但 macOS 保留 DIS 服务，不能用夹具证明真实功率计 DIS 兼容性。
- M5Stamp C3 目前只有编译验证；物理按钮、LED、Safari、Zwift/MyWhoosh 真实骑行 App 待人工验证。
- 配置与身份位于独立 NVS namespace；保存身份失败不切换配置，配置保存失败会尝试回滚身份。这不是跨 namespace 的断电全原子事务。
- 本轮产物为功能测试候选；未补齐真机项和人工项不得宣称正式发布验收完成。
