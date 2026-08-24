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
- `python3 scripts/tools/quality_gate.py`：72 通过，0 失败；工作区未提交产生 2 个预期告警。
- `idf.py build`：通过，固件大小 `0x116260`，app 分区剩余 72%。
- 配置一致性：262 通过，0 失败。
- 门户结构：242 通过，0 失败。
- Portal 响应缓冲复用后，`libmain.a` `.bss` 从 27863 字节降至 12594 字节。

## 当前待验收

当前分支未检测到 ESP32-C3 串口，以下项目保持 `NOT_RUN`，不得复用旧 Git ID 的证据：

- generic-c3 与 m5stamp-c3 各自烧录、启动日志与空闲堆。
- 骑行 BLE 服务、CPS/CSC Feature、ERG 收敛与 Stop 恢复。
- 跑步 RSC/FTMS、Stop 冻结、Start 恢复、Reset。
- 两次独立扫描、连接、断开与重新广播。
- 配网门户、保存退出、BLE 连续性和 profile 重建提示。
- 物理按钮、LED、手机 Captive Portal。
- 72 小时老化。

## 发布阻断

- 正式发布前必须分别生成并严格校验：
  - `tests/reports/device-acceptance-generic-c3.json`
  - `tests/reports/device-acceptance-m5stamp-c3.json`
- 所有 P0 项必须为 `PASS`；`FAIL` 或 `NOT_RUN` 均阻断发布。
- 发布必须从已提交且已推送的源码构建，版本号、Git ID、tag 与 Release 一致。

## 下一步

1. 连接 generic-c3，执行 `tests/operation_gate.py --level device`。
2. 切换 running profile，执行带 `--control` 的跑步 BLE 自测。
3. 连接 m5stamp-c3 重复 P0 验收。
4. 补人工按钮、LED、Captive Portal 和老化证据。
