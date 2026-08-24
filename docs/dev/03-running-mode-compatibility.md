# 跑步模式兼容性与当前实现

本文记录当前跑步模式的有效协议面、控制语义和兼容边界。历史调研与分阶段设想通过 `CHANGELOG.md` 和 Git 历史追溯。

> 当前固件基线：BikePower C v4.1.1，ESP-IDF v5.5.4 + NimBLE，ESP32-C3。

## 当前协议面

跑步 profile 只注册与跑步相关的服务，避免骑行 App 和跑步 App 互相误分类：

- Running Speed and Cadence Service，UUID `0x1814`
- Heart Rate Service，UUID `0x180D`
- Fitness Machine Service，UUID `0x1826`
- Device Information Service，UUID `0x180A`

主广播包含 RSC 与 FTMS，心率服务和完整设备名放在 scan response。连接后由 GATT 发现完整服务表。

## RSC Measurement

RSC 数据包固定为 10 字节：

| 偏移 | 长度 | 字段 |
|------|-----:|------|
| 0 | 1 | flags |
| 1 | 2 | speed，1/256 m/s |
| 3 | 1 | cadence，steps/min |
| 4 | 2 | stride length，cm |
| 6 | 4 | total distance，0.1 m |

运行时 flags 为 `0x07`，表示步幅、累计距离和 Running Status 有效。暂停时 flags 为 `0x03`，速度、步频和步幅归零，累计距离保持。

## FTMS Treadmill Data

当前发送速度、累计距离、心率和用时，flags 固定为 `0x0504`：

- `0x0004`：Total Distance Present
- `0x0100`：Heart Rate Present
- `0x0400`：Elapsed Time Present

payload 固定为 10 字节，速度单位是 `0.01 km/h`。RSC 的 `1/256 m/s` 与 FTMS 的 `0.01 km/h` 不能混用。

## 控制点语义

跑步 profile 支持：

- Request Control `0x00`
- Reset `0x01`
- Set Target Speed `0x02`
- Start/Resume `0x07`
- Stop/Pause `0x08`

控制点使用 indication 响应。命令只有在应用事件被可靠接收后才返回 Success；参数越界返回 Invalid Parameter，队列或应用层未接受返回 Operation Failed。

运行状态规则：

- profile 启动后默认处于运行态，兼容不发送 Start 的 App。
- Stop/Pause 后速度、步频和步幅归零，距离与用时冻结。
- Start/Resume 后恢复当前配置输出并继续累计。
- Reset 清空距离与用时，保持 Reset 前的运行或暂停状态。

## 配置与持久化

持久配置包括：

- 固定速度或配速模板
- 配速浮动
- 步频与步频浮动
- 跑步模板、自定义名称与配速范围
- 心率锁定或联动

运行/暂停状态属于训练会话，不写入 NVS。坡度不是当前核心闭环的一部分，配置、NVS、Portal 和 FTMS Set Target Inclination 均已移除。

## Profile 切换

配网页保存骑行/跑步 profile 后：

- 未连接 BLE 时立即停止并重建 GATT 服务表。
- 已连接时不强制断开，页面显示等待提示；客户端断开后自动重建服务表。
- 不重启整机，不改变 WiFi/BLE 共存规则。

## 验证

无设备测试：

```bash
make -C test run
python3 tests/test_ble_protocol_vectors.py
python3 tests/check_config_consistency.py
```

真机测试：

```bash
python3 tests/ble_selftest.py --profile running --control 12
python3 tests/stage3_conn_test.py
```

跑步真机自测覆盖服务发现、标准 payload、目标速度、暂停冻结、恢复累计和 Reset。

## 兼容边界

- 当前设备是运动数据模拟器，不控制真实跑带。
- 不支持坡度控制。
- Zwift、MyWhoosh 等第三方 App 的分类与控制支持仍需按版本真机确认。
- BLE 是核心路径，不能为了跑步扩展引入常驻 WiFi 或绕过 `radio_ctrl`。
