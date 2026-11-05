# 03 - 领域层模块详解

领域层位于 `components/bikepower_domain/`，是纯 C 组件，**不依赖 ESP-IDF**，可在宿主机用 gcc 编译并运行单元测试。

---

## 设计原则

- **纯算法**：只关心"骑行数据怎么算"，不关心"数据发给谁"或"按钮怎么接"
- **可 host 单测**：`cd test && make` 即可编译运行，不需要 ESP32 硬件
- **无 ESP-IDF 依赖**：不 include `freertos/`、`esp_log.h`、`nvs_flash.h` 等
- **自包含随机数**：通过 `domain_rng` 提供随机源，host 测试可注入种子
- **防御性校验**：所有公开 API 都对输入做范围夹紧，不信任调用方

---

## power_engine.c — 功率/踏频/心率快照引擎

**头文件**：[power_engine.h](../../components/bikepower_domain/include/power_engine.h)

核心对象 `power_engine_t` 保存了所有运行时状态，详见 [04-关键数据结构](04-key-data-structures.md)。

### 骑行模式

| 模式 | 枚举值 | 说明 |
|------|--------|------|
| `RIDE_MODE_STEADY` | 0 | 固定模式：基准功率 + 双时间尺度有界浮动 |
| `RIDE_MODE_ROAD` | 1 | 配速机器人：在 W/kg 功率区间内连续变化 |
| `RIDE_MODE_ERG` | 2 | ERG 模式：FTMS 目标功率（自动切换） |

跑步模式由独立的 `running_engine_t` 处理，通过 `sport_profile` 切换骑行/跑步。

### 关键函数

| 函数 | 说明 |
|------|------|
| `power_engine_init(e)` | 初始化到默认值（260W/90RPM/150BPM/Steady） |
| `power_engine_snapshot(e)` | **核心函数**：按当前模式生成本拍 `snapshot_t` |
| `power_engine_adjust_power(e, delta)` | 短按/自动重复调功（delta = ±power_step） |
| `power_engine_set_power(e, value)` | 直接设置基准功率 |
| `power_engine_set_mode(e, mode)` | 切换模式，调用 `reset_mode_state()` |
| `power_engine_set_erg_target(e, target)` | 设置非持久 ERG 目标（自动切到 ERG 模式，不置 dirty） |
| `power_engine_clear_erg_target(e)` | 清除 ERG 目标，回到 Steady |
| `power_engine_is_dirty(e)` / `clear_dirty(e)` | 脏标记查询/清除（用于 NVS 落盘） |

### Steady 模式（build_steady）

```
power_offset = ride_dynamics(&power_dynamics, power_float)
output_power = base_power + power_offset
output_cadence = cadence_base + ride_dynamics(&cadence_dynamics) + weak_coupling(power_offset)
output_hr = 有用户心率 ? hr_model_steady_user(user_hr, hr_float)
                        : 功率负荷滤波 → FTP映射 → 心率平滑+HRV
```

**双时间尺度动态**（`ride_dynamics_step`）：
- 慢层占幅度 80%，每 6-18 秒选择新航点并用 smoothstep 连续过渡
- 快层每秒以 `alpha=0.70` 吸收一次有界踩踏/传感器扰动，该值按 FIT 的 1Hz 秒差和 AC1 拟合
- 两层合成结果始终夹在配置浮动带内，`amp=0` 时严格恒定

### Road 模式（build_road）

- 按 `ride_type` 索引 `ROAD_PACERS[]` 表获取 W/kg 区间
- 自定义档（`ride_type=5`）使用 profile 中的 `custom_wkg_low/high`
- W/kg 上下界先换算成功率区间，再以区间中点为中心运行分层动态
- 心率由功率推导（同 Steady 的非锁定心率路径）

### ERG 模式（build_erg）

- 目标功率优先取 `erg_active_target`，否则退回 `base_power`
- 叠加目标 ±5W 的相关功率波动（非硬钉死）
- 踏频保留小幅动态并与功率弱耦合
- 心率仍走负荷功率 + FTP 映射 + 平滑 + HRV 路径

---

## hr_model.c — 心率模型

**头文件**：[hr_model.h](../../components/bikepower_domain/include/hr_model.h)

心率不是简单的功率×系数，而是五层叠加：

### 1. 功率负荷滤波（hr_model_smooth_load）

- 上升时间常数 8 秒，下降时间常数 15 秒
- 先过滤短时踩踏尖峰，再进入 FTP 心率映射
- 与后续 30/60 秒心率平滑组成轻量二阶响应

