# 智能家居平台接入调研

本文调研 BikePower C 设备接入米家及其他智能家居平台的可行路径。
结论基于当前仓库实现、ESP32-C3 硬件约束，以及公开平台资料的可见信息。

## 当前设备基线

BikePower C 当前定位是 BLE 运动外设，而不是家庭 IoT 设备。

- 主控：ESP32-C3，4MB Flash。
- 固件：ESP-IDF v5.5.4 + NimBLE + FreeRTOS。
- 当前联网形态：常态只开 BLE；WiFi 只在用户长按 + 二次确认后进入 SoftAP 配网页。
- 当前 BLE 形态：Cycling Power、Heart Rate、Cycling Speed and Cadence、Fitness Machine、Device Information 等 GATT 服务。
- 当前连接模型：业务逻辑仍是单 BLE 连接模型。
- 当前 WiFi 约束：禁止开机自启 WiFi，所有 WiFi 启停必须经过 `radio_ctrl`，配网期间依赖 WiFi/BLE coex。
- 当前云能力：没有家庭 WiFi STA 常驻连接，没有 MQTT/HTTPS 长连接，没有云账号绑定，没有 OTA 在线升级。

这些约束决定了“直接把现有固件注册进米家 App”不是一个小改动。
现有固件最适合先做本地桥接或 Matter/MQTT 网关方案；官方米家接入属于产品化和认证工程。

## 平台路径对比

| 路径 | 是否能进入米家 App | 对当前固件改造量 | 平台门槛 | 适合阶段 |
|------|-------------------|------------------|----------|----------|
| 米家官方 WiFi/BLE 接入 | 可以 | 很高 | 企业开发者、产品创建、功能定义、SDK/模组、提测认证 | 量产产品 |
| 米家 BLE Spec 接入 | 可以 | 高 | 小米 IoT BLE Spec、三元组、提测认证 | 低功耗传感类产品化 |
| Matter over WiFi | 取决于米家 Matter 支持和品类映射 | 很高 | Matter SDK、认证、Matter 设备类型限制 | 跨生态产品化 |
| Home Assistant 本地桥接 | 不直接进米家，可再桥到 Matter | 低到中 | 用户自建 HA/网关 | 原型验证 |
| MQTT/HTTP 本地网关 | 不直接进米家，可经 HA/Node-RED/Matterbridge 转接 | 中 | 自建网关服务 | 开发者/高级用户 |
| 蓝牙广播被动采集 | 不直接进米家 | 低 | 需要旁路采集器 | 数据展示原型 |

## 路径一：米家官方接入

米家官方接入本质是“成为小米 IoT 平台产品”，不是单纯实现一个公开协议。
公开文档入口显示需要注册或加入企业组、创建产品、定义功能、完成硬件/固件研发、再走测试与发布流程。

### 1. WiFi+BLE 模组接入

公开文档中，WiFi+BLE 双模模组方案通常要求产品 MCU 与小米标准模组通过串口通信，由小米模组处理配网、云连接和米家协议。

对 BikePower C 的影响：

- 需要新增或更换硬件：外接小米认证模组，或改成小米认可的模组平台。
- 固件要新增串口协议层，把当前功率、踏频、心率、模式、连接状态映射成米家属性。
- 需要重新设计配网：米家 App 配网会替代当前 SoftAP 配网页的一部分能力。
- 当前“禁止开机自启 WiFi”的项目规则需要重新评估，因为米家设备通常需要常驻或按需 STA 联网。

优点是官方可发布进米家，用户体验最好。
缺点是硬件、平台、认证成本最高，不适合作为当前 ESP32-C3 固件的小步迭代。

### 2. BLE Spec 直连接入

小米 BLE 接入文档强调 2021-06-18 后创建的 BLE 产品属于 BLE Spec 类型，需要基于 Spec 格式开发，平台 SDK 同时包含旧 Object 和新 Spec 能力。

对 BikePower C 的影响：

- 现有 BLE GATT 是标准运动服务，不是米家 BLE Spec 模型。
- 需要新增米家要求的 BLE 服务、属性、认证、绑定、加密和数据模型。
- 需要拿到产品三元组或平台分配的身份材料。
- 需要经过米家产品测试，不能只靠本地实现验证。

