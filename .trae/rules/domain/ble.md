# BLE 领域规则（BikePower C）

<!-- stability: tier-2 (稳定，随 BLE 设计调整) -->
<!-- rule-id-prefix: BLE -->
<!-- applies-to: main/ble_bikepower.c, main/bridge_runtime.c, main/ble_power_bridge.c, main/app_main.c, tests/ble_selftest.py -->

> 本文件定义 BikePower C 的 BLE 实现边界。通用编码和硬件约束仍以 `coding-style.md`、`hardware-constraints.md`、`wifi-ble-mutex.md` 为准。

## 角色与范围

- `BLE-1`：BikePower C 主工作角色是 NimBLE `peripheral`。cycling profile 暴露 CPS/HRS/CSC/FTMS/DIS；running profile 暴露 RSC/HRS/FTMS/DIS，两套 GATT 表互斥。
- `BLE-2`：central/observer 用于真实功率桥接；扫描只能由用户在配网页主动触发并限时运行。开机与异常恢复只允许按已保存地址定向连接，禁止任何后台扫描。
- `BLE-3`：BLE 是设备主工作模式；普通配网期间 peripheral 保持在线。外部设备读取使用临时 central 连接，完成或超时后必须主动断开。
- `BLE-3a`：桥接扫描候选必须命中 CPS，允许无名称功率计；地址类型和地址共同标识设备，候选编号限定本轮代次。仅 DIS/BAS/HRS/CSC 的设备不得作为功率上游。
- `BLE-3b`：桥接固定最多保存三台设备。冷启动最近成功设备优先，快速恢复窗口为 60 秒且最多两轮；失败后运行时降级为完整模拟输出，每 60 秒只定向探测一台已保存设备。
- `BLE-3c`：自动降级只改变运行时实际来源，不得把持久配置来源改回模拟；恢复成功后更新最近成功地址提示。
- `BLE-3d`：功率来源须由两份合法 CPS 样本确认；两份合法 0W 样本同样可确认来源，但 0W 禁止应用固定偏移或比例校正。
- `BLE-3e`：三台设备共享同一校正配置，主备切换不得修改校正模式、瓦数偏移或比例。

## 服务清单

- `BLE-4`：GATT 必须维持当前 profile 对应的服务组合，不得把 cycling 与 running 服务随意混装。
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
- `BLE-9a`：模拟、桥接、跑步数据由 `s_data_lock` 保护；通知采集一次完整输出快照后统一打包，GATT 锁内复制 payload 与动态长度、锁外写 mbuf。ready/stopping/failed/profile/mask/连接句柄使用 C11 原子，协议栈释放仍须通知互斥锁。
- `BLE-9b`：八组配置 GATT 与 revision 事务由 `ble_config_service` 维护；`0009` 保存主设备与共享校正，`000a/000b` 保存两个备选身份。snapshot 发布与提交结果只由应用主任务调用。回调在启动前注册，锁内不调用 BLE、日志、NVS 或 JSON。
- `BLE-10`：新增通知或改变节拍时，必须同步更新 `tests/ble_selftest.py` 与技术文档。

## GATT 句柄缓存

- `BLE-11`：GATT 特征值句柄必须在服务注册后缓存到静态全局变量，避免运行时重复查找。
- `BLE-12`：至少维护以下句柄缓存：
- `g_cps_val_handle`
- `g_hr_val_handle`
- `g_csc_val_handle`
- `g_ftms_bike_handle`
- `g_ftms_treadmill_handle`
- `g_ftms_cp_handle`
- `g_ftms_status_handle`
- `g_rsc_val_handle`
- `BLE-13`：通知发送路径必须基于句柄缓存，不得在高频路径重新解析 attribute table。

## 数据包格式

