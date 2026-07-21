# 跑步模式完整升级方案

> 状态：历史方案归档，仅供追溯，不代表当前 v4.1.0+ 实现。当前实现以 RSC 速度、步频、心率和 FTMS Treadmill 核心数据为主，已移除跑步坡度配置、NVS key、Portal UI/API 和 Set Target Inclination 控制点。
> 目标版本：v4.0.x 后续迭代。
> 范围：跑步输出来源、网页交互、配置持久化、RSC 数据面、FTMS Treadmill Data、FTMS 跑步控制点、测试与发布门禁。
> 前置约束：不得破坏现有骑行 CPS/HRS/CSC/FTMS Indoor Bike/ERG 行为；WiFi/BLE 射频共用规则不变；BLE profile 切换仍由主任务上下文串行处理。

---

## 1. 目标

本次升级把跑步模式从“固定配速 + 步频 + 心率”的基础形态，提升为与骑行模式同等级的完整体验：

- 骑行模式继续保留“固定功率 / 配速机器人 / 自定义 W/kg / ERG 控制”。
- 跑步模式新增“固定配速 / 跑步配速模板 / 自定义配速区间”。
- 跑步 BLE profile 从当前 RSC + HRS + DIS 扩展为 RSC + HRS + FTMS Treadmill + DIS。
- 跑步控制点支持 App 下发目标速度、坡度、开始、暂停和复位，但必须与骑行 ERG 控制点隔离。
- 网页、NVS、mock、host 单测、BLE 自测、release 门禁同步覆盖，避免配置漂移。

本方案一次性覆盖 P0.5、P1、P2，但实现和提交必须分批，保证每一层都能独立验证。

---

## 2. 当前状态

### 已具备能力

- `running_engine` 已可生成速度、步频、步幅、累计距离和用时快照。
- 跑步 profile 已注册 RSC、HRS、DIS。
- 网页端已有骑行/跑步切换，跑步页支持配速、配速浮动、步频、步频浮动、心率联动/锁定。
- `broadcast_mask` 已复用为功率/配速、踏频/步频、心率三类输出开关。
- `tests/ble_selftest.py --profile running` 已能验证 RSC + HRS + DIS。

### 主要缺口

- 跑步没有和骑行 `ride-source-picker` 对等的“输出来源”抽象。
- 跑步没有预设模板、自定义模板和持久化模型。
- 跑步 profile 没有 FTMS Treadmill Data `0x2ACD`，部分 App 可能只按 footpod/RSC 路径识别。
- 跑步控制点尚未支持目标速度/坡度，无法覆盖智能跑步机控制路径。
- FTMS 控制点当前主要服务骑行 ERG；跑步控制需要避免影响现有骑行控制点兼容性。

---

## 3. 用户体验设计

### 3.1 跑步输出来源

跑步首页新增 `run-source-picker`，与骑行输出来源保持一致的胶囊条交互。

建议来源：

| 来源 | 配速范围 | 用途 |
|------|----------|------|
| 固定配速 | 用户直接填写 | 当前默认路径，最可控 |
| 慢跑 | 6:30 - 7:30 /km | 低强度恢复 |
| 有氧 | 5:30 - 6:30 /km | 稳定有氧 |
| 节奏 | 4:30 - 5:15 /km | 中高强度节奏跑 |
| 间歇 | 3:30 - 4:15 /km | 高强度间歇 |
| 自定义 | 用户配置上下限 | 用户自定义训练档 |

固定配速模式保留当前输入方式。模板模式隐藏固定配速输入，展示当前模板的配速区间、折算速度和预计心率区间。自定义模式显示名称、配速下限、配速上限、默认步频。

### 3.2 跑步参数含义