优点是不用常驻 WiFi，理论上更贴合当前“BLE 常态运行”的设备形态。
缺点是 BikePower 当前输出的是运动协议数据，米家智能家居品类未必天然支持“功率计/虚拟骑行台”这种模型；最终可能只能映射成自定义传感器或健康/运动类设备，取决于平台品类开放程度。

## 路径二：Matter over WiFi

ESP32-C3 可以作为 Matter WiFi 设备开发，Espressif 的 esp-matter 文档也覆盖 ESP32-C3。
但 ESP32-C3 没有 802.15.4 radio，不能做 Matter Thread 终端，只能走 Matter over WiFi。

对当前固件的主要冲突：

- Matter 需要 WiFi STA 联网和 commissioning，当前固件只有用户触发的 SoftAP 配网。
- Matter SDK 体量较大，当前 4MB Flash、单 factory 分区、NimBLE 五服务和网页资源已经比较紧。
- Matter 设备类型和 cluster 需要选择合适的语义模型。BikePower 的“功率/踏频/心率/跑步速度”不是 Matter 常见的开关、灯、温湿度、插座、空调类设备。
- BLE 在 Matter 中主要用于配网 commissioning，当前 BLE 已经承担运动 GATT 外设角色；二者共存需要重新评估 NimBLE/Matter BLE 资源和连接模型。

如果目标是“同时进米家、Apple Home、Google Home”，Matter 是长期方向。
但对 BikePower，第一版不建议直接把 Matter 塞进现有固件；更稳的是先由外部网关把 BikePower 数据桥成 Matter 设备。

## 路径三：Home Assistant / 网关桥接

这是当前最现实的验证路线。
BikePower 保持现有 BLE 运动外设身份，由旁路网关读取 BLE 数据，再暴露到 Home Assistant、MQTT 或 Matterbridge。

可选结构：

```text
BikePower BLE
  -> 本地网关脚本 / Home Assistant / ESPHome BLE proxy
  -> MQTT 或 Home Assistant entities
  -> Matterbridge / HA Matter Bridge
  -> 米家或其他 Matter 控制器（取决于平台支持）
```

### 实现方式 A：网关脚本直接读 BLE

在树莓派、Mac mini、NAS 或常开电脑上运行 Python `bleak` 脚本：

- 扫描 `BikePower*`。
- 订阅 CPS/HRS/CSC/FTMS/RSC 通知。
- 解析功率、踏频、心率、速度。
- 发布 MQTT topic，例如 `bikepower/state`。
- Home Assistant 通过 MQTT Sensor 接入。

优点：

- 对固件零侵入或低侵入。
- 可复用当前 `tests/ble_selftest.py` 和 `tests/ble_broadcast_monitor.py` 的解析经验。
- 能先验证“智能家居里展示 BikePower 状态/触发自动化”是否有实际价值。

缺点：

- 需要额外网关常驻。
- 不能原生出现在米家，除非后续再走 Matterbridge 或其他桥接。

### 实现方式 B：新增 BikePower 本地 HTTP/MQTT 输出

让设备在用户明确开启的“家庭网络模式”下连接家中 WiFi，主动上报 MQTT/HTTP。

对当前固件的影响：

- 需要恢复或重新设计家庭 WiFi STA 配置、凭证保存、重连策略。
- 需要处理 WiFi/BLE coex 下的运动数据稳定性。
- 需要 TLS/证书或至少局域网 MQTT 鉴权。
- 会重新打开 v4.0.0 后已移除的家庭网络复杂度。

这条路线适合在明确需要“设备主动联网”之后再做，不适合作为第一步。

### 实现方式 C：ESPHome Bluetooth Proxy

ESPHome 的 Bluetooth Proxy 能把 BLE 覆盖扩展到 Home Assistant。
但它不是通用的 BLE GATT 解析器；Home Assistant 仍需要有对应集成来理解 BikePower 的运动服务数据。

如果采用这条路线：

- 可以用 ESP32-C3/ESP32 网关做 BLE proxy。
- 仍需在 Home Assistant 侧写自定义集成或通过外部脚本把数据转成 MQTT。
- 不建议让 BikePower 自己改刷 ESPHome，因为会丢失当前固件全部运动模拟逻辑。

