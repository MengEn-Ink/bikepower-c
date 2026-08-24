# BLE 领域规则（BikePower C）

<!-- stability: tier-2 (稳定，随 BLE 设计调整) -->
<!-- rule-id-prefix: BLE -->
<!-- applies-to: main/ble_bikepower.c, main/app_main.c, tests/ble_selftest.py -->

> 本文件定义 BikePower C 的 BLE 实现边界。通用编码和硬件约束仍以 `coding-style.md`、`hardware-constraints.md`、`wifi-ble-mutex.md` 为准。

## 角色与范围

- `BLE-1`：BikePower C 主工作角色是 NimBLE `peripheral`，对码表/骑行 App 暴露 CPS/HRS/CSC/FTMS/DIS 五服务。
- `BLE-2`：仅允许为“设备信息模板库”和后续功率桥接实现受控 `central/observer` 能力；扫描必须由用户在配网页主动触发，限时运行，不允许开机后台扫描或与业务无关的 GAP 搜索逻辑。
- `BLE-3`：BLE 是设备主工作模式；普通配网期间 peripheral 保持在线。外部设备读取使用临时 central 连接，完成或超时后必须主动断开。
- `BLE-3a`：设备信息模板扫描结果必须同时满足“有名称”和“服务命中 CPS/CSC/HRS/FTMS 任意一种”；仅 DIS/BAS 或未知服务的广播不得进入用户可选结果。

## 服务清单

- `BLE-4`：GATT 必须维持五服务组合，不得任意删改。
- `CPS`：Cycling Power Service，UUID `0x1818`
- `HRS`：Heart Rate Service，UUID `0x180D`
- `CSC`：Cycling Speed and Cadence Service，UUID `0x1816`
- `FTMS`：Fitness Machine Service，UUID `0x1826`
- `DIS`：Device Information Service，UUID `0x180A`
- `BLE-4a`：Cycling Power Feature、CSC Feature、RSC Feature 和各服务要求的 Sensor Location 必须随 Measurement 一起注册，不得只实现通知特征。
- `BLE-4b`：骑行 Sensor Location 使用 Left Crank `0x05`，跑步 Sensor Location 使用 Top of Shoe `0x01`，不得跨 profile 共用错误语义。

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
- `BLE-15`：CPS 数据包格式固定为 `flags + instantaneous_power + cumulative_crank_revolutions + last_crank_event_time`，总长 8 字节。
- `flags`：2 字节
- `power`：2 字节，有符号功率值，单位 W
- `rev`：2 字节，累计曲柄转数
- `time`：2 字节，最后曲柄事件时间，1/1024 秒
- `BLE-16`：HRS 数据包格式固定为 `flags + bpm`。
- `flags`：1 字节
- `bpm`：1 字节
- `BLE-17`：CSC 使用 crank-only 格式 `flags + cumulative_crank_revolutions + last_crank_event_time`，总长 5 字节。
- `flags`：1 字节
- `rev`：2 字节
- `time`：2 字节
- `BLE-18`：FTMS Indoor Bike Data 的 flags 必须与实际字段一致；全开时为 `0x0044`，仅踏频时为 `0x0004`，仅功率时为 `0x0041`。
- `speed`：2 字节
- `cadence`：2 字节
- `power`：2 字节
- `BLE-19`：FTMS Treadmill Data 当前 flags 固定为 `0x0504`，payload 为 speed + distance + heart rate + elapsed time，总长 10 字节。
- `BLE-19b`：RSC Total Distance 使用 0.1m 分辨率，FTMS Treadmill Total Distance 使用 1m 分辨率，两者必须从同一内部累计值分别换算。
- `BLE-19a`：字段增减、flags 位变化或单位变化都视为协议兼容性变更，必须先更新人工维护的 golden vectors，再同步生产 codec、Python 解析器和文档。

## FTMS 控制点

- `BLE-20`：FTMS 控制点仅支持以下 OpCode：
- `RequestControl`
- `SetTargetPower`
- `StartResume`
- `StopPause`
- `Reset`
- `BLE-21`：未支持的 OpCode 必须返回 `Response Code + Not Supported`，禁止静默吞掉。
- `BLE-22`：`SetTargetPower` 只负责解析目标功率并交给应用层回调，禁止在控制点回调内直接改动复杂状态机。
- `BLE-23`：`StartResume`、`StopPause`、`Reset` 必须先把事件可靠交给主循环；未接受时返回 Operation Failed，禁止提前更新 FTMS 状态。
- `BLE-23a`：ERG 目标和训练启停属于连接会话态，不得置配置 dirty 或写入 NVS。

## 回调与中断安全

- `BLE-24`：GAP 回调、GATT 回调、通知定时器回调、中断回调中禁止阻塞。
- `BLE-25`：这些回调中禁止堆上大块重分配，通知缓冲区必须静态预分配。
- `BLE-26`：这些回调中禁止做复杂串行化逻辑，必要状态变更应投递到主循环或状态机处理。
- `BLE-27`：ISR 中禁止调用 NimBLE API、日志 API、NVS API 和任何非 `FromISR` 接口。

## 验证要求

- `BLE-28`：任何 BLE 服务、特征、通知格式、控制点行为改动，都要更新 `tests/ble_selftest.py`。
- `BLE-29`：若改动涉及连接状态或广播恢复行为，应同时复核 `app_state.c` 的连接事件处理与 LED 表现。
