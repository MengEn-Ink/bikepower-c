# BLE 领域规则（BikePower C）

<!-- stability: tier-2 (稳定，随 BLE 设计调整) -->
<!-- rule-id-prefix: BLE -->
<!-- applies-to: main/ble_bikepower.c, main/app_main.c, tests/ble_selftest.py -->

> 本文件定义 BikePower C 的 BLE 实现边界。通用编码和硬件约束仍以 `coding-style.md`、`hardware-constraints.md`、`wifi-ble-mutex.md` 为准。

## 角色与范围

- `BLE-1`：BikePower C 只实现 NimBLE `peripheral` 模式。
- `BLE-2`：禁止实现扫描、中心角色、后台扫描协程或与业务无关的 GAP 搜索逻辑。
- `BLE-3`：BLE 是设备主工作模式；仅在进入 WiFi 配网时由 `radio_ctrl` 停止 BLE。

## 服务清单

- `BLE-4`：GATT 必须维持五服务组合，不得任意删改。
- `CPS`：Cycling Power Service，UUID `0x1818`
- `HRS`：Heart Rate Service，UUID `0x180D`
- `CSC`：Cycling Speed and Cadence Service，UUID `0x1816`
- `FTMS`：Fitness Machine Service，UUID `0x1826`
- `DIS`：Device Information Service，UUID `0x180A`

## 广播与连接

- `BLE-5`：广播模式使用 `BLE_GAP_CONN_MODE_UND` + `BLE_GAP_DISC_MODE_GEN`。
- `BLE-6`：当前实现是单连接外设模型；连接断开后必须自动恢复广播。
- `BLE-7`：广播字段保持精简，优先放对外发现最关键的 16-bit UUID，不把完整服务能力塞进广播包。

## 通知节拍

- `BLE-8`：通知节拍固定为 `BLE_NOTIFY_INTERVAL_MS = 1000ms`。
- `BLE-9`：通知数据由主循环更新共享快照，定时器负责按节拍发出，禁止在主循环中直接拼包并同步发通知。
- `BLE-10`：新增通知或改变节拍时，必须同步更新 `tests/ble_selftest.py` 与技术文档。

## GATT 句柄缓存

- `BLE-11`：GATT 特征值句柄必须在服务注册后缓存到静态全局变量，避免运行时重复查找。
- `BLE-12`：至少维护以下句柄缓存：
- `g_cps_val_handle`
- `g_hr_val_handle`
- `g_csc_val_handle`
- `g_ftms_bike_handle`
- `g_ftms_cp_handle`
- `g_ftms_status_handle`
- `BLE-13`：通知发送路径必须基于句柄缓存，不得在高频路径重新解析 attribute table。

## 数据包格式

- `BLE-14`：所有打包按蓝牙规范使用小端字节序。
- `BLE-15`：CPS 数据包格式固定为 `flags + instantaneous_power + cumulative_wheel_revs + last_wheel_event_time`。
- `flags`：2 字节
- `power`：2 字节，有符号功率值，单位 W
- `rev`：4 字节，累计轮转数
- `time`：2 字节，最后轮事件时间
- `BLE-16`：HRS 数据包格式固定为 `flags + bpm`。
- `flags`：1 字节
- `bpm`：1 字节
- `BLE-17`：CSC 数据包格式固定为 `flags + cumulative_wheel_revs + last_wheel_event_time`。
- `flags`：1 字节
- `rev`：4 字节
- `time`：2 字节
- `BLE-18`：FTMS Indoor Bike Data 数据包格式固定为 `speed + cadence + power`。
- `speed`：2 字节
- `cadence`：2 字节
- `power`：2 字节
- `BLE-19`：字段增减、flags 位变化或单位变化都视为协议兼容性变更，必须同步更新自测脚本与外部文档。

## FTMS 控制点

- `BLE-20`：FTMS 控制点仅支持以下 OpCode：
- `RequestControl`
- `SetTargetPower`
- `StartResume`
- `StopPause`
- `Reset`
- `BLE-21`：未支持的 OpCode 必须返回 `Response Code + Not Supported`，禁止静默吞掉。
- `BLE-22`：`SetTargetPower` 只负责解析目标功率并交给应用层回调，禁止在控制点回调内直接改动复杂状态机。
- `BLE-23`：`StartResume`、`StopPause`、`Reset` 只更新 FTMS 状态缓冲区和响应，不引入阻塞行为。

## 回调与中断安全

- `BLE-24`：GAP 回调、GATT 回调、通知定时器回调、中断回调中禁止阻塞。
- `BLE-25`：这些回调中禁止堆上大块重分配，通知缓冲区必须静态预分配。
- `BLE-26`：这些回调中禁止做复杂串行化逻辑，必要状态变更应投递到主循环或状态机处理。
- `BLE-27`：ISR 中禁止调用 NimBLE API、日志 API、NVS API 和任何非 `FromISR` 接口。

## 验证要求

- `BLE-28`：任何 BLE 服务、特征、通知格式、控制点行为改动，都要更新 `tests/ble_selftest.py`。
- `BLE-29`：若改动涉及连接状态或广播恢复行为，应同时复核 `app_state.c` 的连接事件处理与 LED 表现。
