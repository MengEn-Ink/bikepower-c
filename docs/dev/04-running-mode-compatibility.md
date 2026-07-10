# 跑步模式兼容性调研与适配方案

> 状态：协议调研与实现追踪。P0 RSC + HRS 跑步传感器路径已落地；FTMS Treadmill Data / Control Point 仍作为后续兼容增强评估。
> 目标软件：Zwift Run、MyWhoosh Running。  
> 当前固件基线：BikePower C v4.0.6，NimBLE peripheral；骑行模式提供 CPS + HRS + CSC + FTMS Indoor Bike + DIS，跑步模式提供 RSC + HRS + DIS。

---

## 1. 结论

跑步模式不能只复用当前 Cycling Power / CSC / FTMS Indoor Bike 数据。Zwift 和 MyWhoosh 的跑步入口首先需要一个“跑步速度来源”：Footpod、跑步手表、智能鞋、跑步机传感器或智能跑步机。对 BLE 设备来说，最稳妥的首版协议面是 **Running Speed and Cadence Service, RSC, UUID 0x1814**，它提供跑步速度和步频；心率继续使用现有 **Heart Rate Service, HRS, UUID 0x180D**。

建议按三阶段落地：

1. **P0：跑步传感器模式**  
   新增 RSC 服务，广播 RSC + HRS，输出速度、步频、可选步幅和累计距离。目标是被 Zwift/MyWhoosh 作为 footpod / running sensor 配对，并驱动跑步配速。
2. **P1：FTMS 跑步机数据模式**  
   在 Fitness Machine Service 下新增 Treadmill Data, UUID 0x2ACD，输出跑带速度、累计距离、坡度、心率、用时。目标是兼容 Runn NPE / 智能跑步机一类软件路径。
3. **P2：跑步机控制点**  
   扩展 Fitness Machine Control Point，支持 Request Control、Set Target Speed、Set Target Inclination、Start/Resume、Stop/Pause。目标是让支持控制的 App 下发目标速度/坡度；这一步需要更谨慎，因为当前设备只是模拟器，没有真实跑带执行机构。

首版推荐做 P0，不建议一开始把自行车服务和跑步服务全部同时广播。跑步软件和骑行软件的设备筛选逻辑不同，同时暴露 CPS/CSC/RSC/FTMS Bike/FTMS Treadmill 容易被 App 归类到错误运动类型。

---

## 2. 软件侧兼容口径

### 2.1 Zwift Run

Zwift 跑步需要速度来源。公开支持的设备类型包括兼容跑步机、Footpod、智能鞋、手表和跑步机传感器。Zwift RunPod 的公开说明强调它测量速度和步频，数据用于驱动 Zwift 中的跑步配速；Runn 传感器公开说明则强调通过 BLE/ANT+ 输出速度、坡度和步频。

因此 Zwift 适配应按两条路径理解：

- Footpod / running sensor 路径：速度 + 步频是核心，RSC 是最合适的 BLE 标准服务。
- Smart treadmill / Runn 路径：速度 + 步频 + 坡度更接近真实跑步机传感器，FTMS Treadmill Data 是标准服务面。

Zwift 的“Controllable”跑步控制能力目前公开材料更明确指向 Wahoo KICKR RUN 这类真实智能跑步机。通用 FTMS Treadmill 是否会被 Zwift 开放控制，需要真机配对验证，不能在文档阶段承诺。

### 2.2 MyWhoosh Running

MyWhoosh 已公开提到跑步模式增强，并列出 Stryd、Assault Runner Pro、Garmin HRM、Runn NPE 等跑步传感器。这个列表同样说明首要需求是跑步速度来源，其次是步频、心率和坡度。

MyWhoosh 的设备过滤策略没有 Zwift 那样多的公开协议细节，落地时要把“可被扫描到”和“进入活动后速度稳定驱动 avatar”分开验收。首版仍建议以 RSC + HRS 起步，再用 FTMS Treadmill Data 提高命中率。

---

## 3. 需要广播哪些数据

这里的“广播”分两层：GAP 广播包用于让 App 找到设备；GATT 通知用于连接后持续传训练数据。跑步模式真正决定配速的是连接后的 GATT 通知，不是广播包本身。

### 3.1 P0 最小广播：RSC + HRS

GAP 广播包建议包含：