- 配速：仍以 `分:秒/km` 为用户主输入。
- 速度：内部存储为 `km/h * 100`，用于 RSC 和 FTMS Treadmill。
- 配速区间：存储为秒/km，避免上下限换算误差。
- 步频：默认 170 spm，可随模板或用户配置。
- 步频浮动：与当前实现一致，在有限范围内平滑游走。
- 心率：默认随跑步强度联动；锁定后按用户心率输出并可应用心率浮动。

### 3.3 网页布局

跑步首页结构调整为：

1. 运动切换：骑行 / 跑步。
2. 广播数据项：配速 / 步频 / 心率。
3. 跑步输出来源胶囊条。
4. 固定或模板摘要。
5. 步频卡片。
6. 心率卡片。
7. 保存配置 / 保存并关闭 WiFi。

原则：

- 保持手机 390 x 844 视口可用。
- 不新增说明书式长文案。
- 固定模式才显示配速输入和常用配速卡片。
- 模板模式显示紧凑摘要，不占用过多高度。

---

## 4. 领域层设计

### 4.1 新增枚举与常量

在 `domain_config.h` 增加：

```c
typedef enum {
    RUN_MODE_FIXED = 0,
    RUN_MODE_PACER = 1,
} run_mode_t;

typedef enum {
    RUN_PACER_JOG = 0,
    RUN_PACER_AEROBIC,
    RUN_PACER_TEMPO,
    RUN_PACER_INTERVAL,
    RUN_PACER_CUSTOM,
} run_pacer_type_t;
```

新增范围常量：

- `RUN_PACE_SEC_MIN`：144，等价 2:24 /km，25 km/h。
- `RUN_PACE_SEC_MAX`：1200，等价 20:00 /km，3 km/h。
- `RUN_CUSTOM_NAME_MAX`：32。
- `RUN_INCLINE_X10_MIN`：-100，等价 -10.0%。
- `RUN_INCLINE_X10_MAX`：150，等价 15.0%。
- `RUN_INCLINE_X10_DEFAULT`：0。

预设模板使用只读表：

```c
typedef struct {
    const char *name;
    int pace_low_sec;
    int pace_high_sec;
    int cadence_spm;
} run_pacer_t;
```

### 4.2 `running_engine` 扩展

`running_engine_t` 新增：

- `run_mode_t mode`
- `run_pacer_type_t pacer_type`
- `int pace_low_sec`
- `int pace_high_sec`
- `char custom_name[RUN_CUSTOM_NAME_MAX]`
- `int incline_x10`
- `running_walk_t pacer_pace_walk`

固定模式：

- 按当前 `speed_x100_kmh` 和 `pace_float_sec` 输出。

模板模式：

- 从模板或自定义区间得到 `[pace_low_sec, pace_high_sec]`。
- 在区间内使用平滑随机游走产生目标配速。
- 转为 `speed_x100_kmh` 后更新 RSC/FTMS 输出。
- 步频使用模板默认值，可继续叠加步频浮动。

### 4.3 心率联动

继续使用 `power_engine_running_heartrate()` 的思路，把跑步速度换算为等效功率后走 FTP/阈值心率模型。

补强点：

- 模板模式下使用当前输出速度计算等效强度。
- 心率锁定时直接使用固定用户心率和 `hr_float`。
- 只开启心率广播时，按钮直接调心率，保持现有语义。

---

## 5. 配置与迁移设计

### 5.1 新增配置字段

`config_data_t` 新增：

| 字段 | 类型 | 说明 |
|------|------|------|
| `run_mode` | int | fixed / pacer |
| `run_pacer_type` | int | jog/aerobic/tempo/interval/custom |
| `run_pace_low_sec` | int | 自定义或当前模板下限 |
| `run_pace_high_sec` | int | 自定义或当前模板上限 |
| `run_custom_name` | char[] | 自定义跑步档名称 |
| `run_incline_x10` | int | 坡度，0.1% 单位，默认 0 |

### 5.2 NVS key

新增短 key，均保持 15 字符以内：