### 2. FTP 6 区映射（hr_model_ftp_zone）

| 功率/FTP | hr_pct | 说明 |
|----------|--------|------|
| ≤55% | HR_ZONE_RECOVERY_BASE + slope × pct | 恢复区 |
| 55-75% | 线性插值 | 有氧耐力 |
| 75-90% | 线性插值 |  tempo |
| 90-105% | 线性插值 | 阈值 |
| 105-120% | 线性插值 | VO2max |
| >120% | 上限值 | 无氧 |

`hr_pct × thr_hr` 后，以 `thr_hr × 85%` 为锚点把相对偏差压缩到 70%，再经 `hr_clamp()` 限制到 `[HR_MIN=60, HR_MAX=200]`。该压缩避免瞬时功率和目标心率过度同步。

### 3. 一阶指数平滑（hr_model_smooth）

```
HR(t) = HR_old + alpha × (HR_target - HR_old)
alpha = 1 - exp(-dt / tau)
dt = 0.05s（50ms 节拍）
```

上下行时间常数不对称：
- `HR_TAU_RISE = 30s`（升心率快）
- `HR_TAU_FALL = 60s`（降心率慢，符合真实体感）

### 4. HRV 变异（hr_model_variation）

每秒左右更新一次缓存，叠加项：
- **RSA**（呼吸性窦性心律不齐）：振幅 `HRV_RSA_AMPLITUDE=2.0 BPM`，频率 `0.12Hz`
- **Mayer wave**：振幅 `1.0 BPM`，周期 `10s`
- **慢漂移**：速率 `0.05 BPM/min`，上限 `5 BPM`
- **独立慢游走**：在 ±7 BPM 随机航点间连续过渡，每段 30-90 秒

### 5. 输出限速（hr_model_emit）

- 内外值差超过 `HR_EMIT_DEADBAND=2 BPM` 时，每拍只变动 1 BPM
- 每 20 拍有低概率跳变 2 BPM（`HR_EMIT_JUMP_BPS=200` 控制概率）
- 模拟真实心率设备的渐进变化，而非跳变

### 关键函数

| 函数 | 说明 |
|------|------|
| `hr_model_init(state, initial_hr)` | 初始化心率状态 |
| `hr_model_smooth_load(state, power)` | 瞬时功率转生理负荷功率 |
| `hr_model_smooth(state, target_hr)` | 非对称心率平滑 |
| `hr_model_variation(state)` / `hr_model_emit(state, raw)` | HRV 叠加与对外限速 |
| `hr_model_steady_user(state, user_hr, float_amp)` | 锁定心率路径（带波动） |
| `hr_model_reset_variation(state)` | 模式切换时重置周期缓存、漂移和慢游走状态 |

---

## running_engine.c — 跑步模式引擎

**头文件**：[running_engine.h](../../components/bikepower_domain/include/running_engine.h)

跑步模式独立于骑行功率引擎，生成 RSC（Running Speed and Cadence）数据。

### 关键数据结构

- `running_engine_t`：跑步引擎状态（速度、步频、配速浮动等）
- `running_snapshot_t`：单次跑步快照（speed_x100_kmh、cadence_spm 等）
- `run_pacer_type_t`：跑步配速机器人类型

### 跑步模式

| 模式 | 说明 |
|------|------|
| `RUN_MODE_MANUAL` | 手动设定速度 |
| `RUN_MODE_PACER` | 配速机器人（按配速区间） |

### 关键函数

| 函数 | 说明 |
|------|------|
| `running_engine_init(e)` | 初始化跑步引擎 |
| `running_engine_snapshot(e, dt)` | 生成跑步快照（含平滑浮动） |
| `running_engine_set_speed(e, speed_x100)` | 设置目标速度（×100 km/h） |
| `running_engine_set_cadence(e, spm)` | 设置步频（steps per minute） |
| `running_engine_reset(e)` | 重置跑步状态（FTMS Reset 时） |

---

## profile.c — 用户档案与配速机器人

**头文件**：[profile.h](../../components/bikepower_domain/include/profile.h)

### profile_t 字段