| 字段 | 建议值 | 说明 |
|------|--------|------|
| Flags | `LE General Discoverable` + `BR/EDR Not Supported` | 与当前实现一致 |
| Local Name | `BikeRun` 或用户配置名 | 名称过长时沿用当前策略：广播放短名，scan response 放完整名 |
| Complete 16-bit Service UUIDs | `0x1814` RSC，`0x180D` HRS | 首版最关键。RSC 用于速度/步频，HRS 用于心率 |

不建议 P0 广播 CPS `0x1818`、CSC `0x1816` 或 FTMS Indoor Bike `0x2AD2` 相关骑行能力，否则 App 可能把设备归到骑行设备。

### 3.2 P1 增强广播：FTMS Treadmill

如果进入跑步机数据模式，GAP 广播包建议包含：

| 字段 | 建议值 | 说明 |
|------|--------|------|
| Complete 16-bit Service UUIDs | `0x1826` FTMS，`0x180D` HRS | 让 App 按智能跑步机路径发现设备 |
| Local Name | `BikeRun Treadmill` 或用户配置名 | 完整名可放 scan response |

如果 31 字节广播包放不下完整名称，优先保证服务 UUID，名称可截断或放 scan response。当前 `start_advertising()` 已经具备“短名广播 + 完整名 scan response”的模式，可以复用。

### 3.3 是否需要 Service Data

首版不需要在广播包放 Service Data。Zwift/MyWhoosh 的跑步配速需要连接后订阅标准 GATT 特征，广播阶段只要能暴露正确服务 UUID 和可识别名称即可。Service Data 会挤占 31 字节预算，也会增加 App 兼容不确定性。

---

## 4. 连接后必须提供的 GATT 数据

### 4.1 RSC 服务，P0 必做

服务与特征：

| 项 | UUID | 属性 | 首版要求 |
|----|------|------|----------|
| Running Speed and Cadence Service | `0x1814` | Primary Service | 必做 |
| RSC Measurement | `0x2A53` | Notify | 必做，每 1s 通知 |
| RSC Feature | `0x2A54` | Read | 必做，声明支持能力 |
| Sensor Location | `0x2A5D` | Read | 建议做，返回 Foot 或 Other |
| SC Control Point | `0x2A55` | Indicate + Write | P0 可不做；如果 Feature 声明支持校准/累计距离设置，则必须做 |

RSC Measurement 建议格式：

| 字段 | 类型 | 单位 | P0 建议 |
|------|------|------|---------|
| Flags | `uint8` | 位图 | bit0=步幅是否存在，bit1=累计距离是否存在，bit2=跑步/走路状态 |
| Instantaneous Speed | `uint16` | 1/256 m/s | 必做，由目标配速换算 |
| Instantaneous Cadence | `uint8` | step/min | 必做，建议 160-190 spm |
| Instantaneous Stride Length | `uint16` | 1/100 m | 建议做，可由速度和步频反推 |
| Total Distance | `uint32` | m | 建议做，用 1s tick 累加 |

推荐 P0 flags：包含步幅、累计距离，跑步状态置 1。这样 App 可以同时拿到速度、步频、距离，行为更接近 RunPod / Stryd。

关键换算：

```text
speed_mps = pace_kmh / 3.6
rsc_speed = round(speed_mps * 256)
cadence_spm = configured_spm
stride_length_m = speed_mps * 60 / cadence_spm
rsc_stride = round(stride_length_m * 100)
total_distance_m += speed_mps * dt_seconds
```

### 4.2 HRS 服务，P0 复用

当前固件已经实现 HRS：

- Service UUID：`0x180D`
- Heart Rate Measurement UUID：`0x2A37`
- 当前格式：flags `0x00` + 8-bit BPM

跑步模式可直接复用现有心率模型和通知打包。MyWhoosh 明确支持 Garmin HRM 等心率来源，Zwift 跑步配对页也有 Heart Rate 传感器入口，因此 HRS 应保留。

### 4.3 FTMS Treadmill Data，P1 增强

服务与特征：