| key | 字段 |
|-----|------|
| `rmode` | `run_mode` |
| `rpacer` | `run_pacer_type` |
| `rplo` | `run_pace_low_sec` |
| `rphi` | `run_pace_high_sec` |
| `rcname` | `run_custom_name` |
| `rincl` | `run_incline_x10` |

### 5.3 schema 迁移

`CONFIG_VERSION` 从 9 升到 10。

迁移规则：

- v9 -> v10：保留已有跑步速度、配速浮动、步频、步频浮动；新增字段填默认。
- 未知旧版本：沿用当前策略，回退默认配置。
- `tests/check_config_consistency.py` 必须同步检查新 key、文档和 mock 字段。

---

## 6. BLE 协议设计

### 6.1 跑步 profile GATT 表

跑步模式目标服务：

- RSC `0x1814`
- HRS `0x180D`
- FTMS `0x1826`
- DIS `0x180A`

FTMS 跑步特征：

| 特征 | UUID | 属性 | 说明 |
|------|------|------|------|
| Fitness Machine Feature | `0x2ACC` | Read | 跑步机能力位 |
| Treadmill Data | `0x2ACD` | Read + Notify | 速度、距离、坡度、心率、用时 |
| Control Point | `0x2AD9` | Write + Indicate/Notify | 速度/坡度/开始/暂停/复位 |
| Status | `0x2ADA` | Read + Notify | 当前训练状态 |

注意：当前控制点响应使用 notify。实现时先保持现有骑行兼容；跑步控制点新增 indication 能力需实测。如果 NimBLE/客户端兼容有风险，第一版可以保留 notify 并用 selftest 覆盖，后续再独立改 indication。

### 6.2 广播策略

跑步广播包仍优先放两个 16-bit UUID：

- RSC `0x1814`
- HRS `0x180D`

FTMS `0x1826` 不放在主广播包，避免 31 字节预算挤压名称；完整能力由 GATT 发现获取。若后续真机验证发现 App 必须在广播阶段看到 FTMS，再评估把 HRS 移到 scan response 或使用不同跑步子模式。

### 6.3 Treadmill Data 打包

新增 `FTMS_TREADMILL_CHAR_UUID = 0x2ACD`。

建议字段：

- Instantaneous Speed：0.01 km/h。
- Total Distance：meter，按规范使用 24-bit。
- Inclination：0.1%。
- Ramp Angle Setting：0.1 degree，可由坡度近似计算或置 0。
- Heart Rate：bpm。
- Elapsed Time：second。

首版坡度默认 0，网页可编辑但不参与心率强度计算，避免把坡度模型复杂化。

### 6.4 跑步控制点

新增 OpCode：

| OpCode | 名称 | 行为 |
|--------|------|------|
| `0x02` | Set Target Speed | 参数 uint16，0.01 km/h，更新跑步速度并切到固定配速 |
| `0x03` | Set Target Inclination | 参数 sint16，0.1%，更新坡度 |
| `0x07` | Start/Resume | 标记训练运行，继续通知 |
| `0x08` | Stop/Pause | 标记暂停，速度可保持但状态变为 paused |
| `0x01` | Reset | 清累计距离和用时 |

隔离规则：

- cycling profile 下 `0x05 Set Target Power` 保持现有 ERG 逻辑。
- running profile 下 `0x05` 返回 not supported。
- running profile 下 `0x02/0x03` 生效。
- 控制点回调不直接改领域状态，只投递 app event，由 `app_main` 主任务上下文应用，延续现有 NimBLE 回调安全规则。

---

## 7. 应用层与事件设计

新增事件：

- `APP_EVT_RUN_TARGET_SPEED`
- `APP_EVT_RUN_TARGET_INCLINE`
- `APP_EVT_RUN_START`
- `APP_EVT_RUN_STOP`
- `APP_EVT_RUN_RESET`

`ble_bikepower.c` 只解析控制点并返回同步结果码。真正的状态修改交给 `app_main`：

