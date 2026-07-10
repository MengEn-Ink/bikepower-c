# 跑步模式完整升级实施计划

> 状态：执行计划。  
> 上游方案：[跑步模式完整升级方案](06-running-mode-full-upgrade-plan.md)。  
> 执行原则：TDD 红绿闭环、按批次提交、每批可独立验证，不破坏骑行 CPS/HRS/CSC/FTMS Indoor Bike/ERG。  

---

## 目标

把跑步模式升级为“固定配速 / 模板配速 / 自定义配速区间 + RSC/HRS/FTMS Treadmill + 跑步控制点”的完整实现，并用 host、mock、静态门禁、ESP-IDF build、generic-c3 烧录和 BLE 真机自测验证。

## 总体架构

- 领域层先形成稳定模型：`running_engine` 负责跑步速度、步频、距离、用时、坡度和模板游走；`config_store` 负责 schema v10、默认值、校验、JSON 往返和迁移。
- 应用层保持主任务串行：NimBLE 控制点只解析和投递事件，`app_main` 消费事件后调用 `power_engine_set_*`，避免在 BLE 回调里改复杂状态。
- 网页/API 与 NVS 只做配置桥接：网页输出来源与骑行来源交互一致，`/api/state` 与 mock 回显同字段，配置一致性门禁防漂移。

## 批次与提交边界

| 批次 | 目标 | 提交类型 | 必过验证 |
|------|------|----------|----------|
| 1 | 领域层、配置 schema v10、NVS key | `feat(running): 增加跑步模板配置模型` | `make -C test && ./test/test_domain`、`python3 tests/check_config_consistency.py`、`python3 tests/check_nvs_schema.py` |
| 2 | 网页、API、mock 保存回显 | `feat(portal): 增加跑步输出来源配置` | `python3 tests/test_portal_config_save.py`、`python3 tests/check_portal_structure.py`、`python3 tests/check_portal_js.py` |
| 3 | FTMS Treadmill Data | `feat(ble): 增加跑步FTMS跑步机数据` | `python3 scripts/tools/quality_gate.py`、`idf.py build`、running BLE selftest |
| 4 | FTMS 跑步控制点 | `feat(ble): 支持跑步FTMS控制点` | cycling/running BLE selftest、ERG 回归 |
| 5 | 文档、发布门禁、真机完整验证 | `docs(running): 同步跑步模式升级文档` | quality gate、flash、serial log、BLE cycling/running |

每个提交正文必须逐文件列出变更，并包含：

```text
Co-authored-by: AI <ai@trae>
Model: GPT-5.5/2026-07-10
```

---

## 批次 1：领域层和配置

### 文件

- 修改：`components/bikepower_domain/include/domain_config.h`
- 修改：`components/bikepower_domain/include/running_engine.h`
- 修改：`components/bikepower_domain/running_engine.c`
- 修改：`components/bikepower_domain/include/power_engine.h`
- 修改：`components/bikepower_domain/power_engine.c`
- 修改：`components/bikepower_domain/include/config_store.h`
- 修改：`components/bikepower_domain/config_store.c`
- 修改：`main/bikepower_config.h`
- 修改：`main/nvs_store.c`
- 修改：`test/test_domain.c`
- 修改：`tests/check_config_consistency.py`
- 修改：`tests/check_nvs_schema.py`

### RED 测试

1. 在 `test/test_domain.c` 增加 `test_running_pacer_defaults()`：
   - `CONFIG_VERSION == 10`
   - 默认 `run_mode == RUN_MODE_FIXED`
   - 默认 `run_pacer_type == RUN_PACER_JOG`
   - 默认 `run_pace_low_sec == 390`
   - 默认 `run_pace_high_sec == 450`
   - 默认 `run_custom_name == RUN_CUSTOM_NAME_DEFAULT`
   - 默认 `run_incline_x10 == 0`