| 项 | UUID | 属性 | 说明 |
|----|------|------|------|
| Fitness Machine Service | `0x1826` | Primary Service | 当前已有，但内容是 Indoor Bike |
| Fitness Machine Feature | `0x2ACC` | Read | 需要区分 Bike/Treadmill 能力位 |
| Treadmill Data | `0x2ACD` | Notify | 跑步机训练数据 |
| Fitness Machine Status | `0x2ADA` | Notify/Read | 控制点存在时建议保留 |
| Fitness Machine Control Point | `0x2AD9` | Write + Indicate | P2 才建议打开控制 |
| Training Status | `0x2AD3` | Read/Notify | 可选，结构化训练状态 |

Treadmill Data 首版字段建议：

| 字段 | 单位 | 建议 |
|------|------|------|
| Instantaneous Speed | 0.01 km/h | 必做，flags bit0=0 表示本包包含速度 |
| Total Distance | m，通常为 `uint24` | 建议做 |
| Inclination | 0.1% | P1 建议做，默认 0.0% |
| Ramp Angle Setting | 0.1 degree | 如果置 Inclination present，按规范通常要同时给出 |
| Heart Rate | bpm | 可做；已有 HRS 时也可重复输出 |
| Elapsed Time | s | 建议做，便于 App 显示训练时长 |

FTMS Treadmill Data 的 flags 语义与 RSC 不同，尤其 bit0 是 More Data：单包传完整数据时应让 Instantaneous Speed 出现在本包。实现时不要复用 Indoor Bike 的 flags `0x0044`。

### 4.4 FTMS Control Point，P2 谨慎实现

当前固件已经支持 FTMS Control Point 的自行车 ERG 子集：

- Request Control `0x00`
- Reset `0x01`
- Set Target Power `0x05`
- Start/Resume `0x07`
- Stop/Pause `0x08`
- Response Code `0x80`

跑步机控制需要新增：

| OpCode | 参数 | 说明 |
|--------|------|------|
| Set Target Speed `0x02` | `uint16`，0.01 km/h | App 下发目标跑带速度 |
| Set Target Inclination `0x03` | `sint16`，0.1% | App 下发目标坡度 |

注意：FTMS 规范要求 Control Point 响应用 indication，而当前代码把 `0x2AD9` 设置为 notify。自行车 ERG 目前能跑通，不代表跑步机控制也会被 Zwift/MyWhoosh 接受。P2 如果要做，应把控制点 CCCD/响应语义按 FTMS 规范补齐，并确认不会破坏现有 ERG 兼容性。

---

## 5. 当前固件影响范围

当前 BLE 实现集中在 `main/ble_bikepower.c`：

- `gatt_svcs[]` 静态注册 CPS、HRS、CSC、FTMS、DIS。
- `start_advertising()` 当前只广播 CPS `0x1818` 和 HRS `0x180D`。
- `notify_tick()` 每 1s 通知 CPS、HRS、CSC、FTMS Indoor Bike。
- 通知缓冲区是静态数组，符合当前零动态大分配策略。
- `sdkconfig.defaults` 中 `CONFIG_BT_NIMBLE_MAX_CCCDS=8`。

跑步模式涉及的文件预计为：

| 文件 | 改动 |
|------|------|
| `main/bikepower_config.h` | 新增 RSC/Treadmill UUID、跑步范围常量、控制点 opcode |
| `main/ble_bikepower.c/.h` | 拆分 BLE profile，新增 RSC/Treadmill 打包、广播 UUID 选择、跑步控制点回调 |
| `components/bikepower_domain/include/domain_config.h` | 新增速度、步频、坡度、步幅范围与默认值 |
| `components/bikepower_domain/power_engine.c/.h` | 增加跑步快照或独立 running_engine，避免把功率字段硬塞成速度 |
| `components/bikepower_domain/config_store.c/.h` | 持久化运动类型、跑步速度、步频、坡度等配置 |
| `main/nvs_store.c` | 新增 NVS key，schema 版本升级 |
| `main/wifi_portal.c` 与 `main/web/*` | 配网页增加“骑行/跑步”运动类型与跑步参数 |
| `tests/ble_selftest.py` | 增加跑步 profile 扫描、RSC/FTMS Treadmill 通知解析 |
| `tests/check_config_consistency.py` | 增加 UUID、通知间隔、NVS key、CCCD 数量检查 |
| `docs/dev/01-technical-reference.md` | 实现后同步 BLE 服务章节 |

