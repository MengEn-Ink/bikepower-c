# BINAVI 连接问题结项：根因、修复与实测结论

更新日期：2026-11-07
结项版本：v4.3.2 预发布（tag `v4.3.2` → `5fed5c3`）
相关记录：[连接状态修复过程](44-binavi-connection-fix.md)、[发版记录](../../tests/reports/binavi-release-prep-20261107/v4.3.2-release.md)

## 结论

BINAVI 码表已能连接本板并显示功率与踏频，踏频稳定连续。心率一项经用户确认不再追究，按码表侧能力差异结项，不作为固件缺陷。本结论仅覆盖连接与功率/踏频数据面；m5stamp-c3 实物、FTMS 配对后 ERG、按钮与 LED 等人工项仍未验收。

## 一、现象

码表能发现本板并以功率角色建链（MTU 64），系统日志完成服务发现，但约 15 秒后由板端 `data_timeout` 主动断开；恢复连接后功率、踏频、心率三路均无数值。同一时间用电脑客户端连接同一固件，三路通知均正常。因此问题位于连接与订阅状态管理，而非数据编码。

## 二、根因

四处状态判定各自独立失效，叠在一起造成"能连上但不持续、看着连上却没订阅"：

| 编号 | 根因 | 后果 |
| --- | --- | --- |
| R1 | 板端不接受码表上报的 status 26 连接状态 | 活跃连接被判为"未连接"，后续判断全部基于错误前提 |
| R2 | 订阅登记发生在 Host 真实连接之前，早到的 SUBSCRIBE 被丢弃 | 码表已订阅，板端仍认为无订阅，通知不发 |
| R3 | 同句柄重复确认时清掉已有订阅登记 | 二次确认把刚登记的订阅清空，形成"确认一次就丢一次" |
| R4 | 异句柄的 DISCONNECT 清掉活跃连接 | 其它链路的断开串扰当前连接，连接被误判释放 |

## 三、修复

| 编号 | 修复 | 位置 |
| --- | --- | --- |
| F1 | 接受 status 26，仅按真实 Host 连接校验后才登记订阅 | `main/ble_bikepower.c` |
| F2 | 早到的 SUBSCRIBE 记入待确认，真实开启 Notify 时补确认 | `main/ble_bikepower.c` |
| F3 | 同句柄重复确认改为幂等，不清已有订阅 | `main/ble_bikepower.c` |
| F4 | 异句柄 DISCONNECT 不再清活跃连接，按句柄隔离 | `main/ble_bikepower.c` |
| F5 | 新增"踏频随功率"兼容位 bit3（默认关闭），不开 CSC 也能用 CPS 曲柄字段承载踏频 | `domain_config.h`、`config_store.c`、`power_engine.c` |
| F6 | 配置页把判断表达式当字符串导致恒真，mask7 设备打开即勾选 bit3 并会在保存时静默写入 | `tools/web/ble-monitor.html`、`main/web/app.js` |
| F7 | 手机演示内嵌的生产 JS 副本未随源码更新，CI 断言"屏幕必须使用当前生产 JS"失败 | `tools/web/phone-portal-source.js`（由 `build_phone_demo.py` 重新生成） |

F6 是最隐蔽的一处：`configCheck` 收到的是非空字符串，恒为真，页面一打开就置脏；用户点保存会把 bit3 一起写进 NVS，等于在没有提示的情况下改变广播语义。改为布尔表达式 `!!(c.broadcast_mask&8)` 后 light/dark 双主题回归全部通过。

## 四、验证

| 项 | 结果 |
| --- | --- |
| 双板构建 | 0 error，merged 各 1204368 字节，app 各 1138832 字节 |
| 烧录 | generic-c3 从 0x0 写入，`Hash of data verified`；刷前应用区已备份 |
| 配置端到端 | 八组读写、错误路径、NVS 重启持久化、GATT 重建、掩码断连重广播、原配置恢复全 PASS |
| 连接状态机 | 两次独立扫描 + 两次连接/断开 PASS（首次复跑有一次第 2 连接超时，重跑通过） |
| 5 分钟耐久 | 300.3 秒在线，CPS/HR/CSC 各 301 帧，MTU 64，末尾仍连接 |
| CI | push 与 release 触发的两次部署均 success；首跑因 F7 失败后修复转绿 |
| 产物门禁 | 10 个上传附件、双板、zip 内 16 条目与清单一致；上传后回读哈希一致 |
| 码表实测 | 可连接，功率与踏频出现且踏频稳定连续（用户确认） |
| 板端数据面 | 45 秒采样 CPS/CSC/HRS 各 34 帧：功率 278 W、曲柄累计 1064、心率 161 bpm |

## 五、关于心率

板端 HRS 侧采样正常（161 bpm、34 帧、无中断），广播掩码 `07` 含心率位。码表侧心率栏为空，说明码表没有取这一路——码表通常只订阅功率与速度踏频服务，心率走独立心率带。CPS 协议本身没有心率字段，无法把心率塞进码表已订阅的功率通道。

用户已确认此项不再追究，故不列为固件缺陷，也不再追加兼容方案。

## 六、当前状态与保留项

- 默认配置（mask `07`）即满足"功率里带踏频"：CPS 自身携带曲柄累计与事件时间，码表功率页同时给出踏频。兼容位 bit3 无需启用，保留作为兜底。
- m5stamp-c3 无实物，只有构建与合并通过，没有该板 P0 证据。
- FTMS 控制点未配对时按 ATT `0x05 Insufficient Authentication` 拒绝，ERG 收敛未验证，未放宽认证。
- 兼容位 mask15 窗口内 300 秒 + 复连 60 秒未执行。
- 实体按钮、LED、手机 Captive Portal、Zwift/跑步 App、配网退出连续性未执行。

## 七、复跑

```powershell
# 板端三路数据面采样（45 秒）
python build\release-4.3.2-work\read_board_data.py

# 配置端到端
python tests\ble_config_e2e.py --port COM205 --board generic-c3 --address 10:20:BA:CC:3A:EE --reset-method rom

# 连接与重连
python tests\stage3_conn_test.py --no-serial --address 10:20:BA:CC:3A:EE --hold-sec 8

# 发布产物门禁
python scripts\tools\check_release_artifacts.py --release-dir build\release-4.3.2 --version 4.3.2 --git-sha 5fed5c3 --require-pushed --allow-dirty
```

发布页：<https://github.com/MengEn-Ink/bikepower-c/releases/tag/v4.3.2>