2. 增加 `test_running_pacer_templates()`：
   - `running_engine_set_pacer(&e, RUN_PACER_AEROBIC)` 后配速区间为 `330..390`
   - `running_engine_set_pacer(&e, RUN_PACER_TEMPO)` 后配速区间为 `270..315`
   - `running_engine_set_pacer(&e, RUN_PACER_INTERVAL)` 后配速区间为 `210..255`
   - 模板模式连续 200 次 `running_engine_snapshot(&e, 1000)`，输出配速始终在对应区间内

3. 增加 `test_running_custom_pacer_validation()`：
   - 自定义下限低于 `RUN_PACE_SEC_MIN` 时夹到 `144`
   - 自定义上限高于 `RUN_PACE_SEC_MAX` 时夹到 `1200`
   - 下限大于上限时把上限调整为下限
   - 空自定义名称回退 `RUN_CUSTOM_NAME_DEFAULT`

4. 增加 `test_running_incline_and_control_values()`：
   - `power_engine_set_run_incline(&e, -250)` 后为 `-100`
   - `power_engine_set_run_incline(&e, 250)` 后为 `150`
   - `power_engine_set_run_speed(&e, 1234)` 自动切回 `RUN_MODE_FIXED`
   - `power_engine_set_run_pacer(&e, RUN_PACER_CUSTOM)` 后 `power_engine_button_target()` 仍优先返回 `CONTROL_TARGET_RUN_SPEED`

5. 增加 `test_config_migrate_v9_to_v10_running_fields()`：
   - 从 `schema_ver = 9` 迁移后保留 `run_speed_x100`、`run_pace_float_sec`、`run_cadence`、`run_cadence_float`
   - 新字段补默认：`run_mode`、`run_pacer_type`、`run_pace_low_sec`、`run_pace_high_sec`、`run_custom_name`、`run_incline_x10`

运行并确认 RED：

```bash
make -C test && ./test/test_domain
```

预期：编译或测试失败，失败点指向新增类型、字段或函数尚未实现。

### GREEN 实现

1. `domain_config.h`
   - `CONFIG_VERSION` 升到 `10`
   - 新增 `run_mode_t`、`run_pacer_type_t`、`run_pacer_t`
   - 新增 `RUN_PACE_SEC_MIN/MAX`、`RUN_CUSTOM_NAME_MAX`、`RUN_CUSTOM_NAME_DEFAULT`、`RUN_INCLINE_X10_MIN/MAX/DEFAULT`
   - 声明 `extern const run_pacer_t RUN_PACERS[RUN_PACER_COUNT]`

2. `running_engine.h/.c`
   - `running_snapshot_t` 增加 `int incline_x10`
   - `running_engine_t` 增加 `mode`、`pacer_type`、`pace_low_sec`、`pace_high_sec`、`custom_name`、`incline_x10`、`pacer_pace_walk`
   - 新增 setters：`running_engine_set_mode`、`running_engine_set_pacer`、`running_engine_set_custom_pacer`、`running_engine_set_incline`
   - 模板模式用区间中点加平滑游走生成当前配速；固定模式保留当前 `speed_x100_kmh + pace_float_sec`

3. `power_engine.h/.c`
   - 对外暴露跑步模式、模板、自定义、坡度 setter
   - `power_engine_set_run_speed()` 成功后切回 `RUN_MODE_FIXED`
   - 跑步心率联动继续使用当前输出速度

4. `config_store.h/.c`
   - `config_data_t` 增加新字段
   - defaults/validate/serialize/deserialize 同步新字段
   - v9 -> v10 只补新字段，保留旧跑步配置

5. `bikepower_config.h` 与 `nvs_store.c`
   - 新增 NVS key：`rmode`、`rpacer`、`rplo`、`rphi`、`rcname`、`rincl`
   - `engine_to_config()` 与 `config_to_engine()` 同步桥接
   - load/save 读写新 key