如果 P0 同时保留现有 6 个 notify/indicate 特征并新增 RSC Measurement，`CONFIG_BT_NIMBLE_MAX_CCCDS=8` 仍接近上限；如果再加 Treadmill Data、Training Status 或更多控制点能力，需要提高 CCCD 上限并实测堆内存。

---

## 6. 架构方案对比

### 方案 A：单固件、启动时选择 Cycling / Running Profile

设备配置里增加 `sport_profile`，取值 `cycling` 或 `running`。启动 BLE 前根据该配置选择 GATT 表、广播服务 UUID 和通知打包逻辑。用户在配网页切换运动类型后，提示断开当前 BLE 连接并重启 BLE profile；不需要重启整机，也不触碰 WiFi/BLE 共存规则。

优点：

- 对 App 最干净：骑行模式只像功率计/骑行台，跑步模式只像 footpod/跑步机。
- 避免 Zwift/MyWhoosh 设备分类混乱。
- 保持一个固件包，用户不用刷不同版本。

缺点：

- 切换运动类型时需要 BLE 断开重连。
- 当前 `ble_bikepower.c` 需要从“固定五服务”改为 profile 化服务注册。

推荐采用。

### 方案 B：单固件同时暴露 Cycling + Running 所有服务

继续保留 CPS/CSC/FTMS Bike，同时新增 RSC/FTMS Treadmill，广播里尽量放多个服务 UUID。

优点：

- 不需要运动类型切换。
- 同一设备可以被不同 App 发现。

缺点：

- 广播空间紧张，服务 UUID 和名称会互相挤压。
- App 可能按错误服务分类，例如跑步入口看到 cycling power，骑行入口看到 treadmill。
- CCCD、GATT 表、测试矩阵都会变大。

不推荐作为首版。

### 方案 C：独立 Running 固件

新增构建配置或分支，生成只支持跑步的固件。

优点：

- 实现最快，风险隔离最好。
- 不影响当前骑行模式发版。

缺点：

- 用户要刷不同固件切换用途。
- Release 产物和说明复杂度增加。
- 后续配置、测试、文档容易分叉。

适合作为内部验证分支，不建议作为最终产品形态。

---

## 7. 推荐落地路线

### 阶段 P0：RSC 跑步传感器

目标：Zwift/MyWhoosh 能在跑步配对页发现设备，并通过速度/步频/心率驱动跑步。

实现要点：

1. 在领域层新增 `running_snapshot_t`，字段包含 `speed_x100_kmh`、`cadence_spm`、`stride_cm`、`distance_m`、`heartrate_bpm`。
2. 新增 `sport_profile` 配置，默认仍为 `cycling`，避免影响现有用户。
3. BLE 启动时根据 `sport_profile` 注册 cycling 或 running GATT 表。
4. running GATT 表至少包含 RSC + HRS + DIS。
5. running 广播只放 RSC + HRS，不放 CPS/CSC。
6. `notify_tick()` 在 running profile 下打包 RSC Measurement 和 HR Measurement。
7. `tests/ble_selftest.py` 增加 `--profile running`，校验 RSC/HRS/DIS、速度单位、步频范围、累计距离递增。

P0 建议默认参数：

| 配置 | 默认值 | 范围 |
|------|--------|------|
| 跑步速度 | 10.0 km/h | 3.0-25.0 km/h |
| 步频 | 170 spm | 120-220 spm |
| 坡度 | 0.0% | -5.0%-15.0%，P0 先存配置但不广播 |
| 心率 | 150 bpm 或沿用当前模型 | 40-220 bpm |

### 阶段 P1：FTMS Treadmill 数据

目标：提高对 Runn / 智能跑步机路径的兼容概率，提供坡度和训练时长。

实现要点：

1. 在 running GATT 表增加 FTMS Service。
2. `0x2ACC` Feature 按 treadmill 能力重写，不能复用当前 Indoor Bike feature。
3. 新增 `0x2ACD` Treadmill Data 通知，1s 周期输出速度、距离、坡度、心率、用时。
4. 如果同一 profile 同时有 RSC 和 FTMS，广播优先放 FTMS + HRS，RSC 可通过 GATT 发现；也可增加配置 `running_ble_mode=rsc|treadmill|both` 做 A/B 验证。
5. 提高或校验 `CONFIG_BT_NIMBLE_MAX_CCCDS`，避免 CCCD 数不足导致服务注册失败。