1. NimBLE 回调解析参数。
2. 校验范围。
3. 入队事件。
4. `app_main` 调用 `power_engine_set_run_speed()`、`power_engine_set_run_incline()` 等。
5. 下一轮 50ms tick 更新 `running_engine_snapshot()`。
6. BLE 1s notify 发送 RSC + HRS + FTMS Treadmill。

这样避免在 NimBLE 回调里直接触碰复杂状态或重启 BLE。

---

## 8. 网页/API 设计

### 8.1 `/api/state`

新增字段：

- `run_mode`
- `run_pacer_type`
- `run_pacers`
- `run_pace_low_sec`
- `run_pace_high_sec`
- `run_custom_name`
- `run_incline_x10`

`run_pacers` 返回预设模板列表，类似骑行 `pacers`：

```json
[
  {"id":"jog","name":"慢跑","pace_low_sec":390,"pace_high_sec":450,"cadence":165},
  {"id":"aerobic","name":"有氧","pace_low_sec":330,"pace_high_sec":390,"cadence":170}
]
```

### 8.2 `/api/config`

允许保存新增字段。非法字段按领域层范围夹紧或拒绝，现有字段行为不变。

### 8.3 前端交互

新增函数：

- `renderRunSourcePicker()`
- `onRunSourceSelect(source)`
- `updateRunPacerInfo()`
- `onRunCustomPaceAdj(which, delta)`
- `onRunCustomNameInput()`
- `paceSecText(sec)`
- `speedTextFromPaceSec(sec)`

保持 iOS Captive Portal WebView 兼容：不用模块、不用复杂动态依赖。

---

## 9. 测试计划

### 9.1 TDD 顺序

1. `test/test_domain.c`
   - 跑步模板默认值。
   - 固定模式配速输出。
   - 模板模式在配速区间内平滑游走。
   - 自定义模板上下限校验。
   - 控制点速度/坡度范围校验。
   - schema v9 -> v10 迁移。

2. `tests/test_portal_config_save.py`
   - 保存跑步模板、自定义配速、坡度后 `/api/state` 回显。

3. `tests/check_portal_structure.py`
   - 跑步输出来源 DOM、JS 函数、字段存在。
   - 旧固定跑步页面不被误删。

4. `tests/check_config_consistency.py`
   - 新 NVS key、CONFIG_VERSION、文档字段同步。

5. `tests/ble_selftest.py --profile running`
   - RSC + HRS + DIS 保持通过。
   - 新增 FTMS Treadmill Data 通知解析。
   - 写入 Set Target Speed 后速度收敛。
   - 写入 Set Target Inclination 后 Treadmill Data 反映坡度。
   - Start/Stop/Reset 响应码正确。

### 9.2 构建与真机

必须通过：

- `python3 scripts/tools/quality_gate.py`
- `./scripts/flash_latest.sh --board generic --capture-seconds 25`
- `BIKEPOWER_EXPECT_BOARD=generic-c3 python3 tests/ble_selftest.py`
- `BIKEPOWER_EXPECT_BOARD=generic-c3 python3 tests/ble_selftest.py --profile running`

建议补充：

- 用 nRF Connect 检查跑步 GATT 表。
- 用 Zwift Run / MyWhoosh 实机观察设备分类和配速驱动。
- 如果 App 对 FTMS Treadmill 广播有要求，再调整广播 UUID 策略。

---

## 10. 实施分批

### 批次 1：领域层和配置

文件范围：

- `components/bikepower_domain/include/domain_config.h`
- `components/bikepower_domain/include/running_engine.h`
- `components/bikepower_domain/running_engine.c`
- `components/bikepower_domain/include/config_store.h`
- `components/bikepower_domain/config_store.c`
- `main/bikepower_config.h`
- `main/nvs_store.c`
- `test/test_domain.c`
- `tests/check_config_consistency.py`