### GREEN 验证

```bash
make -C test
./test/test_domain
python3 tests/check_config_consistency.py
python3 tests/check_nvs_schema.py
```

预期：全部通过。

---

## 批次 2：网页、API、mock

### 文件

- 修改：`main/wifi_portal.c`
- 修改：`main/web/index.html`
- 修改：`main/web/app.js`
- 修改：`main/web/app.css`
- 修改：`tests/mock_server.py`
- 修改：`tests/test_portal_config_save.py`
- 修改：`tests/check_portal_structure.py`

### RED 测试

1. `tests/test_portal_config_save.py`
   - POST `/api/config` 保存：
     - `sport_profile=running`
     - `run_mode=pacer`
     - `run_pacer_type=custom`
     - `run_pace_low_sec=300`
     - `run_pace_high_sec=360`
     - `run_custom_name="晨跑"`
     - `run_incline_x10=25`
   - GET `/api/state` 必须回显同值。
   - 非法值覆盖：
     - `run_mode="bad"` 保持旧值
     - `run_pace_low_sec=1` 夹到 `144`
     - `run_pace_high_sec=9999` 夹到 `1200`
     - `run_incline_x10=999` 夹到 `150`

2. `tests/check_portal_structure.py`
   - 检查 `run-source-picker`
   - 检查 `run-custom-panel`
   - 检查 JS 函数：`renderRunSourcePicker`、`onRunSourceSelect`、`updateRunPacerInfo`、`onRunCustomPaceAdj`、`paceSecText`、`speedTextFromPaceSec`
   - 检查 collectConfig 包含新字段

运行并确认 RED：

```bash
python3 tests/test_portal_config_save.py
python3 tests/check_portal_structure.py
```

预期：缺字段、缺 DOM 或缺 JS 函数失败。

### GREEN 实现

1. `/api/state`
   - 输出 `run_mode`、`run_pacer_type`、`run_pacers`、`run_pace_low_sec`、`run_pace_high_sec`、`run_custom_name`、`run_incline_x10`
   - `run_pacers` 与领域层常量一致：jog/aerobic/tempo/interval

2. `/api/config`
   - 按现有严格解析风格保存新字段
   - 非法字段不覆盖旧值；越界字段经领域层 setter/validate 夹紧

3. 前端
   - 跑步页在广播项下方增加 `run-source-picker`
   - 固定模式显示当前配速输入和常用配速
   - 模板模式显示模板摘要
   - 自定义模式显示名称、上下限、坡度
   - 保存 payload 带新字段

4. Mock
   - 初始 state 与 `/api/config` 回显支持新字段
   - `run_pacers` 与固件输出一致

### GREEN 验证

```bash
python3 tests/test_portal_config_save.py
python3 tests/check_portal_structure.py
python3 tests/check_portal_js.py
python3 tests/check_portal_render.py --viewport-width 390 --viewport-height 844
```

预期：保存回显、结构、JS 语法和移动视口渲染通过。

---

## 批次 3：FTMS Treadmill Data

### 文件

- 修改：`main/ble_bikepower.h`
- 修改：`main/ble_bikepower.c`
- 修改：`main/app_main.c`
- 修改：`tests/ble_selftest.py`
- 修改：`tests/check_ble_monitor.py` 或相关静态门禁（如需）
- 修改：`docs/dev/01-technical-reference.md`

### RED 测试

1. `tests/ble_selftest.py --profile running`
   - 发现 `FTMS_SERVICE_UUID`
   - 发现 `FTMS_TREADMILL_UUID = 00002acd-0000-1000-8000-00805f9b34fb`
   - 可订阅 Treadmill Data
   - 解析到速度、距离、坡度、心率、用时

2. 静态检查
   - 跑步 profile GATT 表包含 RSC/HRS/FTMS/DIS
   - cycling profile 仍包含 CPS/HRS/CSC/FTMS/DIS

