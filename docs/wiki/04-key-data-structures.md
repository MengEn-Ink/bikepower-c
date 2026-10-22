# 04 - 关键数据结构

本文档列出项目中最重要的数据结构及其字段含义。

---

## 应用层结构

### app_evt_t — 应用事件

**定义位置**：[app_events.h](../../main/app_events.h)

```c
typedef enum {
    APP_EVT_BTN_DOWN,         // 按钮按下
    APP_EVT_BTN_REPEAT,       // 长按自动重复（每 200ms）
    APP_EVT_BTN_UP,           // 按钮释放（附带按压时长）
    APP_EVT_CONN_CHANGED,     // BLE 连接状态变化（0/1）
    APP_EVT_ERG_TARGET,       // FTMS 目标功率（W）
    APP_EVT_ERG_CLEAR,        // FTMS 停止/复位
    APP_EVT_RUN_TARGET_SPEED, // 跑步目标速度（×100 km/h）
    APP_EVT_RUN_START,        // 跑步开始/恢复
    APP_EVT_RUN_STOP,         // 跑步暂停
    APP_EVT_RUN_RESET,        // 跑步复位
    APP_EVT_WIFI_DONE,        // WiFi 配网结束（保存/超时）
} app_evt_type_t;

typedef struct {
    app_evt_type_t type;
    int data;       // 事件参数（按压时长/连接状态/目标功率等）
} app_evt_t;
```

事件投递到 `s_app_queue`（深度 16），由主循环统一消费。

### board_desc_t — 板型描述

**定义位置**：[board_detect.c](../../main/board_detect.c)

```c
typedef struct {
    const char *name;            // 板型名（"generic-c3" / "m5stamp-c3"）
    int btn_pin;                 // 按钮 GPIO 号
    int led_pin;                 // LED GPIO 号
    led_drv_t led_drv;           // LED_DRV_GPIO 或 LED_DRV_SK6812
    led_active_level_t led_active; // GPIO LED 极性
} board_desc_t;
```

---

## 领域层结构

### power_engine_t — 功率引擎核心状态