- `BLE-14`：所有打包按蓝牙规范使用小端字节序。
- `BLE-15`：CPS Measurement 固定为 8 字节 crank-only 格式：`flags + instantaneous_power + cumulative_crank_revs + last_crank_event_time`。
- `flags`：2 字节
- `power`：2 字节，有符号功率值，单位 W
- `rev`：2 字节，累计曲柄转数
- `time`：2 字节，最后曲柄事件时间，单位 1/1024 秒
- `BLE-16`：HRS 数据包格式固定为 `flags + bpm`。
- `flags`：1 字节
- `bpm`：1 字节
- `BLE-17`：CSC Measurement 固定为 5 字节 crank-only 格式：`flags + cumulative_crank_revs + last_crank_event_time`。
- `flags`：1 字节
- `rev`：2 字节
- `time`：2 字节
- `BLE-18`：FTMS Indoor Bike Data 必须让 flags 与实际字段严格一致：全开 `0x0044/8B`、仅踏频 `0x0004/6B`、仅功率 `0x0041/4B`。
- `BLE-18a`：FTMS Treadmill Data 固定 `flags=0x0504`、10 字节，字段为 speed(0.01km/h) + distance(uint24,m) + heart rate + elapsed time。
- `BLE-18b`：RSC Measurement 固定 10 字节；运行 flags `0x07`，暂停 flags `0x03`；Total Distance 单位是 0.1m，领域层米值进入 BLE codec 前必须乘 10。RSC Feature 为 2 字节 `0x0007`。
- `BLE-19`：字段增减、flags 位变化或单位变化都视为协议兼容性变更，必须同步更新自测脚本与外部文档。
- `BLE-19a`：CPS/CSC/HRS/FTMS/RSC 字节布局统一走生产使用的纯 C `ble_payload_codec`；固定 JSON golden vectors 由人工维护，禁止从编码器反向生成真值。

## FTMS 控制点

- `BLE-20`：FTMS 控制点仅支持以下 OpCode：
- `RequestControl`
- `SetTargetPower`
- `StartResume`
- `StopPause`
- `Reset`
- `BLE-21`：未支持的 OpCode 必须返回 `Response Code + Not Supported`，禁止静默吞掉。
- `BLE-22`：`SetTargetPower` 只负责解析目标功率并交给应用层回调，禁止在控制点回调内直接改动复杂状态机。
- `BLE-23`：running profile 的 `StartResume`、`StopPause`、`Reset` 必须先确认应用事件已被可靠接收；未接收返回 `Operation Failed(0x04)` 且不得提前更新 FTMS Status，接收成功后才更新状态并返回 Success。
- `BLE-23c`：Fitness Machine Status OpCode 与 Control Point 独立；Reset=`0x01`、Stopped/Paused by User=`0x02`、Started/Resumed by User=`0x03`，这三类通知必须为 1 字节。
- `BLE-23a`：跑步 Stop 后速度、步频、步幅归零且距离/用时冻结；Start 后恢复累计；Reset 清零累计但保持 Reset 前的运行/暂停态。
- `BLE-23b`：ERG 目标、激活状态和运行时模式不得覆盖 `base_power` 或设置配置 dirty；NVS 导出时 ERG 必须规范化为 steady。

## 回调与中断安全

- `BLE-24`：GAP 回调、GATT 回调、通知定时器回调、中断回调中禁止阻塞。
- `BLE-25`：这些回调中禁止堆上大块重分配，通知缓冲区必须静态预分配。
- `BLE-26`：这些回调中禁止做复杂串行化逻辑，必要状态变更应投递到主循环或状态机处理。
- `BLE-27`：ISR 中禁止调用 NimBLE API、日志 API、NVS API 和任何非 `FromISR` 接口。

## 验证要求

- `BLE-28`：任何 BLE 服务、特征、通知格式、控制点行为改动，都要更新 `tests/ble_selftest.py`。
- `BLE-29`：若改动涉及连接状态或广播恢复行为，应同时复核 `app_state.c` 的连接事件处理与 LED 表现。
- `BLE-30`：协议改动至少运行 `make -C test run` 与 `python3 tests/test_ble_protocol_vectors.py`；桥接恢复改动还要运行 `tests/test_bridge_session.py`、`tests/check_bridge_failover_contract.py`、离线双次重连 harness，以及真机多设备定向连接/断开/降级/恢复。
- `BLE-31`：单固件运行时只暴露一个 GATT profile。双 profile 真机门禁必须显式声明当前 profile，或使用 `tests/device_profile_harness.py` 经配网 API 切换；测试阶段强制 `broadcast_mask=7`，结束时必须在 `finally` 恢复原 profile 与原广播掩码。
- `BLE-32`：running `--control` 真机门禁必须逐 opcode 等待对应 indication，并按 moving → Stop 冻结 → paused Reset 清零且保持暂停 → Resume 恢复四阶段分别采集至少两个实际样本；RSC 与 Treadmill 的累计增长/冻结都要严格判断，并由 `tests/test_ble_running_harness.py` 离线守护流程结构与 helper 语义。