运行并确认 RED：

```bash
BIKEPOWER_EXPECT_BOARD=generic-c3 python3 tests/ble_selftest.py --profile running
```

预期：当前固件缺少 FTMS Treadmill Data，测试失败。

### GREEN 实现

1. `bikepower_config.h`
   - 新增 `FTMS_TREADMILL_CHAR_UUID 0x2ACD`

2. `ble_bikepower.c`
   - 跑步 GATT 表增加 FTMS service：Feature、Treadmill Data、Control Point、Status
   - 新增 treadmill data buffer 和 value handle
   - `ble_bikepower_set_running_data()` 同步速度、距离、坡度、心率、用时
   - `notify_tick_cb()` 在 running profile 下按广播 mask 通知 RSC、HRS、FTMS Treadmill
   - 广播 UUID 仍保持 RSC + HRS

3. `tests/ble_selftest.py`
   - 增加 Treadmill Data 解析函数
   - running selftest 同时检查 RSC 与 FTMS 数据合理性

### GREEN 验证

```bash
python3 scripts/tools/quality_gate.py
. /Users/bytedance/esp/esp-idf-v5.5.4/export.sh >/dev/null 2>&1 && idf.py build
./scripts/flash_latest.sh --board generic --capture-seconds 25
BIKEPOWER_EXPECT_BOARD=generic-c3 python3 tests/ble_selftest.py
BIKEPOWER_EXPECT_BOARD=generic-c3 python3 tests/ble_selftest.py --profile running
```

预期：构建、烧录、cycling BLE、running BLE 全部通过，串口无 panic/abort/backtrace。

---

## 批次 4：FTMS 跑步控制点

### 文件

- 修改：`main/bikepower_config.h`
- 修改：`main/ble_bikepower.h`
- 修改：`main/ble_bikepower.c`
- 修改：`main/app_events.h`
- 修改：`main/app_main.c`
- 修改：`components/bikepower_domain/include/power_engine.h`
- 修改：`components/bikepower_domain/power_engine.c`
- 修改：`tests/ble_selftest.py`
- 修改：`test/test_domain.c`

### RED 测试

1. `test/test_domain.c`
   - `power_engine_set_run_speed(&e, 1100)` 设置成功并切 `RUN_MODE_FIXED`
   - `power_engine_set_run_incline(&e, 35)` 设置成功
   - `power_engine_run_reset(&e)` 清距离和用时

2. `tests/ble_selftest.py --profile running --control`
   - 写 `0x02 + uint16 speed_x100`，响应 `0x80,0x02,0x01`
   - Treadmill Data 后续速度接近目标
   - 写 `0x03 + sint16 incline_x10`，响应成功，Treadmill Data 坡度匹配
   - 写 `0x07`、`0x08`、`0x01` 响应成功
   - running profile 写 `0x05 Set Target Power` 返回 not supported
   - cycling profile `0x05 Set Target Power` 仍成功进入 ERG

运行并确认 RED：

```bash
make -C test && ./test/test_domain
BIKEPOWER_EXPECT_BOARD=generic-c3 python3 tests/ble_selftest.py --profile running --control
```

预期：控制点缺事件或缺解析导致失败。

### GREEN 实现

1. `bikepower_config.h`
   - 新增 `FTMS_OP_SET_TARGET_SPEED 0x02`
   - 新增 `FTMS_OP_SET_TARGET_INCLINATION 0x03`

2. `app_events.h`
   - 新增 `APP_EVT_RUN_TARGET_SPEED`
   - 新增 `APP_EVT_RUN_TARGET_INCLINE`
   - 新增 `APP_EVT_RUN_START`
   - 新增 `APP_EVT_RUN_STOP`
   - 新增 `APP_EVT_RUN_RESET`