## 推荐分阶段方案

### P0：先做本地网关 PoC

目标：不改固件，证明智能家居场景有用。

交付物：

- `tools/gateway/bikepower_ble_mqtt.py`
- MQTT topic 约定文档
- Home Assistant MQTT Sensor 示例
- 解析 CPS/HRS/CSC/FTMS/RSC 的单元测试或样例 payload 测试

验证：

- 设备 BLE 连接不断流。
- Home Assistant 能显示功率、踏频、心率、运动模式。
- 可基于功率/心率触发自动化，例如开风扇、切灯效、记录训练状态。

预估改造量：低。

### P1：做 Home Assistant 集成或 Matterbridge 映射

目标：把 BikePower 作为一组标准实体接入智能家居平台。

可映射实体：

- `sensor.bikepower_power_w`
- `sensor.bikepower_cadence_rpm`
- `sensor.bikepower_heart_rate_bpm`
- `sensor.bikepower_speed_kmh`
- `sensor.bikepower_mode`
- `binary_sensor.bikepower_connected`

如果要跨到 Matter，建议先由 Home Assistant 或 Matterbridge 对外暴露。
这样固件仍保持小而稳定。

预估改造量：中。

### P2：评估官方米家产品化

目标：决定是否值得走小米 IoT 官方接入。

需要先确认的问题：

- 米家平台是否开放适合 BikePower 的产品品类。
- 目标是“米家里展示传感器数据”，还是“被米家自动化控制训练状态”。
- 是否接受企业开发者、产品认证、硬件模组和测试周期。
- 是否要保留现有标准 BLE 运动服务，还是做双协议设备。

只有当这些问题明确后，才建议进入官方 SDK/模组接入。

### P3：Matter 原生固件

目标：设备自己成为 Matter over WiFi 节点。

建议只在以下条件同时满足时启动：

- 确认目标平台支持所需 Matter 设备类型或可接受自定义/近似映射。
- 确认 4MB Flash 和 RAM 足够，或切换到更合适硬件。
- 接受重做 WiFi STA、commissioning、凭证、分区和发布流程。
- 接受 BLE 运动服务与 Matter commissioning 的资源冲突风险。

对当前项目，这不是短期优先项。

## 产品语义建议

BikePower 不建议在智能家居里伪装成开关或灯。
更合理的语义是“运动状态传感器 + 自动化触发源”。

建议暴露的数据：

- 功率 W
- 踏频 rpm
- 心率 bpm
- 速度 km/h
- 运动类型：cycling / running
- 设备连接状态
- 训练活动状态：idle / active
- 当前模式：steady / road / erg / running

建议谨慎暴露的控制：

- 目标功率
- 跑步目标速度
- 启停训练状态

控制类能力必须保留本地安全边界，不能让云端自动化在用户不知情时突然改变训练负载。

## 风险清单

- 米家官方接入需要平台账号、企业/产品流程和认证，不是纯固件问题。
- Matter 原生接入会引入 WiFi STA 常驻、commissioning、证书/凭证、分区和内存压力。
- 当前项目刚移除家庭网络和 OTA，贸然恢复云连接会扩大维护面。
- ESP32-C3 单路 RF 下，BLE 运动数据稳定性优先级应高于智能家居上报。
- 智能家居平台的设备类型可能无法准确表达功率计/骑行台语义。
- 米家区域、品类、认证策略会变化，正式立项前必须以当时平台后台可创建的产品类型为准。

## 当前结论

短期推荐路线：

1. 先做 BLE 到 MQTT/Home Assistant 的外部网关 PoC。
2. 验证自动化场景和实体模型。
3. 如需要跨生态，再从 Home Assistant 或 Matterbridge 暴露 Matter。
4. 只有在明确要商业化进入米家 App 时，再启动小米 IoT 官方接入评估。

不建议当前直接在 BikePower 固件内实现米家官方协议或 Matter 原生节点。
这会把一个稳定的 BLE 运动设备改造成云联网 IoT 设备，改造面和认证成本都明显超过第一阶段收益。