### 阶段 P2：FTMS 跑步机控制

目标：支持 App 下发目标速度/坡度，模拟智能跑步机。

实现要点：

1. Control Point 属性改为标准的 Write + Indicate，确认自行车 ERG 兼容性是否受影响。
2. 新增 Set Target Speed `0x02` 和 Set Target Inclination `0x03`。
3. Request Control 成功后才接受速度/坡度控制。
4. Start/Resume、Stop/Pause 更新训练状态，Stop 后速度输出归零或保持配置值需明确。
5. 控制点只改变模拟输出，不宣称能控制真实跑带；配网页和用户手册要写清楚。

---

## 8. 验收方案

### 8.1 自动化

- `idf.py build` 通过。
- host 单测覆盖跑步速度、步频、步幅、距离、坡度范围夹紧。
- `tests/ble_selftest.py --profile running`：
  - 扫描到设备名。
  - GATT 发现 RSC `0x1814`、HRS `0x180D`、DIS `0x180A`。
  - 订阅 RSC Measurement，连续 5 包速度在配置值 ±0.1 km/h 内。
  - 步频在配置值 ±2 spm 内。
  - Total Distance 单调递增。
  - HRS 通知正常。
- P1 增加 FTMS Treadmill Data 解析，确认速度、坡度、用时单位正确。
- P2 增加 Control Point 写入用例，确认响应码和输出值变化。

### 8.2 真机软件验证

Zwift：

1. 进入 Running 配对页。
2. 确认设备在 Speed Source / Footpod 或 Treadmill 类别出现。
3. 连接后确认速度显示接近配置值。
4. 连接 HRS 后确认心率显示。
5. 开始跑步，确认 avatar 按速度移动。
6. P1/P2 阶段再验证坡度和 controllable 是否被识别。

MyWhoosh：

1. 进入 Running 模式配对。
2. 确认设备能作为跑步传感器或跑步机出现。
3. 连接后确认速度、步频、心率稳定刷新。
4. 开始活动，确认跑步世界内速度生效。

### 8.3 风险判定

如果 Zwift/MyWhoosh 能扫描到但不显示在跑步配对列表，优先检查广播 UUID 是否包含 RSC/FTMS，而不是只看 GATT 表。很多 App 在配对页会先按广播服务过滤设备。

如果能配对但速度不动，优先检查 RSC Measurement 单位。RSC speed 是 1/256 m/s；FTMS Treadmill speed 是 0.01 km/h。两者不能混用。

如果 P2 控制点无响应，优先检查 Control Point CCCD 是否按 indication 配置。当前自行车 ERG 的 notify 响应在部分 App 可工作，但不是完整 FTMS 控制点语义。

---

## 9. 本次建议的实施边界

先做 P0，不同时做 P1/P2。理由：

- P0 的协议面小，主要是新增 RSC 打包和 profile 选择，便于快速用 Zwift/MyWhoosh 真机验证方向。
- 跑步 App 的最低闭环是速度，不是坡度或控制。
- P1/P2 会改 FTMS feature、control point、CCCD 上限和更多 App 行为，容易把首版验证复杂化。

P0 完成并确认 Zwift/MyWhoosh 都能驱动跑步后，再做 P1 的 FTMS Treadmill Data。P2 只有在确认目标软件确实会对通用 FTMS treadmill 下发速度/坡度控制时再实现。

---

## 10. 参考资料

- Zwift Support：Compatible Running Hardware，说明 Zwift 支持兼容跑步机、footpod、手表、智能鞋和 Runn 等跑步传感器。
- Zwift：RunPod 产品说明，说明 RunPod 通过 Bluetooth 配对并提供速度与步频。
- Zwift / NPE：Runn 相关说明，说明 Runn 输出速度、坡度和步频。
- MyWhoosh：Running enhancements 说明，提到 Stryd、Assault Runner Pro、Garmin HRM、Runn NPE 等跑步传感器。
- Bluetooth SIG：Running Speed and Cadence Profile / Service，定义 RSC Measurement、Feature、Sensor Location、SC Control Point。
- Bluetooth SIG：Fitness Machine Service，定义 Treadmill Data、Fitness Machine Feature、Fitness Machine Control Point、Fitness Machine Status。