完成标准：

- host 单测红绿通过。
- 配置迁移和 JSON 往返通过。
- NVS key 长度检查通过。

### 批次 2：网页/API/mock

文件范围：

- `main/wifi_portal.c`
- `main/web/index.html`
- `main/web/app.js`
- `main/web/app.css`
- `tests/mock_server.py`
- `tests/test_portal_config_save.py`
- `tests/check_portal_structure.py`

完成标准：

- 网页保存回显通过。
- 门户结构和 JS 语法门禁通过。
- 手机视口文本不溢出。

### 批次 3：BLE FTMS Treadmill Data

文件范围：

- `main/ble_bikepower.c`
- `main/ble_bikepower.h`
- `main/app_main.c`
- `tests/ble_selftest.py`
- `docs/dev/01-technical-reference.md`

完成标准：

- cycling profile 自测仍通过。
- running profile RSC/HRS/DIS/FTMS Treadmill 自测通过。
- GATT/CCCD 数量不超配置。

### 批次 4：FTMS 跑步控制点

文件范围：

- `main/ble_bikepower.c`
- `main/app_events.h`
- `main/app_main.c`
- `components/bikepower_domain/*`
- `tests/ble_selftest.py`

完成标准：

- Set Target Speed/Inclination 生效。
- Start/Stop/Reset 响应码正确。
- cycling ERG 不回归。

### 批次 5：文档与发布门禁

文件范围：

- `docs/dev/01-technical-reference.md`
- `docs/dev/03-test-coverage-analysis.md`
- `docs/dev/04-running-mode-compatibility.md`
- `docs/user/02-user-manual.md`
- `PROGRESS.md`
- `CHANGELOG.md`

完成标准：

- 文档与代码一致性门禁通过。
- release 构建前不包含过时字段描述。

---

## 11. 风险与回滚

### 风险 1：FTMS Treadmill 广播发现不稳定

缓解：

- 首版广播仍放 RSC + HRS，保证 footpod 路径。
- 若目标 App 必须广播 FTMS，再做单独实验分支调整。

### 风险 2：控制点 indication/notify 兼容

缓解：

- 不改骑行 ERG 的响应语义。
- 跑步控制点先用自测脚本覆盖。
- 如果 App 要求 indication，再评估同时支持 notify/indicate 或按 profile 区分。

### 风险 3：网页复杂度上升

缓解：

- 跑步来源复用骑行来源的交互模式。
- 自定义区默认只在自定义模式显示。
- 静态结构门禁防止 DOM/JS 漂移。

### 风险 4：配置升级丢失

缓解：

- v9 -> v10 字段级迁移。
- 保留已有跑步速度、步频、浮动和心率配置。
- host 单测覆盖迁移。

---

## 12. 验收标准

必须全部满足：

- 骑行模式原有 BLE 自测通过，ERG 控制不回归。
- 跑步模式 RSC + HRS + DIS 自测通过。
- 跑步模式 FTMS Treadmill Data 通知可解析，速度、距离、坡度、心率、用时合理。
- 跑步控制点 Set Target Speed / Set Target Inclination / Start / Stop / Reset 响应正确。
- 网页可在跑步固定、模板、自定义之间切换并保存回显。
- NVS 迁移后旧配置不丢失核心字段。
- `quality_gate.py` 通过。
- generic-c3 真机烧录、串口日志无 panic/abort/backtrace。

---

## 13. 建议结论

建议按本方案一次性完成跑步模式完整升级，但严格分批实现和验证。核心策略是：

1. 先把跑步体验对齐骑行的输出来源抽象。
2. 再扩展 FTMS Treadmill Data，提高跑步 App 兼容面。
3. 最后加入跑步控制点，并通过事件队列隔离 NimBLE 回调和主状态修改。

这条路径能同时满足“功能完整”和“不要破坏现有骑行稳定性”两个目标。
