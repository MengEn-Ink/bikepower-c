# BLE 蓝牙服务详解

> 本文档详细说明 BikePower C 的 BLE 实现，基于 NimBLE host，提供 5+1 个标准蓝牙服务。

## 目录

- [服务架构总览](#服务架构总览)
- [服务与 UUID 清单](#服务与-uuid-清单)
- [两种运动配置文件](#两种运动配置文件)
- [GATT 服务表](#gatt-服务表)
- [数据包格式规范](#数据包格式规范)
- [FTMS 控制点协议](#ftms-控制点协议)
- [广播与连接](#广播与连接)
- [通知机制](#通知机制)
- [线程安全与互斥锁](#线程安全与互斥锁)
- [关键函数 API](#关键函数-api)
- [DIS 设备信息服务](#dis-设备信息服务)

---

## 服务架构总览

BikePower C 采用 NimBLE host 实现蓝牙 peripheral 角色，暴露标准蓝牙 SIG 定义的运动传感器服务集，与码表/骑行 App 兼容。

### 核心设计原则

| 原则 | 实现 |
|------|------|
| 静态预分配 | 所有通知缓冲区按 `ble_payload_codec.h` 的长度常量在编译期分配，运行时零 malloc |
| 句柄缓存 | GATT 特征值句柄在服务注册后缓存到静态全局变量，避免运行时重复查找 |
| 双时间尺度 | 主循环 50ms 写共享变量，BLE 定时器 1s 读并打包发送 |
| 单连接模型 | 同一时间只允许一个中心设备连接，断开后自动恢复广播 |
| 共存友好 | WiFi 配网期间 BLE 保持在线（coex 仲裁），仅共存失败时才停 BLE 兜底 |

### 任务模型

| 任务/上下文 | 职责 |
|------------|------|
| `nimble_host` 任务 | 运行 NimBLE 协议栈事件循环（`nimble_port_run()`） |
| `esp_timer` 回调 (`notify_tick`) | 1s 周期打包数据并发送通知 |
| GAP/GATT 回调 | 运行在 NimBLE host 任务上下文，处理连接/断开/写请求 |
| app_main 任务 | 调用 `ble_bikepower_set_data()` 更新共享快照，处理 BLE 启动失败重试 |

---

## 服务与 UUID 清单

### 骑行配置文件（默认）

| 服务 | UUID | 特征 | 属性 |
|------|------|------|------|
| Cycling Power Service (CPS) | `0x1818` | Power Measurement (`0x2A63`) | Read + Notify |
| Heart Rate Service (HRS) | `0x180D` | Heart Rate Measurement (`0x2A37`) | Read + Notify |
| Cycling Speed and Cadence (CSC) | `0x1816` | CSC Measurement (`0x2A5B`) | Read + Notify |
| Fitness Machine Service (FTMS) | `0x1826` | Feature (`0x2ACC`) | Read |
| | | Indoor Bike Data (`0x2AD2`) | Read + Notify |
| | | Control Point (`0x2AD9`) | Write + Write No Rsp + Indicate |
| | | Status (`0x2ADA`) | Read + Notify |
| Device Information Service (DIS) | `0x180A` | Manufacturer Name (`0x2A29`) | Read |
| | | Model Number (`0x2A24`) | Read |
| | | Firmware Revision (`0x2A26`) | Read |

### 跑步配置文件

| 服务 | UUID | 特征 | 属性 |
|------|------|------|------|
| Running Speed and Cadence (RSC) | `0x1814` | RSC Measurement (`0x2A53`) | Read + Notify |
| | | RSC Feature (`0x2A54`) | Read |
| | | Sensor Location (`0x2A5D`) | Read |
| Heart Rate Service (HRS) | `0x180D` | Heart Rate Measurement (`0x2A37`) | Read + Notify |
| Fitness Machine Service (FTMS) | `0x1826` | Feature (`0x2ACC`) | Read |
| | | Treadmill Data (`0x2ACD`) | Read + Notify |
| | | Control Point (`0x2AD9`) | Write + Write No Rsp + Indicate |
| | | Status (`0x2ADA`) | Read + Notify |
| Device Information Service (DIS) | `0x180A` | （同上） | Read |

---

## 两种运动配置文件

通过 `power_engine_set_sport_profile()` 切换运动模式，BLE 层会在下次启动时选择不同的 GATT 服务表：

```c
typedef enum {
    SPORT_PROFILE_CYCLING = 0,   // 默认：CPS + HRS + CSC + FTMS(Indoor Bike) + DIS（按 broadcast_mask 过滤）
    SPORT_PROFILE_RUNNING = 1,   // 跑步：RSC + HRS + FTMS(Treadmill) + DIS（按 broadcast_mask 过滤）
    SPORT_PROFILE_COUNT
} sport_profile_t;
```

### 切换注意事项

- 切换运动配置文件或修改 `broadcast_mask`（服务开关）都会触发 BLE 重启请求（`s_ble_restart_requested = true`）
- 主循环检测到该标志后会调用 `ble_bikepower_stop()` + `ble_bikepower_start()` 重新注册 GATT 表
- 切换期间会短暂断连，随后新服务集生效

### 广播字段差异

广播包 16-bit UUID 按当前 `broadcast_mask` 动态拼装，仅包含已注册服务：

| 配置 | 广播包 UUID（全开时） | Scan Response UUID |
|------|----------------------|-------------------|
| Cycling | CPS + CSC + FTMS | HRS（HEARTRATE 启用时） |
| Running | RSC + FTMS | HRS（HEARTRATE 启用时） |

> 广播包空间有限（31字节），主广播只放码表按设备类型过滤时最关键的服务，完整能力由连接后 GATT 发现获取。若某项服务被关闭，其 UUID 不会出现在广播包中。

---

## GATT 服务表

### 骑行模式 GATT 表

GATT 服务表在 `cycling_gatt_svcs[]` 静态数组中定义，注册时由 NimBLE 分配句柄并通过 `val_handle` 字段回写到静态全局变量：

```c
static uint16_t g_cps_val_handle;       // CPS Power Measurement
static uint16_t g_hr_val_handle;        // HRS Heart Rate Measurement
static uint16_t g_csc_val_handle;       // CSC Measurement
static uint16_t g_ftms_bike_handle;     // FTMS Indoor Bike Data
static uint16_t g_ftms_cp_handle;       // FTMS Control Point
static uint16_t g_ftms_status_handle;   // FTMS Status
```

### 跑步模式 GATT 表

```c
static uint16_t g_rsc_val_handle;       // RSC Measurement
static uint16_t g_hr_val_handle;        // (共享) HRS
static uint16_t g_ftms_treadmill_handle;// FTMS Treadmill Data
static uint16_t g_ftms_cp_handle;       // (共享) Control Point
static uint16_t g_ftms_status_handle;   // (共享) Status
```

---

## 数据包格式规范

所有多字节字段均采用**小端字节序**（Little-Endian），符合蓝牙 SIG 规范。

### CPS Power Measurement（8 字节）

| 偏移 | 长度 | 字段 | 值/说明 |
|------|------|------|---------|
| 0 | 2 | flags | `0x0020`（Crank Revolution Data Present） |
| 2 | 2 | Instantaneous Power | 功率值（W，sint16） |
| 4 | 2 | Cumulative Crank Revs | 累计曲柄转数（uint16，每次通知 +2） |
| 6 | 2 | Last Crank Event Time | 上次曲柄事件时间（1/1024s 单位） |

打包由生产代码 `ble_payload_codec.c` 负责；`tests/fixtures/ble_protocol_vectors.json` 同时约束 C encoder 和 Python parser，避免自证式测试。

**曲柄推演算法**：基于当前踏频 `cadence` 计算每转时间，每次通知累加 2 转，保持与旧版 MicroPython 协议位级兼容。

```c
uint32_t target = (2u * 60u * 1000u) / cadence;  // 每2转间隔毫秒
adjusted_ms = g_last_crank_ms + target;
g_crank_rev += 2;
time_1024 = (adjusted_ms * 1024 / 1000) & 0xFFFF;
```

### HRS Heart Rate Measurement（2 字节）

| 偏移 | 长度 | 字段 | 值/说明 |
|------|------|------|---------|
| 0 | 1 | flags | `0x00`（bpm 格式，无 EE/接触状态） |
| 1 | 1 | Heart Rate Measurement Value | 心率（bpm，uint8） |

### CSC Measurement（5 字节）

| 偏移 | 长度 | 字段 | 值/说明 |
|------|------|------|---------|
| 0 | 1 | flags | `0x02`（Crank Data Present） |
| 1 | 2 | Cumulative Crank Revs | 累计曲柄转数（与 CPS 共享 `g_crank_rev`） |
| 3 | 2 | Last Crank Event Time | 上次曲柄事件时间（与 CPS 共享） |

### RSC Measurement（10 字节，跑步模式）

| 偏移 | 长度 | 字段 | 值/说明 |
|------|------|------|---------|
| 0 | 1 | flags | `0x07`（Instantaneous Stride Length + Total Distance + Walking/Running） |
| 1 | 2 | Instantaneous Speed | 速度（1/256 m/s 单位，uint16） |
| 3 | 1 | Instantaneous Cadence | 步频（SPM，uint8） |
| 4 | 2 | Instantaneous Stride Length | 步幅（cm，uint16） |
| 6 | 4 | Total Distance | 总距离（0.1m，uint32）；领域层米值在 BLE 边界乘 10 |

暂停时速度、步频和步幅归零，flags 为 `0x03`；运行时 flags 为 `0x07`。两种状态都保留累计距离字段。

### RSC Feature（2 字节，跑步模式）

固定值 `{ 0x07, 0x00 }`（`0x0007`），表示支持瞬时步幅、总距离和 Walking/Running Status。

### Sensor Location（1 字节，跑步模式）

固定值 `0x05` = Foot（足部）。

### FTMS Indoor Bike Data（可变长，骑行模式）

| 偏移 | 长度 | 字段 | 条件 |
|------|------|------|------|
| 0 | 2 | flags | 位掩码决定后续字段 |
| 2 | 2 | Instantaneous Speed | flags bit2=1（cadence 启用时），单位 0.01 km/h |
| 4 | 2 | Instantaneous Cadence | flags bit2=1，单位 0.5 RPM |
| 6 | 2 | Instantaneous Power | flags bit6=1，单位 W，sint16 |

**flags 位定义**：
- bit0 (More Data)：cadence 禁用时置 1
- bit2：Instantaneous Cadence + Speed present
- bit6：Instantaneous Power present

| 广播字段组合 | flags | 长度 | 负载 |
|--------------|-------|------|------|
| 踏频 + 功率 | `0x0044` | 8 字节 | speed + cadence + power |
| 仅踏频 | `0x0004` | 6 字节 | speed + cadence |
| 仅功率 | `0x0041` | 4 字节 | power；More Data 置位，不携带 speed |

**速度/踏频换算**：
- speed = cadence * 30（模拟 90RPM → 27.0 km/h）
- ftms_cad = cadence * 2（FTMS 单位为 0.5 RPM）

### FTMS Treadmill Data（10 字节，跑步模式）

| 偏移 | 长度 | 字段 | 值/说明 |
|------|------|------|---------|
| 0 | 2 | flags | `0x0504`（Total Distance + Heart Rate + Elapsed Time） |
| 2 | 2 | Instantaneous Speed | 速度（单位转换：rsc_speed * 360 / 256 = 0.01 km/h） |
| 4 | 3 | Total Distance | 总距离（m，uint24，注意 3 字节） |
| 7 | 1 | Heart Rate | 心率（bpm） |
| 8 | 2 | Elapsed Time | 已用时间（s，uint16，超 0xFFFF 截断） |

FTMS Treadmill 的 Total Distance 单位保持米；不要套用 RSC Measurement 的 0.1m 单位。

### FTMS Feature（8 字节，静态）

**骑行模式**：
```c
{ 0x02, 0x40, 0x00, 0x00,   // Cadence + Power Measurement
  0x08, 0x00, 0x00, 0x00 }  // Target Power Setting
```

**跑步模式**：
```c
{ 0x04, 0x05, 0x00, 0x00,   // Distance + Heart Rate + Elapsed Time
  0x02, 0x00, 0x00, 0x00 }  // Target Speed Setting
```

### FTMS Status（当前状态均为 1 字节）

| 偏移 | 长度 | 字段 | 值/说明 |
|------|------|------|---------|
| 0 | 1 | Op Code | `0x01`=Reset，`0x02`=Stopped/Paused by User，`0x03`=Started/Resumed by User |

Fitness Machine Status 和 Control Point 是两套独立的 OpCode 编号。当前使用的三种 Status 不带参数，因此通知长度严格为 1 字节；未来支持带参数的 Status 时必须按对应格式编码，不得固定补零。

---

## FTMS 控制点协议

FTMS Control Point 采用写+指示（Indication）模式，客户端写命令后服务端通过 Indication 返回结果。

### 支持的 OpCode

| OpCode | 值 | 负载 | 功能 |
|--------|----|------|------|
| Request Control | `0x00` | 无 | 请求控制权 |
| Set Target Power | `0x05` | sint16 target (W) | 设置 ERG 目标功率 |
| Set Target Speed | `0x02` | uint16 target (0.01km/h) | 设置跑步目标速度（跑步模式） |
| Start/Resume | `0x07` | 无 | 开始/恢复运动 |
| Stop/Pause | `0x08` | 无 | 暂停运动 |
| Reset | `0x01` | 无 | 重置 |

> 未支持的 OpCode 返回 `Response Code + Not Supported (0x02)`，不静默吞掉。

### 响应格式（Indication，3 字节）

| 偏移 | 长度 | 字段 | 值 |
|------|------|------|----|
| 0 | 1 | Response Code OpCode | `0x80` |
| 1 | 1 | Request OpCode | 回传请求的 OpCode |
| 2 | 1 | Result Code | `0x01`=Success, `0x02`=Not Supported, `0x03`=Invalid Parameter, `0x04`=Operation Failed |

### Result Code 表

| 值 | 含义 | 触发条件 |
|----|------|---------|
| `0x01` | Success | 命令执行成功 |
| `0x02` | Not Supported | 未知 OpCode，或跑步模式下请求 SetTargetPower，或骑行模式下请求 SetTargetSpeed |
| `0x03` | Invalid Parameter | 负载长度错误或目标值超出协议/配置范围 |
| `0x04` | Operation Failed | 参数合法，但应用回调缺失、队列未接纳或状态转换失败 |

### 控制点处理流程

```
客户端写 Control Point
  → ftms_cp_cb()（NimBLE host 任务上下文）
    → 解析 op + 负载
    → 骑行模式 SetTargetPower: g_erg_cb(target) → 投递 APP_EVT_ERG_TARGET 到主循环
    → 跑步模式 SetTargetSpeed: 先校验值域，再调用 g_run_control_cb(BLE_RUN_CTRL_SPEED, target)
    → running Start/Stop/Reset: 先由应用回调可靠接纳，再更新 g_ftms_status_buf
    → ftms_send_response() → indicate_one() 发送响应
```

带应用接纳语义的命令（Set Target Power/Speed 与 running Start/Stop/Reset）只有回调成功时才发送 Success；running 状态命令也只在成功后更新 FTMS Status。参数合法但回调缺失或拒绝时返回 `Operation Failed (0x04)`，不得先制造“已成功”的状态通知。跑步 Start/Resume、Stop/Pause、Reset 分别控制非持久化会话状态：暂停后的 RSC 运动量归零但距离/用时冻结，Reset 清累计量并保持原运行/暂停态。骑行 Set Target Power 进入非持久化 ERG 会话，Stop/Pause 或 Reset 退出 ERG 并恢复原基础功率；ERG 目标和会话模式均不得写入 NVS。

---

## 广播与连接

### 广播参数

| 参数 | 值 | 说明 |
|------|----|------|
| 模式 | `BLE_GAP_CONN_MODE_UND` + `BLE_GAP_DISC_MODE_GEN` | 通用可连接、可发现 |
| 间隔 | `BLE_ADVERTISING_INTERVAL_MS`（100ms） | 换算为 0.625ms 单位写入；缩短首次连接与断开重连等待 |
| 超时 | `BLE_HS_FOREVER` | 无限时长广播，直到连接或停止 |
| 标志 | `BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP` | 通用发现，不支持 BR/EDR |

### 设备名处理

- 设备名存于 `g_device_name[]`（最多 `DEVICE_NAME_MAX-1` 个字符）
- 长度 ≤ `BLE_ADV_NAME_MAX_LEN`（18 字节）时完整放入广播包
- 超过 18 字节时截断放入广播包，完整名放入 Scan Response
- 通过 `ble_bikepower_set_device_name()` 设置，`ble_bikepower_refresh_device_name()` 热更新（已连接时不重启广播）

### 连接事件处理

`gap_event_cb()` 处理三类 GAP 事件：

| 事件 | 处理 |
|------|------|
| `BLE_GAP_EVENT_CONNECT` (status=0) | 保存 `g_conn_handle`，记录"已连接"日志 |
| `BLE_GAP_EVENT_CONNECT` (status≠0) | 连接失败，重新启动广播 |
| `BLE_GAP_EVENT_DISCONNECT` | 清 `g_conn_handle`，重新启动广播 |
| `BLE_GAP_EVENT_ADV_COMPLETE` | 广播结束，重新启动广播 |

---

## 通知机制

### 双时间尺度设计

通知模块采用**写者-读者分离**设计，避免跨任务锁争用：

```
app_main 任务（50ms 周期）          esp_timer 回调（1s 周期）
       │                                    │
       ▼                                    ▼
ble_bikepower_set_data()            notify_tick()
  写 g_power_w/g_hr_bpm/g_cadence     xSemaphoreTake(g_notify_lock, 0)
  （volatile 变量，单字原子）          │
                                       ├→ pack_all() / pack_running()
                                       │    读取 volatile 变量打包
                                       │    更新 g_crank_rev / g_last_crank_ms
                                       │
                                       ├→ notify_one_nolock() × N
                                       │    ble_gatts_notify_custom()
                                       │
                                       └→ xSemaphoreGive(g_notify_lock)
```

### 通知发送条件

通知仅在以下条件全部满足时发送：
1. `!g_stopping`（未在停止过程中）
2. `g_ready`（BLE 已成功启动并完成同步）
3. `g_conn_handle != BLE_HS_CONN_HANDLE_NONE`（有活跃连接）
4. 成功获取 `g_notify_lock`（stop 过程中拿不到锁则跳过）

### 广播字段掩码

`g_broadcast_mask` 同时控制 **GATT 服务注册**与**通知发送**，可通过配网页或 setter 动态调整：

| 位 | 掩码 | 控制的服务 / 通知 |
|----|------|-------------------|
| bit0 | `BROADCAST_FIELD_POWER` | 骑行 CPS、FTMS Power；跑步 RSC/FTMS Speed |
| bit1 | `BROADCAST_FIELD_CADENCE` | 骑行 CSC、FTMS Cadence；跑步 RSC/FTMS Cadence |
| bit2 | `BROADCAST_FIELD_HEARTRATE` | HRS、FTMS Treadmill HR |

- **服务注册**：`build_active_gatt_svcs()` 按 mask 过滤 GATT 服务表，未启用的服务不注册到 GATT（例如只开心率时蓝牙只暴露 HRS + DIS，类似心率带）。FTMS 在 POWER 或 CADENCE 任一启用时注册；DIS 始终注册。
- **广播 UUID**：广播包 16-bit UUID 列表按已注册服务动态拼装，不声明 GATT 中不存在的服务。
- **通知**：`notify_one_nolock`/`indicate_one` 以 `handle==0` 守卫跳过未注册服务。
- mask 为 0 时自动归一化为 `BROADCAST_FIELD_ALL`（全开）。
- 运行时修改 mask 后，配网页保存会置 `s_ble_restart_requested`，主循环在无连接时 stop→start 重建 GATT 表。

---

## 线程安全与互斥锁

### g_notify_lock 互斥锁

通知互斥锁是 BLE 模块线程安全的核心，解决以下竞态：

1. **notify_tick 回调 vs ble_bikepower_stop()**：stop 在 deinit NimBLE 前必须拿到锁，确保没有飞行中的回调正在访问协议栈资源（防止 UAF）
2. **FTMS 响应 vs stop()**：控制点回调中调用 `indicate_one()` 也要拿锁
3. **多通知原子性**：同一轮 notify_tick 中多个通知的发送过程中不会被 stop 打断

### 锁等待策略

| 调用场景 | 等待时间 | 策略原因 |
|---------|---------|---------|
| `notify_tick`（esp_timer 回调） | 0（非阻塞，trylock） | 定时器回调不能阻塞 |
| `notify_one`（FTMS 响应等） | `BLE_NOTIFY_LOCK_WAIT_MS`（短等待） | FTMS 关键响应尽量不丢，但不阻塞太久 |
| `ble_bikepower_stop` | `BLE_STOP_LOCK_WAIT_MS`（较长等待） | 必须等飞行中回调结束才能 deinit |

### volatile 变量

跨任务访问的共享变量使用 `volatile` 修饰防止编译器缓存：
- `g_conn_handle`：host 任务写，app_main/esp_timer 读
- `g_ready` / `g_stopping` / `g_start_failed`：app_main 写，多上下文读
- `g_power_w` / `g_hr_bpm` / `g_cadence` 等：app_main 写，esp_timer 读（C3 上 16/8 位单次访问原子）

---

## 关键函数 API

### 生命周期管理

| 函数 | 说明 |
|------|------|
| `ble_bikepower_start()` | 初始化 NimBLE、注册 GATT 服务、启动通知定时器、创建 host 任务 |
| `ble_bikepower_stop()` | 停止定时器、等待回调结束、断开连接、停止广播、deinit NimBLE |
| `ble_bikepower_is_ready()` | 查询 BLE 是否已就绪（广播已启动） |
| `ble_bikepower_start_failed()` | 查询启动是否失败（供 app_main 重试） |

### 数据更新

| 函数 | 说明 |
|------|------|
| `ble_bikepower_set_data(power, hr, cadence)` | 更新骑行模式的功率/心率/踏频快照 |
| `ble_bikepower_set_running_data(snapshot, hr)` | 更新跑步模式的 RSC/FTMS 数据 |
| `ble_bikepower_set_profile(profile)` | 设置运动配置文件（需重启 BLE 生效） |
| `ble_bikepower_set_broadcast_mask(mask)` | 设置通知字段掩码 |
| `ble_bikepower_set_device_name(name)` | 设置设备名缓冲区 |
| `ble_bikepower_refresh_device_name()` | 热更新 GAP 设备名并重广播（未连接时） |

### 回调注册

| 函数 | 说明 |
|------|------|
| `ble_bikepower_set_erg_callback(cb)` | 注册 ERG 目标功率回调（FTMS SetTargetPower 时调用） |
| `ble_bikepower_set_erg_clear_callback(cb)` | 注册 ERG 清除回调（Stop/Reset 时调用） |
| `ble_bikepower_set_run_control_callback(cb)` | 注册跑步控制回调（Start/Stop/Reset/SetSpeed 时调用） |

### 状态查询

| 函数 | 说明 |
|------|------|
| `ble_bikepower_conn_count()` | 返回当前连接数（0 或 1） |

### 启动失败处理

BLE 启动或重广播失败时，不直接在 NimBLE 回调中做复杂清理，而是：
1. `mark_start_failed(reason, rc)`：清 `g_ready`、置 `g_start_failed = true`
2. app_main 主循环检测到 `ble_bikepower_start_failed()` 后，在主任务上下文中执行 stop + 延时 + 重试
3. 避免在协议栈回调中重入释放资源导致崩溃

---

## DIS 设备信息服务

DIS 提供三个只读特征值，供 App 识别设备：

| 特征 | 来源 | 示例值 |
|------|------|--------|
| Manufacturer Name | `DEVICE_MANUFACTURER` 编译常量 | `"BikePower"` |
| Model Number | `DEVICE_MODEL` 编译常量 | 根据板型变化 |
| Firmware Revision | `board_firmware_name()` 动态生成 | 包含版本号+板型+Git SHA |

`board_firmware_name()` 根据当前检测到的板型生成固件名称，格式为：
`BikePower v{version}-{board} ({git_sha})`