| 字段 | 范围 | 默认 | 说明 |
|------|------|------|------|
| `weight_kg` | 20-200 | 70 | 体重（kg） |
| `ftp_w` | 50-500 | 260 | 功能阈值功率（W） |
| `thr_hr` | 100-200 | 170 | 阈值心率（BPM） |
| `ride_type` | 0-5 | 0 | 配速机器人索引 |
| `custom_wkg_low` | 1.0-7.0 | 2.5 | 自定义 W/kg 下限 |
| `custom_wkg_high` | 1.0-7.0 | 3.0 | 自定义 W/kg 上限 |
| `custom_pacer_name` | 字符串 | "自定义" | 自定义配速机器人昵称 |

### 配速机器人表（ROAD_PACERS）

| 索引 | 名称 | W/kg 区间 | 说明 |
|------|------|-----------|------|
| 0 | Coco | 2.4-2.8 | 轻松骑 |
| 1 | Yumi | 2.7-3.1 | 有氧耐力 |
| 2 | Jacques | 3.0-3.5 | tempo |
| 3 | Genie | 3.5-3.9 | 阈值 |
| 4 | Constance | 4.0-4.4 | VO2max |
| 5 | 自定义 | 1.0-7.0（可调） | 用户自定义 |

### 关键函数

| 函数 | 说明 |
|------|------|
| `profile_init(p)` | 初始化为默认值 |
| `profile_validate(p)` | 范围校验夹紧 |
| `profile_get_pacer(p, idx)` | 获取配速机器人描述 |

---

## power_bridge.c — 三设备配置与功率校正

**头文件**：[power_bridge.h](../../components/bikepower_domain/include/power_bridge.h)

桥接配置固定最多保存三台设备，设备身份由 `addr_type + addr[6]` 共同组成。
校验会删除无效地址、控制字符名称和重复身份，并压紧设备数组。全部设备共享
同一组原值、固定瓦数偏移或百分比校正；原始功率小于等于 0 时直接返回 0，
不会因正偏移产生虚假功率。

NVS bridge wire v2 使用 `[version][source][count][calibration][watts][percent]`
头部和变长设备项；解码继续兼容 wire v1 单设备格式。CPS 样本要求两份合法
通知后生效，曲柄差分处理 16 位回绕，3 秒过期或断线后旧样本失效。

## bridge_failover.c — 主备恢复纯状态机

**头文件**：[bridge_failover.h](../../components/bikepower_domain/include/bridge_failover.h)

状态机不依赖 ESP-IDF 或 NimBLE，只输出 connect/stop/persist_recent 动作：

- 冷启动按最近成功设备优先，再按保存顺序定向连接。
- 异常断连后开启 60 秒快速窗口，最多遍历全部设备两轮。
- 全部失败后进入模拟降级，持久配置来源保持真实。
- 降级期间每 60 秒轮转一台已保存设备做定向探测，不启动后台扫描。
- 任一设备产生两份合法样本后恢复真实输出，并提示应用层更新最近成功身份。

---

## config_store.c — 配置 Schema 管理

**头文件**：[config_store.h](../../components/bikepower_domain/include/config_store.h)

负责配置的默认值、校验、schema 迁移和 JSON 序列化。

### 当前 Schema 版本

`CONFIG_VERSION = 14`

### 迁移路径

| 从版本 | 到版本 | 迁移操作 |
|--------|--------|----------|
| v6 | v14 | 补 `run_pace_float_sec` 默认值并逐级迁移 |
| v7 | v14 | 补 `run_cadence_float` 默认值并逐级迁移 |
| v8 | v14 | 补 `broadcast_mask = BROADCAST_FIELD_ALL` 并逐级迁移 |
| v9 | v14 | 补跑步参数（mode/pacer/pace 范围/custom_name） |
| v10 | v14 | 先进入 v11，再继续逐级迁移 |
| v11 | v12 | `power_float=5`（历史默认）改为 50，其他自定义值不变 |
| v12 | v13 | 补默认桥接配置，保持模拟来源且无绑定 |
| v13 | v14 | 单设备身份迁移到三设备表槽位 0，共享校正保持不变 |
| 未知 | v14 | 回退到默认配置 |

### config_data_t 字段

`config_data_t` 是 NVS 存储和 JSON 序列化的中间结构。桥接字段包含
`bridge_devices` 对象数组和共享校正；序列化仍保留槽位 0 的旧单设备别名，
用于兼容既有客户端。

### 关键函数

