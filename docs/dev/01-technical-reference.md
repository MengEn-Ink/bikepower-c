# BikePower C 技术参考

本文只描述当前实现。当前固件版本为 `v4.1.1`，技术栈为 ESP-IDF v5.5.4 + NimBLE + FreeRTOS，目标芯片为 ESP32-C3。

## 架构

```text
GPIO ISR -> button_task -> app_queue -> app_state
                                  -> power_engine/running_engine
                                  -> radio_ctrl -> wifi_portal
app_main -> snapshot -> ble_bikepower -> NimBLE notifications
NVS <-> nvs_store <-> config_store
```

- `main/`：设备生命周期、状态机、BLE、WiFi、GPIO、NVS 与网页门户。
- `components/bikepower_domain/`：纯 C 领域逻辑，可在 host 单测。
- `tests/`：静态门禁、mock、真机和发布证据。
- `test/`：领域层 host 测试。

BLE/WiFi 启停只能经过 `radio_ctrl`。NimBLE 回调只校验、打标或投递事件，停止和重建由 `app_main` 执行。

## 主循环

`app_main` 每 50ms：

1. 消费 `app_queue`。
2. 消费 WiFi 完成 pending latch。
3. 消费最后一条 ERG target/clear pending 事件。
4. 检查 BLE 就绪与重试。
5. 处理 profile 重建请求。
6. 推进状态机和领域快照。
7. 更新 BLE 通知数据。
8. 每 10s 记录堆水位并保存 dirty 配置。
9. 喂 Task WDT。

## BLE

### Profile

- Cycling：CPS、HRS、CSC、FTMS、DIS。
- Running：RSC、HRS、FTMS、DIS。
- 外设连接模型为单连接；设备模板扫描按用户操作临时使用 central/observer。

### 必需特征

- CPS Measurement `0x2A63`
- Cycling Power Feature `0x2A65`：`0x00000008`，支持 crank revolution data
- CSC Measurement `0x2A5B`
- CSC Feature `0x2A5C`：`0x0002`，支持 crank revolution data
- RSC Measurement `0x2A53`
- RSC Feature `0x2A54`：`0x0007`，支持步幅、累计距离和 Walking/Running Status
- Sensor Location `0x2A5D`：骑行 Left Crank `0x05`，跑步 Top of Shoe `0x01`
- FTMS Feature `0x2ACC`
- FTMS Control Point `0x2AD9`
- FTMS Indoor Bike Data `0x2AD2`
- FTMS Treadmill Data `0x2ACD`
- FTMS Status `0x2ADA`

### Payload

- CPS：8 字节，`flags(2) + signed power(2) + crank rev(2) + crank time(2)`。
- CSC crank-only：5 字节，`flags(1) + crank rev(2) + crank time(2)`。
- HRS：2 字节，`flags(1) + bpm(1)`。
- FTMS Bike：flags 必须与实际 speed/cadence/power 字段一致。
- FTMS Treadmill：10 字节，flags `0x0504`，包含 speed、distance、heart rate、elapsed time。
- RSC：10 字节；运行 flags `0x07`，暂停 flags `0x03`；Total Distance 分辨率为 0.1m。

动态 payload 统一由 `ble_payload_codec.c` 编码。`tests/fixtures/ble_protocol_vectors.json` 是人工维护的协议真值，C codec 与 Python parser 分别校验。

### FTMS 控制

- Request Control `0x00`
- Reset `0x01`
- Set Target Speed `0x02`
- Set Target Power `0x05`
- Start/Resume `0x07`
- Stop/Pause `0x08`

结果码：

- Success `0x01`
- Not Supported `0x02`
- Invalid Parameter `0x03`
- Operation Failed `0x04`

应用事件被可靠接收后才返回 Success。跑步默认运行；Stop 冻结距离与用时并输出零速度/步频；Start 恢复累计；Reset 清零但保持运行或暂停状态。

ERG 是连接会话态，不修改 `base_power`，不置 dirty，不写 NVS。BLE 断开或 Stop/Reset 后回到 steady 基准功率。

## WiFi 配网

- 用户长按 2s 后二次确认才进入配网。
- SoftAP 名称 `BikePower-XXXXXX`，密码 `12345678`，网关 `192.168.4.1`。
- 默认启用 `CONFIG_ESP_COEX_SW_COEXIST_ENABLE`，BLE 保持在线。
- 180s 超时；保存并关闭后有 5s 收尾窗口。
- `portal_done()` 使用可确认回调；队列满时 `app_main` 使用 pending latch，保证最终幂等清理。
- 前端三件套 `main/web/` 由 `scripts/inline_static.py` 编译时内联。
- profile 已保存但等待 BLE 断开重建服务时，`ble_restart_pending` 驱动页面提示。
- HTTP handler 复用单个 4KB 响应缓冲，大响应使用 chunked 发送。

## 配置

- NVS namespace：`bikepower`
- `CONFIG_VERSION = 11`
- 已知迁移链：v6、v7、v8、v9、v10 逐步迁移到 v11。
- 未知旧版本回退默认配置。
- 持久配置只允许 steady/road；读到历史 ERG 时规范化为 steady。
- 删除的跑步坡度字段只忽略旧输入，不恢复运行时结构。

## 稳定性

- 主循环与 LED 任务受 Task WDT 保护。
- 空闲堆低于 80KB 告警，低于 20KB 严重告警。
- 通知和协议缓冲静态预分配。
- `libmain.a` `.bss` 当前约 12.6KB，较响应缓冲优化前减少约 15KB。
- Coredump 写入独立 flash 分区。
- 发布默认日志级别为 WARN。

## 构建与验证

```bash
idf.py build
make -C test run
python3 tests/operation_gate.py --level full
python3 scripts/tools/quality_gate.py
```

真机：

```bash
python3 tests/serial_capture.py -p <port> -d 12 --check-boot
python3 tests/ble_selftest.py
python3 tests/ble_selftest.py --profile running --control 12
python3 tests/stage3_conn_test.py
python3 tests/stage5_portal_test.py
```

Release：

```bash
./scripts/build_all.sh
python3 scripts/tools/check_release_artifacts.py --require-pushed --require-device-evidence
python3 tests/test_release_artifacts.py
```

`build_all`、`flash_latest` 与 `quality_gate` 使用独立构建目录和 sdkconfig，不污染普通 `build/`。正式发布要求两个板型的当前版本、Git ID 与全部 P0 证据匹配；`NOT_RUN` 不算通过。