**定义位置**：[power_engine.h](../../components/bikepower_domain/include/power_engine.h#L47-L74)

这是全局唯一的运行时状态对象，保存在 `app_main.c` 的 `s_engine` 静态变量中。

| 字段 | 类型 | 说明 |
|------|------|------|
| `base_power` | `int` | 基准功率（W），Steady 模式下按钮/网页调整此值 |
| `cadence_base` | `int` | 基准踏频（RPM） |
| `last_heartrate` | `int` | 最近一次对外输出的心率（BPM） |
| `user_heartrate` | `int` | 用户锁定心率（有效仅当 has_user_hr） |
| `has_user_hr` | `bool` | 是否锁定用户心率 |
| `power_float` | `int` | 功率浮动幅度（±W，Steady 模式） |
| `cadence_float` | `int` | 踏频浮动幅度（±RPM，Steady 模式） |
| `hr_float` | `int` | 锁定心率浮动幅度（±BPM） |
| `power_step` | `int` | 物理按钮调功步进（W），默认 20，可配 |
| `power_walk` | `fluct_walk_t` | 功率平滑游走状态 |
| `cadence_walk` | `fluct_walk_t` | 踏频平滑游走状态 |
| `mode` | `ride_mode_t` | 当前骑行模式（Steady/Road/ERG） |
| `erg_active_target` | `int` | FTMS ERG 目标功率（W） |
| `has_erg_target` | `bool` | 是否有活跃 ERG 目标 |
| `last_power` | `int` | 最近一次快照功率（用于模式切换初始化） |
| `last_cadence` | `int` | 最近一次快照踏频 |
| `profile` | `profile_t` | 用户档案（体重/FTP/阈值心率/配速机器人） |
| `led` | `led_config_t` | LED 自定义配置（各场景模式+颜色） |
| `hr` | `hr_state_t` | 心率模型内部状态 |
| `sport_profile` | `sport_profile_t` | 运动类型（骑行/跑步） |
| `broadcast_mask` | `int` | BLE 广播字段掩码（Bit0:功率,1:踏频,2:心率,3:跑步） |
| `running` | `running_engine_t` | 跑步引擎状态 |
| `snapshot` | `snapshot_t` | 最近一次快照输出 |
| `snapshot_count` | `long` | 快照计数器 |
| `dirty` | `bool` | 脏标记（待持久化） |
| `device_name` | `char[]` | BLE 设备名（最长 DEVICE_NAME_MAX-1） |

### snapshot_t — 单次输出快照

```c
typedef struct {
    int power;       // 功率（W）
    int cadence;     // 踏频（RPM）
    int heartrate;   // 心率（BPM）
    ride_mode_t mode; // 生成该快照的模式
} snapshot_t;
```

主循环每 50ms 调用 `power_engine_snapshot()` 更新 `s_engine.snapshot`，BLE 通知定时器每 1s 取最新值发送。

### fluct_walk_t — 有界平滑随机游走

```c
typedef struct {
    float value;  // 当前连续偏移量（相对基准值）
    int target;   // 当前目标偏移量（带内随机重采样）
    int hold;     // 当前目标剩余保持拍数
    bool init;    // 是否已初始化
} fluct_walk_t;
```

算法：每 `FLUCT_WALK_HOLD_LOOPS`（60拍≈3s）在 `[-amp, +amp]` 内重采样 target，然后按 `FLUCT_WALK_TAU`（0.8）一阶指数逼近 value，输出始终夹在带内。

### profile_t — 用户档案

**定义位置**：[profile.h](../../components/bikepower_domain/include/profile.h)

| 字段 | 类型 | 范围 | 默认 | 说明 |
|------|------|------|------|------|
| `weight_kg` | `int` | 20-200 | 70 | 体重（kg），Road 模式计算功率 |
| `ftp_w` | `int` | 50-500 | 260 | 功能阈值功率（W） |
| `thr_hr` | `int` | 100-200 | 170 | 阈值心率（BPM） |
| `ride_type` | `int` | 0-5 | 0 | 配速机器人索引（0=Coco...5=自定义） |
| `custom_wkg_low` | `float` | 1.0-7.0 | 2.5 | 自定义 W/kg 下限 |
| `custom_wkg_high` | `float` | 1.0-7.0 | 3.0 | 自定义 W/kg 上限 |
| `custom_pacer_name` | `char[]` | — | "自定义" | 自定义配速机器人昵称 |

### hr_state_t — 心率模型状态

**定义位置**：[hr_model.h](../../components/bikepower_domain/include/hr_model.h)

包含内部平滑值、HRV 相位、漂移量、对外输出值等，不对外暴露字段细节，通过 `hr_model_*` 函数操作。

### running_engine_t — 跑步引擎状态

**定义位置**：[running_engine.h](../../components/bikepower_domain/include/running_engine.h)

| 字段 | 类型 | 说明 |
|------|------|------|
| `target_speed_x100` | `int` | 目标速度（×100 km/h） |
| `current_speed_x100` | `int` | 当前速度（平滑后） |
| `cadence_spm` | `int` | 步频（steps per minute） |
| `cadence_float` | `int` | 步频浮动幅度 |
| `pace_float_sec` | `int` | 配速浮动秒数 |
| `mode` | `run_mode_t` | 手动/配速机器人模式 |
| `pacer` | `run_pacer_type_t` | 配速机器人类型 |
| `pace_low_sec` | `int` | 配速下限（秒/公里） |
| `pace_high_sec` | `int` | 配速上限（秒/公里） |
| `custom_name` | `char[]` | 自定义跑步配速名称 |
| 游走状态 | `fluct_walk_t` | 速度/步频浮动 |

### running_snapshot_t — 跑步快照

```c
typedef struct {
    int speed_x100_kmh;  // 瞬时速度（×100 km/h）
    int cadence_spm;      // 瞬时步频（spm）
    uint8_t inst_stride_length_cm; // 步长（cm）
    bool running;         // 是否在跑步状态
} running_snapshot_t;
```

### config_t — NVS/JSON 配置中间结构

**定义位置**：[config_store.h](../../components/bikepower_domain/include/config_store.h)

这是持久化和序列化用的结构，包含所有需要保存的字段（schema_ver、功率、踏频、模式、心率、体重、FTP、阈值心率、ride_type、自定义W/kg、设备名、LED 配置、运动 profile、广播掩码、跑步参数等），约 40 个字段。

通过 `config_to_engine()` 和 `engine_to_config()` 在 `config_t` 和 `power_engine_t` 之间转换。

### led_config_t — LED 自定义配置

**定义位置**：[led_pattern.h](../../components/bikepower_domain/include/led_pattern.h)

每个场景（silent/wait/conn/wifi/confirm/error）有 `mode`（慢闪/快闪/常亮/熄灭）和 `color`（RGB 三元组，仅 SK6812 生效）。

### device_template_t — BLE 设备信息模板

**定义位置**：[device_template.h](../../components/bikepower_domain/include/device_template.h)

| 字段 | 说明 |
|------|------|
| `alias` | 用户自定义别名 |
| `adv_name` | 广播名 |
| `device_name` | 应用到本设备的名称 |
| `manufacturer` | DIS 厂商字符串 |
| `model` | DIS 型号 |
| `firmware_rev` | DIS 固件版本 |
| `hardware_rev` | 硬件版本 |
| `software_rev` | 软件版本 |
| `last_addr` | 最近一次扫描到的地址 |
| `addr_type` | 地址类型 |
| `rssi` | 信号强度 |
| `service_flags` | 支持的服务标记位（功率/踏频/心率/FTMS/RSC） |

最多保存 5 个模板，存于 NVS namespace `ble_tpl` 的 `tpl0`-`tpl4`。
