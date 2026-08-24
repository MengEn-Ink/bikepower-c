# BikePower C 当前状态

> 当前固件版本：v4.1.1
> 技术栈：ESP-IDF v5.5.4 + NimBLE + FreeRTOS，ESP32-C3
> 历史变更统一查阅 [CHANGELOG.md](CHANGELOG.md)。

## 当前能力

- 支持 generic-c3 与 m5stamp-c3 两种板型。
- 骑行 profile 提供 CPS、HRS、CSC、FTMS、DIS。
- 跑步 profile 提供 RSC、HRS、FTMS、DIS。
- FTMS 支持骑行 ERG 与跑步目标速度、开始、暂停、恢复、重置。
- 配置保存到 NVS，当前 `CONFIG_VERSION = 11`。
- WiFi 仅用于用户主动触发的 SoftAP 配网；默认与 BLE 共存，退出配网不重启。
- 配网页前端三件套 `main/web/` 由 `scripts/inline_static.py` 编译时内联。
- 已移除设备内 OTA 与家庭 WiFi；当前固件升级走电脑端网页烧录，手机端不支持烧录。
- Release 产物支持双板型 merged bin、SHA256、manifest、测试报告、用户手册与证据包。

## 最近软件验证

- `make -C test run`：5866 通过，0 失败。
- `python3 tests/test_ble_protocol_vectors.py`：10 个固定协议向量双路径通过。
- `python3 tests/operation_gate.py --level full`：通过，包含 6 轮核心流程耐久。
- `python3 scripts/tools/quality_gate.py`：82 通过，0 告警，0 失败；新增门禁全部接入后的最终结果见提交说明。
- `idf.py build`：通过，固件大小 `0x116250`，app 分区剩余 72%。
- 配置一致性：252 通过，0 失败。
- 门户结构：242 通过，0 失败。
- Portal 响应缓冲复用后，`libmain.a` `.bss` 从 27863 字节降至 12594 字节。

## 最近真机验证

- generic-c3 已烧录当前分支固件。
- 串口启动日志通过，未发现 panic、WDT、Backtrace 或真实 coredump。
- 骑行 BLE 服务、CPS/CSC Feature、Sensor Location、通知、ERG 收敛与 Stop 恢复通过。
- 两次独立扫描、连接、断开与重新广播通过。
- 隔离的 running 测试固件已通过 RSC/FTMS、12km/h 目标速度、Stop 冻结、Start 恢复和 Reset。
- ccache 跨两个全新隔离目录实测：第二轮命中 1048 次。

## 当前待验收

以下项目保持 `NOT_RUN`，不得复用其他 Git ID 或板型的证据：

- m5stamp-c3 烧录、启动日志、骑行/跑步 BLE 与重连。
- 配网门户 HTTP、广播项、保存退出和 profile 重建提示；当前测试机的企业网络扩展把 `192.168.4.1` 路由到 `utun4`，无法访问设备 AP。
- 物理按钮、LED、手机 Captive Portal。
- 72 小时老化。

## 发布阻断

- 正式发布前必须分别生成并严格校验：
  - `tests/reports/device-acceptance-generic-c3.json`
  - `tests/reports/device-acceptance-m5stamp-c3.json`
- 所有 P0 项必须为 `PASS`；`FAIL` 或 `NOT_RUN` 均阻断发布。
- 发布必须从已提交且已推送的源码构建，版本号、Git ID、tag 与 Release 一致。

## 下一步

1. 在不拦截 `192.168.4.0/24` 的网络环境验证 Portal、广播项、保存退出和 BLE 连续性。
2. 连接 m5stamp-c3 重复 P0 验收。
3. 补人工按钮、LED、手机 Captive Portal 和老化证据。