| 函数 | 说明 |
|------|------|
| `config_store_defaults(c)` | 填充默认值 |
| `config_store_validate(c)` | 范围夹紧，返回是否有修正 |
| `config_store_migrate(c)` | schema 版本迁移（修改 c->schema_ver） |
| `config_store_serialize(c, buf, len)` | 序列化为 JSON 字符串 |
| `config_store_deserialize(json, c)` | 从 JSON 反序列化；schema v14 要求提供合法 `bridge_devices`，其他缺失字段采用默认值 |

---

## json_miniparse.c — 轻量 JSON 解析

**头文件**：[json_miniparse.h](../../components/bikepower_domain/include/json_miniparse.h)

用于配置 JSON 的字段与对象数组提取，**无动态内存分配**。生产 HTTP 请求体由 cJSON 解析，再由配置层处理。

### 特性

- 简单字段提取支持工具生成格式的字符串与整数；字符串提取不解码转义
- 对象数组遍历会跳过字符串转义及嵌套对象，按索引复制受输出缓冲区大小限制
- 配置层另行校验数组最多三项、设备身份和名称，并处理名称反转义
- 不构建完整 DOM，适合嵌入式场景

### 关键函数

| 函数 | 说明 |
|------|------|
| `json_get_str(json, key, out, out_len)` | 提取简单字符串，不解析转义 |
| `json_get_int(json, key)` | 提取整数，缺失返回 0 |
| `json_get_object_array_count(json, key)` | 返回对象数组元素数；缺失为 -1，格式错误为 -2 |
| `json_get_object_array_item(json, key, index, out, out_len)` | 提取指定对象元素 |

---

## 其他纯逻辑模块

### button_logic.c / button_debounce.c

按钮纯逻辑，与硬件 GPIO 解耦：
- `button_debounce`：边沿去抖状态机（30ms 窗口）
- `button_logic`：短按/长按/自动重复/确认窗口语义

### led_pattern.c

LED 模式与颜色映射纯逻辑：
- 骑行/跑步不同状态的默认 LED 模式
- 颜色值（RGB）与模式关联

### device_template.h / device_analysis.c

- `device_template_t`：扫描、上游 DIS 读取与设备身份复制共用的数据结构；不提供旧门户模板库的增删和 JSON 接口
- `device_analysis`：外部 BLE 设备运动数据解析（CPS/HRS/CSC/FTMS/RSC 通知解析）

### domain_rng.c

领域层随机数抽象：
- host 单测可注入固定种子实现可复现测试
- 固件侧使用 `esp_random()`（在 nvs_store/ble 层桥接）

---

## 常量中心：domain_config.h

领域层所有常量集中在 [domain_config.h](../../components/bikepower_domain/include/domain_config.h)，bikepower_config.h 通过 `#include "domain_config.h"` 引用：

| 类别 | 常量 |
|------|------|
| 功率 | `POWER_MIN=0`, `POWER_MAX=2000`, `DEFAULT_POWER=260`, `POWER_STEP_DEFAULT=20` |
| 踏频 | `CADENCE_MIN=20`, `CADENCE_MAX=120`, `DEFAULT_CADENCE=90` |
| 心率 | `HR_MIN=60`, `HR_MAX=200`, `DEFAULT_HEARTRATE=150` |
| 浮动 | `POWER_FLOAT_LEGACY_DEFAULT=5`, `POWER_FLOAT_DEFAULT=50`, `CADENCE_FLOAT_DEFAULT=8`, `HR_FLOAT_DEFAULT=2` |
| 心率模型 | `HR_TAU_RISE=30`, `HR_TAU_FALL=60`, `HRV_RSA_AMPLITUDE=2.0` 等 |
| 随机游走 | `FLUCT_WALK_HOLD_LOOPS=60`, `FLUCT_WALK_TAU=0.8f` |
| 跑步 | `RUN_SPEED_X100_MIN/MAX`, `RUN_CADENCE_MIN/MAX` 等 |
| 配置 | `CONFIG_VERSION=14`, `DEVICE_NAME_MAX=24` |
| 桥接 | `BRIDGE_DEVICE_MAX=3`, `BRIDGE_WIRE_VERSION=2`, `BRIDGE_FAST_WINDOW_MS=60000`, `BRIDGE_BACKGROUND_INTERVAL_MS=60000` |
| 节拍 | `HR_LOOP_INTERVAL_MS=50` |
| 广播 | `BROADCAST_FIELD_POWER/CADENCE/HR/RUNNING`, `BROADCAST_FIELD_ALL` |
| 运动 profile | `SPORT_PROFILE_CYCLING=0`, `SPORT_PROFILE_RUNNING=1` |