3. `ble_bikepower.h/.c`
   - 新增跑步控制回调类型或统一 app event 回调
   - running profile 下支持 speed/incline/start/stop/reset
   - cycling profile 下保持 `0x05 Set Target Power`
   - running profile 下 `0x05` 返回 `FTMS_RESULT_NOT_SUPPORTED`

4. `app_main.c`
   - 回调只投递队列
   - 主循环消费事件后调用 `power_engine_set_run_speed()`、`power_engine_set_run_incline()`、`power_engine_run_reset()`

### GREEN 验证

```bash
make -C test
./test/test_domain
python3 scripts/tools/quality_gate.py
. /Users/bytedance/esp/esp-idf-v5.5.4/export.sh >/dev/null 2>&1 && idf.py build
./scripts/flash_latest.sh --board generic --capture-seconds 25
BIKEPOWER_EXPECT_BOARD=generic-c3 python3 tests/ble_selftest.py
BIKEPOWER_EXPECT_BOARD=generic-c3 python3 tests/ble_selftest.py --profile running --control
```

预期：cycling ERG 与 running 控制点都通过。

---

## 批次 5：文档、门禁和真机验收

### 文件

- 修改：`docs/dev/01-technical-reference.md`
- 修改：`docs/dev/03-test-coverage-analysis.md`
- 修改：`docs/dev/04-running-mode-compatibility.md`
- 修改：`docs/user/02-user-manual.md`
- 修改：`PROGRESS.md`
- 修改：`CHANGELOG.md`
- 视门禁需要修改：`tests/check_config_consistency.py`
- 视门禁需要修改：`scripts/tools/quality_gate.py`

### RED 检查

1. `tests/check_config_consistency.py`
   - 要求 `CONFIG_VERSION 10`
   - 要求新 NVS key 出现在代码和技术参考
   - 要求 `/api/state` 字段出现在 mock、前端、技术参考

2. 文档检查
   - 技术参考 BLE running profile 包含 FTMS Treadmill
   - 用户手册包含跑步固定/模板/自定义配置说明
   - 测试覆盖率文档包含 running FTMS/control selftest

### GREEN 验证

```bash
python3 scripts/tools/quality_gate.py
. /Users/bytedance/esp/esp-idf-v5.5.4/export.sh >/dev/null 2>&1 && idf.py build
./scripts/flash_latest.sh --board generic --capture-seconds 25
BIKEPOWER_EXPECT_BOARD=generic-c3 python3 tests/ble_selftest.py
BIKEPOWER_EXPECT_BOARD=generic-c3 python3 tests/ble_selftest.py --profile running --control
```

真机日志验收：

- 固件启动无 panic/abort/backtrace
- 识别板型 `generic-c3`
- LED 静默配置仍保持灭灯
- BLE 广播名正常
- cycling 与 running profile 切换后均可连接

---

## 执行顺序

1. 批次 1 RED：补 host/config/NVS 测试并确认失败。
2. 批次 1 GREEN：实现领域层和配置，跑 host/config/NVS 验证，提交并 push。
3. 批次 2 RED/GREEN：实现网页/API/mock，跑 portal 门禁，提交并 push。
4. 批次 3 RED/GREEN：实现 FTMS Treadmill，跑 quality gate、build、flash、BLE running，提交并 push。
5. 批次 4 RED/GREEN：实现跑步控制点，跑 cycling/running 控制点真机回归，提交并 push。
6. 批次 5：同步文档和门禁，跑完整验证，提交并 push。

## 回滚策略

- 批次 1 失败：只回滚领域/配置提交，不影响已发布 v4.0.6。
- 批次 2 失败：保留领域层，回滚网页/API/mock 提交。
- 批次 3 失败：保留 RSC/HRS running profile，回滚 FTMS Treadmill 提交。
- 批次 4 失败：保留 FTMS Treadmill 数据面，回滚控制点提交。
- 真机 App 发现必须广播 FTMS：单独开后续小提交调整广播 UUID，不混入本轮控制点实现。
