# BikePower C 技术参考

本文面向开发者、维护者和后续接手者。
内容基于当前仓库实现撰写，重点对应 `main/`、`components/bikepower_domain/`、`tests/` 和 `docs/images/`。
阅读顺序建议配合 [architecture.png](../images/architecture.png)、[main-loop-flow.png](../images/main-loop-flow.png)、[button-led-state.png](../images/button-led-state.png) 和 [rf-wifi-flow.png](../images/rf-wifi-flow.png) 一起看。

当前固件版本为 `v4.1.1`。
运行环境为 `ESP-IDF v5.5.4 + NimBLE + FreeRTOS`，目标芯片为 `ESP32-C3`。

## 第一章：项目架构

项目整体采用三层结构。
这不是文档上的概念拆分，而是代码目录和职责边界的直接映射。

### 1.1 应用层：`main/`

应用层负责把用户输入、BLE 通信、WiFi 配网、LED 提示和主循环拼成一个完整设备。
关键文件如下：

- `app_main.c`：启动入口、事件循环、50ms 节拍、WDT、NVS 装载
- `app_state.c`：高层状态机，消费 `app_evt_t`
- `board_detect.c`：启动时检测通用 ESP32-C3 / M5Stamp C3
- `button.c`：中断驱动按钮任务，通用板 `GPIO9`，M5Stamp C3 `GPIO3`
- `led.c`：独立灯效任务，通用板 `GPIO12` 普通 LED，M5Stamp C3 `GPIO2` SK6812 RGB
- `ble_bikepower.c`：NimBLE 五服务、通知打包、FTMS 控制点
- `radio_ctrl.c`：WiFi/BLE 射频共用唯一入口
- `wifi_portal.c`：`esp_http_server` 配网页面（SoftAP 由 `radio_ctrl.c` 启动）
- `nvs_store.c`：NVS 与领域层配置对象桥接
- `bikepower_config.h`：应用层常量中心

应用层的特点是“状态协调”而不是“算法本体”。
例如功率如何生成不在应用层决定，但什么时候生成、生成后发给谁、什么时候持久化、什么时候切 WiFi，都由应用层负责。

### 1.2 领域层：`components/bikepower_domain/`

领域层是纯 C 组件，可做 host 单测。
它不依赖 ESP-IDF，也不直接关心 BLE、WiFi、GPIO。

关键文件如下：

- `power_engine.c`：功率、踏频、心率快照生成
- `hr_model.c`：心率分区、平滑、HRV、对外限速
- `profile.c`：体重、FTP、阈值心率、配速机器人
- `config_store.c`：配置 schema、默认值、校验、迁移
- `json_miniparse.c`：轻量 JSON 字段提取
- `button_logic.c`：按钮按压语义纯逻辑
- `button_debounce.c`：去抖与长按节拍纯逻辑
- `led_pattern.c`：LED 模式、颜色与闪烁节拍映射
- `domain_rng.c`：领域层随机数
- `include/domain_config.h`：领域层常量中心

这层的价值是把“骑行模拟逻辑”和“设备运行逻辑”分开。
因此很多维护工作都可以先在 host 单测里完成，再接回嵌入式环境。

### 1.3 硬件与平台层

这里没有单独的 `hal/` 目录，但底层依赖很明确：

- FreeRTOS：任务、队列、信号量、调度
- NimBLE：BLE peripheral、GATT、GAP
- ESP WiFi：SoftAP
- NVS：配置持久化
- GPIO：按钮和 LED
- `esp_timer`：毫秒/微秒计时与定时回调
- Task WDT：主循环和长任务保护

对应关系可直接对照 [architecture.png](../images/architecture.png)。
如果后续要重构，建议维持“三层职责不交叉”的基本形状。

## 第二章：启动流程

### 2.1 从 bootloader 到 `app_main`

标准 ESP-IDF 启动完成后进入 `app_main()`。
当前入口位于 `main/app_main.c`。

`app_main()` 的实际初始化顺序如下：

1. `ESP_LOGI(TAG, "BikePower C 启动...")`
2. `nvs_flash_init()`
3. 如遇 `ESP_ERR_NVS_NO_FREE_PAGES` 或 `ESP_ERR_NVS_NEW_VERSION_FOUND`，执行 `nvs_flash_erase()` 后重试
4. `power_engine_init(&s_engine)`
5. `nvs_store_load(&s_engine)`
6. `power_engine_clear_dirty(&s_engine)`
7. `xQueueCreate(16, sizeof(app_evt_t))` 创建 `s_app_queue`
8. `led_init()`
9. `app_state_init(&s_engine)`
10. `app_state_set_wifi_trigger(on_wifi_trigger)`
11. `button_init(s_app_queue)`
12. `ble_bikepower_set_erg_callback(on_erg_target)`
13. `ble_bikepower_set_device_name(s_engine.device_name)`
14. `ble_bikepower_start()`
15. `esp_task_wdt_add(NULL)`
16. 进入永久主循环

这里有几个实现上的关键点：

- BLE 设备名并不是固定宏，而是先从 NVS 读配置后再调用 `ble_bikepower_set_device_name()`
- 状态机先启动，再注册 `wifi_trigger`
- `button.c` 只拿到事件队列句柄，不直接依赖状态机
- `on_erg_target()` 不直接改领域层，而是先把 `APP_EVT_ERG_TARGET` 投递进队列

### 2.2 NVS 初始化与回退逻辑

配置加载通过 `nvs_store_load()` 完成。
当 namespace `bikepower` 不存在时，逻辑是：

1. `config_store_defaults(&c)`
2. `config_to_engine(&c, e)`
3. `nvs_store_save(e)` 回写默认值

如果存在旧值，则：

1. 从键 `sv`、`power`、`cadence`、`mode`、`hr`、`hashr`、`pfloat`、`cfloat`、`hrfloat`、`pstep`、`weight`、`ftp`、`thr_hr`、`ride`、`cwkglo`、`cwkghi`、`cname`、`sport`、`bmask`、`rspeed`、`rpacef`、`rcad`、`rcadf`、`rmode`、`rpacer`、`rplo`、`rphi`、`rcname`、`devname` 和 `led*` 场景配置读取
2. `config_store_migrate(&c)`
3. `config_store_validate(&c)`
4. `config_to_engine(&c, e)`
5. 若发生 migration 或 clamp，再保存一次修正后结果

因此启动时即完成：

- 默认值注入
- schema 版本处理
- 越界参数夹紧
- 名称字符串兜底

## 第三章：主循环数据流

主循环位于 `app_main.c` 的 `while (1)`。
节拍由 `HR_LOOP_INTERVAL_MS` 控制，当前值在 `domain_config.h` 中为 `50ms`。

### 3.1 每个 50ms 周期的顺序

每拍的执行顺序可以概括为：

1. 非阻塞 drain `s_app_queue`
2. 如果状态机处于 `ST_WIFI_CONFIG`，只喂狗并 `vTaskDelay(50ms)`，跳过 BLE 快照逻辑
3. 调用 `ble_bikepower_conn_count()` 轮询连接变化
4. 如连接状态有变化，构造 `APP_EVT_CONN_CHANGED`
5. 调用 `app_state_tick()`
6. 调用 `power_engine_snapshot(&s_engine)`
7. 调用 `ble_bikepower_set_data()`
8. 每 200 拍打印一次心跳并检查堆内存
9. 如 `power_engine_is_dirty()` 为真，则调用 `nvs_store_save()`
10. `esp_task_wdt_reset()`
11. `vTaskDelay(50ms)`

这意味着主循环不是“所有 BLE 发包都在这里做”，而是“这里生成领域快照，BLE 定时器异步取走并通知”。

### 3.2 事件队列消费

队列元素类型为 `app_evt_t`。
当前事件种类在 `app_events.h` 中定义为：

- `APP_EVT_BTN_DOWN`
- `APP_EVT_BTN_REPEAT`
- `APP_EVT_BTN_UP`
- `APP_EVT_CONN_CHANGED`
- `APP_EVT_ERG_TARGET`
- `APP_EVT_ERG_CLEAR`
- `APP_EVT_RUN_TARGET_SPEED`
- `APP_EVT_RUN_START`
- `APP_EVT_RUN_STOP`
- `APP_EVT_RUN_RESET`
- `APP_EVT_WIFI_DONE`

所有这些事件都汇聚到 `app_state_handle_event()`。
因此状态机是应用层高层行为的唯一决策中心。

### 3.3 快照与通知的分离

主循环调用 `power_engine_snapshot()` 只做一件事：
产出 `snapshot_t` 并更新 `s_engine.snapshot`。

紧接着调用 `ble_bikepower_set_data((uint16_t)s.power, (uint8_t)s.heartrate, (uint16_t)s.cadence)`。
这个函数只是更新共享变量：

- `g_power_w`
- `g_hr_bpm`
- `g_cadence`

真正的 BLE 通知由 `ble_bikepower.c` 里的 `notify_tick()` 定时器回调完成。
通知周期由 `BLE_NOTIFY_INTERVAL_MS` 控制，当前是 `1000ms`。

### 3.4 心跳与低内存分级告警

`tick >= 200` 时，相当于每 `10s` 执行一次心跳逻辑。
会打印：

- 当前空闲堆
- 功率、心率、踏频
- 模式
- 连接数
- 状态机状态

内存阈值在 `bikepower_config.h`：

- `HEAP_WARN_BYTES = 40960`
- `HEAP_CRIT_BYTES = 20480`

低于 warn 打 `ESP_LOGW`，低于 crit 打 `ESP_LOGE`。

## 第四章：BLE 服务详解

BLE 实现集中在 `main/ble_bikepower.c`。
模块提供五个 GATT 服务：

- `Cycling Power Service`，UUID `0x1818`
- `Heart Rate Service`，UUID `0x180D`
- `Cycling Speed and Cadence Service`，UUID `0x1816`
- `Fitness Machine Service`，UUID `0x1826`
- `Device Information Service`，UUID `0x180A`

### 4.1 广播与 GAP 行为

广播入口是 `start_advertising()`。
骑行模式主广播字段放三个 16-bit UUID，优先兼容码表按功率计、踏频传感器或智能骑行台类型过滤扫描：

- `POWER_SERVICE_UUID`
- `CSC_SERVICE_UUID`
- `FTMS_SERVICE_UUID`

跑步模式主广播字段放 `RSC_SERVICE_UUID` 和 `FTMS_SERVICE_UUID`。主广播包空间有限，心率和完整设备名放入 scan response，完整能力仍依赖连接后的 GATT 发现。

广播参数使用：

- `BLE_GAP_CONN_MODE_UND`
- `BLE_GAP_DISC_MODE_GEN`
- 广播间隔 `BLE_ADVERTISING_INTERVAL_MS = 500`

GAP 事件回调为 `gap_event_cb()`：

- `BLE_GAP_EVENT_CONNECT`：成功则记录 `g_conn_handle`
- 连接失败则重启广播
- `BLE_GAP_EVENT_DISCONNECT`：清空连接句柄并重启广播
- `BLE_GAP_EVENT_ADV_COMPLETE`：重启广播

当前实现明确是单连接模型，`ble_bikepower_conn_count()` 只返回 `0` 或 `1`。

### 4.2 动态通知特征

通知定时器由 `esp_timer_create()` 创建，名字为 `ble_notify`。
`notify_tick()` 中调用 `pack_all()` 后，依次通过 `notify_one()` 推送：

- `g_cps_val_handle`
- `g_hr_val_handle`
- `g_csc_val_handle`
- `g_ftms_bike_handle`

共享缓冲区全部静态预分配，避免运行时动态分配大缓冲：

- `g_cps_buf[9]`（当前旧兼容布局；标准 8 字节修复见下方已知缺陷）
- `g_hr_buf[2]`
- `g_csc_buf[5]`
- `g_ftms_bike_buf[8]`
- `g_ftms_resp_buf[3]`

> 已知协议缺陷：当前 CPS 使用 1 字节 flags 并发送 9 字节旧兼容布局，FTMS Bike/Treadmill 的部分 flags 与实际 payload 也存在不一致。现有自测曾复制同一偏移，不能作为协议正确性的独立证据。修复边界和 golden vectors 方案见 [12-top5-protocol-stability-design.md](12-top5-protocol-stability-design.md)，审查优先级见 [13-project-review-and-optimization.md](13-project-review-and-optimization.md)。

### 4.3 CPS / HRS / CSC / FTMS / DIS 特征

对应特征 UUID 在 `bikepower_config.h` 中定义：

- `POWER_CHAR_UUID = 0x2A63`
- `HR_CHAR_UUID = 0x2A37`
- `CSC_CHAR_UUID = 0x2A5B`
- `FTMS_FEATURE_CHAR_UUID = 0x2ACC`
- `FTMS_CONTROL_POINT_CHAR_UUID = 0x2AD9`
- `FTMS_POWER_CHAR_UUID = 0x2AD2`
- `FTMS_STATUS_CHAR_UUID = 0x2ADA`
- `DIS_MANUFACTURER_CHAR_UUID = 0x2A29`
- `DIS_MODEL_CHAR_UUID = 0x2A24`
- `DIS_FIRMWARE_CHAR_UUID = 0x2A26`

DIS 读取内容来自编译期常量：

- `DEVICE_MANUFACTURER = "BikePower"`
- `DEVICE_MODEL = "ESP32-C3"`
- `FIRMWARE_VERSION = "4.1.1"`
- `FIRMWARE_STABLE_VERSION = "4.0.0"`
- `FIRMWARE_FIX_VERSION = "4.1.1"`
- `BIKEPOWER_GIT_SHA` 由 `main/CMakeLists.txt` 在构建时注入最后一次 Git 短提交 ID
- DIS 固件字段与门户完整版本名使用 `board_firmware_name()` 生成，格式为 `板型-v版本-stable稳定版-fix修复版-gGitID`，例如 `generic-c3-v4.1.1-stable4.0.0-fix4.1.1-g6e90860`

测试脚本 `tests/ble_selftest.py` 也会校验这三项。

### 4.4 FTMS 控制点 OpCode 状态机

控制点写回调是 `ftms_cp_cb()`。
支持的 OpCode 为：

- `FTMS_OP_REQUEST_CONTROL = 0x00`
- `FTMS_OP_RESET = 0x01`
- `FTMS_OP_SET_TARGET_SPEED = 0x02`
- `FTMS_OP_SET_TARGET_POWER = 0x05`
- `FTMS_OP_START_RESUME = 0x07`
- `FTMS_OP_STOP_PAUSE = 0x08`

响应格式使用：

- `FTMS_OP_RESPONSE_CODE = 0x80`
- `FTMS_RESULT_SUCCESS = 0x01`
- `FTMS_RESULT_NOT_SUPPORTED = 0x02`

状态更新逻辑：

- `RequestControl`：直接 success
- `SetTargetPower`：解析 `<h` 16-bit 功率值，调用 `g_erg_cb(target)`
- `SetTargetSpeed`：解析 `uint16` 速度（km/h*100），投递跑步目标速度事件
- `StartResume`：`g_ftms_status_buf[0] = 0x01`
- `StopPause`：`g_ftms_status_buf[0] = 0x02`
- `Reset`：`g_ftms_status_buf[0] = 0x00`
- 其他：返回 not supported

应用层注册的 `g_erg_cb` 实际是 `app_main.c` 里的 `on_erg_target()`。
它不直接写引擎，而是投递 `APP_EVT_ERG_TARGET`，由主循环串行消费。

## 第五章：状态机

高层状态机定义在 `app_state.h`：

- `ST_BOOTING`
- `ST_BLE_ACTIVE`
- `ST_WIFI_CONFIRM`
- `ST_WIFI_CONFIG`
- `ST_SAVING`
- `ST_REBOOTING`
- `ST_ERROR_RECOVERY`

当前代码里真正进入和使用的主态主要是前三个加 `ST_WIFI_CONFIG`。
`ST_SAVING`、`ST_REBOOTING`、`ST_ERROR_RECOVERY` 已作为枚举保留给后续扩展和更清晰的恢复流程。

### 5.1 初始化

`app_state_init(power_engine_t *engine)` 会：

1. 保存 `s_engine`
2. 把 `s_state` 设为 `ST_BLE_ACTIVE`
3. 把 `s_connected` 设为 `false`
4. 调用 `update_idle_led()`

`update_idle_led()` 的规则很简单：

- 未连接：`LED_SLOW`
- 已连接：`LED_SOLID`

### 5.2 连接事件

收到 `APP_EVT_CONN_CHANGED` 时：

- `s_connected = (e->data != 0)`
- 如果当前还在 `ST_BLE_ACTIVE`，刷新 idle LED

因此连接断开后的灯效恢复不依赖 BLE 模块直接控灯，而是状态机统一管理。

### 5.3 按钮事件语义

按钮事件路径是：

`GPIO ISR -> button_task -> app_queue -> app_state_handle_event()`

语义规则如下：

- 短按 `< SHORT_PRESS_MS(500ms)`：按当前可配置步进降低控制目标，默认 `20W`，范围 `5-50W`
- 长按 `>=500ms`：每 `BTN_AUTO_REPEAT_MS(200ms)` 按当前可配置步进提高控制目标（仅 steady 模式）
- 长按释放 `>= WIFI_BTN_HOLD_MS(2000ms)`：进入确认窗口
- 确认窗口内再按一次：触发 `trigger_wifi()`
- 确认窗口超过 `CONFIRM_WINDOW_MS(3000ms)`：取消并回 `ST_BLE_ACTIVE`

### 5.4 长按进入配网时的回滚行为

`enter_confirm_window()` 中有一个容易漏掉但很重要的行为：

- 如果本次长按过程发生过自动加功率，即 `s_auto_adjusted == true`
- 则先调用 `power_engine_set_power(s_engine, s_power_before_press)` 回滚到按下前基准功率
- 然后才切状态到 `ST_WIFI_CONFIRM`

这保证了“长按调功”和“超长按进配网”不会叠加副作用。

### 5.5 配网触发

`trigger_wifi()` 的逻辑：

- 若 `s_wifi_trigger` 已注册，则：
- `s_state = ST_WIFI_CONFIG`
- `led_set_pattern(LED_OFF)`
- 调用 `s_wifi_trigger()`

在 `app_main.c` 中注册的回调是 `on_wifi_trigger()`，内部调用：

`radio_ctrl_enter_wifi(&s_engine)`

## 第六章：功率引擎

功率引擎实现位于 `components/bikepower_domain/power_engine.c`。
核心对象是 `power_engine_t`。

### 6.1 关键运行时字段

`power_engine_t` 关键成员包括：

- `base_power`
- `cadence_base`
- `last_heartrate`
- `user_heartrate`
- `has_user_hr`
- `mode`
- `erg_active_target`
- `has_erg_target`
- `profile`
- `hr`
- `snapshot`
- `snapshot_count`
- `dirty`
- `device_name`

### 6.2 初始化默认值

`power_engine_init()` 会设置：

- `DEFAULT_POWER = 260`
- `DEFAULT_CADENCE = 90`
- `DEFAULT_HEARTRATE = 150`
- `DEFAULT_RIDE_MODE = RIDE_MODE_STEADY`
- `DEVICE_NAME_DEFAULT = "BikePower"`

然后调用：

- `profile_init(&e->profile)`
- `hr_model_init(&e->hr, DEFAULT_HEARTRATE)`
- `power_engine_reset_mode_state(e)`

### 6.3 快照生成

`power_engine_snapshot()` 根据当前模式分发到：

- `build_steady()`
- `build_road()`
- `build_erg()`

然后写回：

- `e->last_power`
- `e->last_cadence`
- `e->last_heartrate`
- `e->snapshot`
- `e->snapshot_count++`

### 6.4 Steady 模式（固定模式）

`build_steady()` 的特点：

- 功率在 `base_power` 上叠加 `[-power_float, +power_float]` 的有界平滑随机游走偏移
- 踏频在 `cadence_base` 上叠加 `[-cadence_float, +cadence_float]` 的有界平滑随机游走偏移
- 浮动幅度 `power_float` / `cadence_float` / `hr_float` 可在配网页调整并持久化（默认 ±5 W / ±2 rpm / ±2 bpm，范围见 `domain_config.h` 的 `*_FLOAT_MIN/MAX`）
- 浮动为 0 时对应量恒定输出基准值
- 平滑游走（`fluct_walk_step`）：每 `FLUCT_WALK_HOLD_LOOPS`（默认 60 拍≈3s）在浮动带内重采样一个目标偏移，再按时间常数 `FLUCT_WALK_TAU` 一阶指数逼近，输出始终限制在带内，因此平稳变化、不逐拍随机跳变
- 如果 `has_user_hr` 为真，则走 `hr_model_steady_user(st, user_hr, hr_float)`（心率按 `hr_float` 幅度平稳波动，`hr_float=0` 恒定）
- 否则按 FTP 区间推导目标心率，再走平滑和 HRV

### 6.5 Road 模式

`build_road()` 的特点：

- 预设档按当前 `ride_type` 取 `ROAD_PACERS[]`；自定义档 `ride_type=5` 取 profile 中的 `custom_wkg_low/high`
- 在对应 `wkg_low` 到 `wkg_high` 之间随机取样
- 计算公式约为 `power = weight * wkg / 10`
- 再按功率推导目标心率
- 自定义档昵称 `custom_pacer_name` 会持久化，并在网页下拉框中与当前 W/kg 区间一起显示

当前配速机器人表定义在 `profile.c`：

- `Coco`：`2.4-2.8 W/kg`
- `Yumi`：`2.7-3.1 W/kg`
- `Jacques`：`3.0-3.5 W/kg`
- `Genie`：`3.5-3.9 W/kg`
- `Constance`：`4.0-4.4 W/kg`
- `自定义`：默认 `2.5-3.0 W/kg`，可在网页端修改昵称与 `1.0-7.0 W/kg` 范围

### 6.6 ERG 模式

`build_erg()` 的特点：

- 若 `has_erg_target` 为真，则目标功率优先取 `erg_active_target`
- 否则退回 `base_power`
- 在目标上仍叠加少量功率波动
- 踏频仍保留小幅随机波动
- 心率路径仍走目标心率 + 平滑 + HRV + emit

这意味着 ERG 不是硬钉死一个完全平坦的瓦数，而是保留轻微“真实感”波动。

### 6.7 模式切换与重置

`power_engine_set_mode()` 在成功切换后会调用 `power_engine_reset_mode_state()`。

重置行为：

- `RIDE_MODE_ROAD`：按配速机器人下限功体比初始化 `last_power`
- `RIDE_MODE_ERG`：优先用 `erg_active_target` 初始化
- `RIDE_MODE_STEADY`：恢复到 `base_power` / `cadence_base`
- 所有模式切换后都调用 `hr_model_reset_variation()`

### 6.8 ERG 目标写入行为

`power_engine_set_erg_target()` 的语义比名字更强：

1. 检查目标功率是否在 `POWER_MIN` 到 `POWER_MAX` 之间
2. 写入 `erg_active_target`
3. 设置 `has_erg_target = true`
4. 同步更新 `base_power`
5. 如果原先不在 `RIDE_MODE_ERG`，自动切到 `ERG`
6. 置 `dirty = true`

因此当前实现中 FTMS 下发目标功率会改变后续持久化结果，不只是一次性的临时控制。这是已确认的 P0 会话态/持久化边界缺陷，目标行为见 [12-top5-protocol-stability-design.md](12-top5-protocol-stability-design.md)。

## 第七章：心率模型

心率模型位于 `components/bikepower_domain/hr_model.c`。
它不是简单的“功率乘一个系数”，而是由四层行为叠加：

1. FTP 区间映射
2. 一阶指数平滑
3. HRV 变异叠加
4. 对外输出限速

### 7.1 FTP 6 区映射

`hr_model_ftp_zone()` 根据 `power / ftp` 百分比推导 `hr_pct`。
分段大致如下：

- `<=55% FTP`
- `55-75% FTP`
- `75-90% FTP`
- `90-105% FTP`
- `105-120% FTP`
- `>120% FTP`

恢复段使用：

- `HR_ZONE_RECOVERY_BASE = 42`
- `HR_ZONE_RECOVERY_SLOPE = 26`

得到的 `hr_pct` 再乘以 `thr_hr`，最后经 `hr_clamp()` 限制到 `HR_MIN=60` 和 `HR_MAX=200`。

### 7.2 一阶指数平滑

`hr_model_smooth()` 使用公式：

`HR(t) = HR_old + alpha * (HR_target - HR_old)`

其中：

- `alpha = 1 - exp(-dt / tau)`
- `dt = HR_LOOP_INTERVAL_MS / 1000.0f = 0.05s`

上下行时间常数不同：

- `HR_TAU_RISE = 30`
- `HR_TAU_FALL = 60`

这使得升心率比降心率更快，符合真实骑行体感。

### 7.3 HRV 波动

`hr_model_variation()` 每秒左右更新一次缓存。
叠加项包括：

- 呼吸性窦性心律变异 `RSA`
- Mayer wave
- 慢漂移 `drift_bpm`

相关常量：

- `HRV_RSA_AMPLITUDE = 2.0f`
- `HRV_RSA_FREQ_HZ = 0.12f`
- `HRV_MAYER_AMPLITUDE = 1.0f`
- `HRV_MAYER_PERIOD_S = 10.0f`
- `HRV_DRIFT_RATE_BPM_PER_MIN = 0.05f`
- `HRV_DRIFT_CAP_BPM = 5.0f`

### 7.4 对外输出限速

`hr_model_emit()` 不是直接把内部平滑值原样输出。
它先看与当前对外值的差：

- 若超过 `HR_EMIT_DEADBAND = 2`，每次只变动 `1 BPM`
- 每 20 个 tick 还有低概率发生一次 `2 BPM` 随机跳变
- 跳变概率由 `HR_EMIT_JUMP_BPS = 200` 控制

这样设计的目的是让 BLE 心率通知更接近真实设备，而不是每秒跳一大截。

### 7.5 用户固定心率路径

在 `Steady` 模式下，如果用户设置了心率：

- `power_engine_set_heartrate()`
- `hr_model_set_user_hr()`
- 后续 `build_steady()` 直接走 `hr_model_steady_user()`

此路径也有轻微波动和缓慢重采样，不是严格恒定值。

## 第八章：WiFi 配网

WiFi 配网链路分布在 `app_state.c`、`radio_ctrl.c`、`wifi_portal.c`。
当前配网默认使用 ESP-IDF coex 共存模式：BLE 保持在线继续通知，WiFi SoftAP 负责本地门户；仅共存初始化失败兜底时才主动停 BLE。

![WiFi/BLE 射频共用流程](../images/rf-wifi-flow.png)

### 8.1 射频共用入口

唯一入口是 `radio_ctrl_enter_wifi(power_engine_t *engine)`。
普通配网执行顺序如下：

1. 打日志 `"进入 WiFi 配网（共存模式）：BLE 保持在线"`
2. `radio_ctrl_start_wifi_ap()` 启动 SoftAP，不触碰 NimBLE
3. `captive_portal_start_dns()` 启动 DNS 劫持
4. `wifi_portal_start(engine)` 启动 HTTP 门户
5. 保存并关闭或 180s 超时后，`wifi_portal` 投递 `APP_EVT_WIFI_DONE`
6. 状态机调用 `radio_ctrl_exit_wifi()`，关闭 HTTP/DNS/SoftAP，原地回到 BLE 工作态

SoftAP 启动时还会通过 DHCP 下发 `Domain-Name-Server = 192.168.4.1` 和 Captive Portal URI（DHCP Option 114 / RFC 8910，`http://192.168.4.1/.well-known/captive-portal`）。Portal URI 使用静态存储期字符串，满足 ESP-IDF DHCP Server 对指针生命周期的要求，避免多次进入配网或长时间运行后 DHCP 报文引用失效；该 URI 返回 `application/captive+json`，其中 `user-portal-url` 指向真正的 HTML 首页 `http://192.168.4.1/`。

失败兜底也集中在 `radio_ctrl`：只有共存 SoftAP 初始化失败时，才调用 `ble_bikepower_stop()` 释放 BLE 并重试；主循环通过 `radio_ctrl_ble_suspended()` 区分主动停 BLE 与 BLE 故障，避免在兜底配网期间自动重启 BLE。

### 8.2 SoftAP 启动

SoftAP 由 `radio_ctrl_start_wifi_ap()` 启动，HTTP 门户由 `wifi_portal_start()` 绑定到已经启动的 AP。
关键配置来自 `bikepower_config.h`：

- `WIFI_SSID_PREFIX = "BikePower"`，实际 SoftAP 名称由 `radio_ctrl` 追加 AP MAC 后 3 字节生成，例如 `BikePower-A1B2C3`
- `WIFI_PASSWORD = "12345678"`
- `WIFI_AP_CHANNEL = 1`
- `WIFI_PORT = 80`
- `WIFI_SHUTDOWN_MS = 180000`
- `WIFI_RENEW_MS = 180000`

`start_softap()` 中的运行时配置还包括：

- `ap.ap.max_connection = 2`
- `ap.ap.authmode = WIFI_AUTH_WPA2_PSK`

### 8.3 HTTP 服务与路由

v3.2.0 起配网门户前后端分离：
- 前端三件套 `main/web/index.html + app.css + app.js` 由 `scripts/inline_static.py` 在编译时内联为 `web_index_html.h`，C 侧 `GET /` 原样吐出整页 HTML（CSS/JS 已内联，兼容 iOS Captive Portal WebView 不加载外部资源）。
- 业务接口全部走 `/api/xxx` JSON，前端 SPA 通过 `XMLHttpRequest` 调用并自行渲染。

`start_httpd()` 使用 `HTTPD_DEFAULT_CONFIG()`，并设置：

- `cfg.server_port = WIFI_PORT`
- `cfg.lru_purge_enable = true`
- `cfg.max_uri_handlers = HTTP_MAX_URI_HANDLERS`（当前 30）
- `cfg.stack_size = TASK_STACK_HTTPD`（当前 12288）
- `cfg.max_open_sockets = HTTP_MAX_OPEN_SOCKETS`（当前 7）
- `cfg.keep_alive_enable = false`

已注册业务路由 18 个，另有 10 个 Captive Portal 探测路由：

- `GET /`（返回内联 HTML 整页）
- `GET /favicon.ico`（204）
- `GET /.well-known/captive-portal`（RFC 8910 Captive Portal API，返回 `application/captive+json`）
- `GET /api/state`（设备全状态：device_name/firmware_version/firmware_name/board/stable_version/fix_version/git_sha/power/cadence/heartrate/mode/profile_*/pacers/LED 配置/remaining_sec/closing）
- `GET /api/time`（纯数字剩余秒数）
- `POST /api/renew`（续期，返回新的剩余秒数）
- `GET /api/mem_stats`（`{"free":..,"min_free":..}`）
- `POST /api/config`（保存配置，返回 `{"ok":true}`）
- `POST /api/close-wifi`（保存配置后进入 5 秒收尾窗口，再关闭 WiFi 回到 BLE）
- `GET /api/device-templates`（读取最多 5 个 BLE 设备信息模板）
- `POST /api/device-templates/save`（保存一个设备信息模板，模板满时返回错误）
- `POST /api/device-templates/delete`（删除指定模板）
- `POST /api/device-templates/apply`（把模板设备名应用为本设备 BLE 名称并持久化）
- `POST /api/ble-scan/start`（启动 8 秒 BLE active scan，结果缓存在内存中）
- `POST /api/ble-scan/stop`（停止当前扫描）
- `GET /api/ble-scan/results`（返回扫描状态和最多 12 个附近设备摘要；仅包含有名称且广播服务命中骑行功率、踏频、心率或智能骑行台任意一种的设备；运行态结果包含 `adv_raw_hex`、`scan_rsp_raw_hex`、`manufacturer_data_hex`、`service_data_hex`、`tx_power`、`appearance` 等诊断字段）
- `POST /api/ble-scan/read`（临时连接指定设备，读取 GAP Device Name 与 DIS 厂商/型号/版本字段后断开；若该设备来自本轮扫描，会合并扫描阶段的原始广播字段）
- `POST /api/ble-scan/analyze`（临时连接指定设备，读取基础信息并订阅运动数据通知，短时采样后返回功率、踏频、心率、速度、设备能力摘要，以及最近一次 CPS/HRS/CSC/FTMS 通知 raw hex）

Captive Portal 探测路由与未知路径统一返回 `303 See Other`、`Location: /` 和短文本 body `Redirect to the captive portal`。这与 ESP-IDF captive_portal 示例保持一致：iOS Captive Network Assistant 对“只有空 302”的响应兼容性较差，带 body 的 303 更稳；404 兜底则用于覆盖 Chrome/Chromium `browsernetworktime/*` 等新增或变化的系统探测路径。DHCP Option 114 走 `/.well-known/captive-portal` JSON API，避免新系统把 HTML 首页当作 Captive Portal API 解析失败而不弹窗。

其中：

- `/api/config`：保存配置并实时生效到 BLE 通知
- `/api/close-wifi`：保存配置后进入 5 秒收尾窗口，再关闭 WiFi 回到 BLE
- `/api/time`：返回剩余秒数
- `/api/renew`：续期
- `/api/mem_stats`：返回 `free` 和 `min_free`
- `/api/device-templates*`：管理设备信息模板库，模板内容独立存入 NVS namespace `ble_tpl`
- `/api/ble-scan*`：模板库专用 BLE Central/Observer 入口，仅用户点击扫描、读取或分析时运行；读取完成、分析采样完成、失败或超时都会断开外部设备连接

### 8.4 超时恢复

配网超时控制基于 `esp_timer`。
关键变量：

- `s_shutdown_timer`
- `s_deadline_sec`（32 位秒级单调时间，避免 64 位变量在 httpd 任务与定时器回调间撕裂误重启）
- `s_closing`（收尾窗口中不再续期、不再接受配置修改）
- `s_done_called`（done 回调幂等标志）

流程为：

1. `arm_deadline()` 设定截止时间
2. `esp_timer_start_periodic(..., 1000000)` 每秒检查一次
3. `shutdown_check_cb()` 判断是否超时
4. 超时或保存并关闭 WiFi 后调用 `portal_done()`
5. `portal_done()` 通过 done 回调投递 `APP_EVT_WIFI_DONE`
6. 状态机在 app_main 上下文调用 `radio_ctrl_exit_wifi()`

所以当前版本的配网退出不重启设备；BLE 在普通配网期间保持在线。v4.0.0 已永久放弃设备内 OTA 与家庭网络配置；普通用户升级统一走网页端在线烧录。

### 8.5 前端反馈与 JSON 安全

配网门户是单页 SPA。首页与设置页通过 `location.hash` 切换，保存入口统一为 `POST /api/config`，保存并关闭 WiFi 入口为 `POST /api/close-wifi`。前端提交完整 JSON payload，保存成功后页面显示即时反馈；字段级校验覆盖功率、踏频、心率、体重、FTP、阈值心率、设备名称、调功步进、浮动幅度、自定义 W/kg 与 LED 配置。设备名称在设置页默认文本展示，点击右侧编辑按钮后进入输入态，点对勾确认后再随“保存设置”持久化；设备信息页提供“设备信息模板库”折叠卡片，最多保存 5 个模板，支持扫描附近 BLE 设备、连接读取设备基础信息、短时分析运动数据、确认别名后保存、应用模板设备名和二次确认删除。扫描结果只展示包含骑行功率、踏频、心率或智能骑行台任意能力的有名称设备，并在设备名称行右侧以小型圆角标签显示功率、踏频、心率、智能骑行台能力；同一设备包含多个服务时并排显示多个标签。分析卡默认只显示状态、样本数和摘要，用户展开“详细数据”后可查看功率、心率、踏频、速度、RSSI、样本数和服务能力，展开“原始数据”后可查看广播 raw hex、扫描响应 raw hex、厂商数据、服务数据以及最近一次运动通知 raw hex。分析结果和原始诊断字段只保存在前端运行态和本次 HTTP 响应中，用于辅助识别外部设备能力；`POST /api/device-templates/save` 只接收常规模板字段，不把 raw hex 写入 `ble_tpl` 模板 NVS，也不改变主配置 schema。物理按钮和 LED 配置卡片默认收起，仅用户主动展开时显示编辑控件。

`wifi_portal.c` 使用 `json_escape()` 转义字符串字段，避免设备名等用户可控内容破坏 JSON；前端渲染动态文本时使用 HTML 转义。`tests/check_portal_js.py` 与 `tests/check_portal_structure.py` 分别覆盖 JS 语法和前后端路由/视图契约。

## 第九章：配置持久化

### 9.1 NVS namespace 与键

NVS 实现位于 `main/nvs_store.c`。
namespace 固定为：

`NVS_NAMESPACE = "bikepower"`

键定义如下：

- `NVS_KEY_SCHEMA_VER = "sv"`
- `NVS_KEY_POWER = "power"`
- `NVS_KEY_CADENCE = "cadence"`
- `NVS_KEY_MODE = "mode"`
- `NVS_KEY_HEARTRATE = "hr"`
- `NVS_KEY_HAS_USER_HR = "hashr"`
- `NVS_KEY_POWER_FLOAT = "pfloat"`
- `NVS_KEY_CADENCE_FLOAT = "cfloat"`
- `NVS_KEY_HR_FLOAT = "hrfloat"`
- `NVS_KEY_POWER_STEP = "pstep"`
- `NVS_KEY_WEIGHT = "weight"`
- `NVS_KEY_FTP = "ftp"`
- `NVS_KEY_THR_HR = "thr_hr"`
- `NVS_KEY_RIDE_TYPE = "ride"`
- `NVS_KEY_CUSTOM_WKG_LO = "cwkglo"`
- `NVS_KEY_CUSTOM_WKG_HI = "cwkghi"`
- `NVS_KEY_CUSTOM_NAME = "cname"`
- `NVS_KEY_DEVICE_NAME = "devname"`
- `NVS_KEY_LED_SILENT = "ledsil"`
- `NVS_KEY_LED_WAIT_MODE = "ledwm"`
- `NVS_KEY_LED_WAIT_COLOR = "ledwc"`
- `NVS_KEY_LED_CONN_MODE = "ledconm"`
- `NVS_KEY_LED_CONN_COLOR = "ledconc"`
- `NVS_KEY_LED_WIFI_MODE = "ledwifim"`
- `NVS_KEY_LED_WIFI_COLOR = "ledwific"`
- `NVS_KEY_LED_CONFIRM_MODE = "ledcfm"`
- `NVS_KEY_LED_CONFIRM_COLOR = "ledcfc"`
- `NVS_KEY_LED_ERROR_MODE = "lederrm"`
- `NVS_KEY_LED_ERROR_COLOR = "lederrc"`
- `NVS_KEY_SPORT_PROFILE = "sport"`
- `NVS_KEY_BROADCAST_MASK = "bmask"`
- `NVS_KEY_RUN_SPEED = "rspeed"`
- `NVS_KEY_RUN_PACE_FLOAT = "rpacef"`
- `NVS_KEY_RUN_CADENCE = "rcad"`
- `NVS_KEY_RUN_CAD_FLOAT = "rcadf"`
- `NVS_KEY_RUN_MODE = "rmode"`
- `NVS_KEY_RUN_PACER = "rpacer"`
- `NVS_KEY_RUN_PACE_LO = "rplo"`
- `NVS_KEY_RUN_PACE_HI = "rphi"`
- `NVS_KEY_RUN_CUSTOM_NAME = "rcname"`

设备信息模板由 `main/device_template_store.c` 单独管理，避免模板集合数据影响主配置保存。模板 namespace 固定为：

`NVS_TEMPLATE_NAMESPACE = "ble_tpl"`

模板槽位最多 5 个，key 使用 `tpl0` 到 `tpl4`。每个槽位保存一个 `device_template_t` blob，包含模板别名、广播名、应用到本设备的设备名、厂商、型号、固件版本、硬件版本、软件版本、最近地址、地址类型、RSSI 和服务标记。删除模板会擦除对应 key；应用模板只复制设备名到主配置并刷新 BLE 广播名。

### 9.2 schema 版本

领域层配置版本在 `domain_config.h` 中：

`CONFIG_VERSION = 11`

`config_store_migrate()` 负责把旧 `schema_ver` 升级到当前版本。当前策略是：

- v6 -> v11：保留已有配置，补 `run_pace_float_sec = RUN_PACE_FLOAT_SEC_DEFAULT`
- v7 -> v11：保留已有配置，补 `run_cadence_float = RUN_CADENCE_FLOAT_DEFAULT`
- v8 -> v11：补 `broadcast_mask = BROADCAST_FIELD_ALL`
- v9 -> v11：保留既有跑步速度、配速浮动、步频、步频浮动，补 `run_mode`、`run_pacer_type`、`run_pace_low_sec`、`run_pace_high_sec`、`run_custom_name`
- v10 -> v11：移除未落地的跑步坡度持久化字段，保留其他当前字段
- 未知旧版本：回退默认配置

这意味着 v6/v7/v8/v9/v10 升级到当前版本时会保留主要用户配置；更早或无法识别的 schema 会安全回默认，避免异常配置导致新固件启动异常。

### 9.3 校验与夹紧

`config_store_validate()` 会对以下字段做边界修正：

- 功率 `POWER_MIN` 到 `POWER_MAX`
- 踏频 `CADENCE_MIN` 到 `CADENCE_MAX`
- 模式合法性
- 固定心率 `HR_MIN` 到 `HR_MAX`，`has_user_hr` 规范化为 0/1
- 体重 `PROFILE_WEIGHT_MIN` 到 `PROFILE_WEIGHT_MAX`
- FTP `PROFILE_FTP_MIN` 到 `PROFILE_FTP_MAX`
- 阈值心率 `PROFILE_THR_HR_MIN` 到 `PROFILE_THR_HR_MAX`
- `ride_type` 合法性
- 设备名空串回退为 `DEVICE_NAME_DEFAULT`

### 9.4 配置 JSON 序列化

领域层提供 `config_store_serialize()` 和 `config_store_deserialize()`，用于配置导出/导入、备份/恢复和 host 单测。序列化输出包含 schema、功率、踏频、模式、心率、体重、FTP、阈值心率、骑行类型和设备名；设备名按 JSON 规则转义双引号、反斜杠和控制字符。反序列化要求 JSON 至少包含 `schema_ver`，缺失字段先保留默认值，最后复用 `config_store_validate()` 做范围夹紧。

### 9.5 脏标记落盘策略

落盘触发来源有两类：

1. 主循环每 10 秒心跳时，若 `power_engine_is_dirty()` 为真，则自动保存
2. WiFi 配网页面点击保存后，`h_config()` 直接 `nvs_store_save()`

成功保存后会调用：

`power_engine_clear_dirty(&s_engine)`

这套策略兼顾了：

- 按钮调功后的断电恢复
- 配网页面提交时的即时生效

## 第十章：稳定性保障

### 10.1 Task WDT

主循环在 `app_main()` 中订阅 `esp_task_wdt_add(NULL)`。
LED 任务在 `led_task()` 中也订阅 WDT。

两者分别在循环里执行：

- `esp_task_wdt_reset()`

按钮任务没有订阅 WDT。
这是刻意设计，因为 `button_task` 空闲时会无限阻塞等待信号量，若强行订阅会在长时间无人按键时误触发看门狗。

### 10.2 Coredump

根据 `PROGRESS.md`，项目已在：

- `partitions.csv`
- `sdkconfig.defaults`

中配置 coredump 分区和写 flash 方案。
这属于发版稳定性链路的一部分。

### 10.3 低内存监控

主循环每 10 秒检查一次：

- `esp_get_free_heap_size()`
- `esp_get_minimum_free_heap_size()`

WiFi 配网页面额外提供 `/mem_stats`，便于在配网压测或前端路由巡检时观测内存。

### 10.4 预分配和轻量回调

为避免 NimBLE 回调里频繁分配内存，BLE 层的测量缓冲区采用静态预分配。
FTMS 控制点收到写请求后只做最少逻辑：

- 解析 op code
- 更新少量状态字节
- 通过回调通知应用层

应用层再通过事件队列串行化处理，避免跨线程重逻辑。

### 10.5 日志体系

固件端日志统一使用 ESP-IDF `ESP_LOGI/W/E/D`，每个 C 模块固定一个大写简称 TAG。
日志级别语义如下：

- `ERROR`：不可恢复错误、功能降级、需要人工介入的问题。
- `WARN`：自动恢复、低水位、配置夹紧、队列投递重试、外设暂不可用。
- `INFO`：启动里程碑、状态切换、用户动作、低频遥测。
- `DEBUG/VERBOSE`：局部诊断使用，不能作为发版默认输出。

高频路径必须限频。
主循环遥测当前由 `MAIN_HEARTBEAT_INTERVAL_TICKS` 控制为约 10 秒一次，BLE 通知/GATT 回调、WiFi socket 循环、按钮任务循环不得逐包刷 INFO。
ISR 内禁止日志。

主机端串口日志由 `tests/serial_daemon.py` 和 `scripts/log_daemon.sh` 管理。
设备日志写入 `tests/logs/device_YYYYMMDD_HHMMSS.log`，每行格式为 `[YYYY-MM-DD HH:MM:SS] <固件原始日志>`。
文件名中的时间是主机创建该串口采集会话的时间。
默认按“串口连接会话”切文件：守护进程启动、串口断开后重连会创建新文件；如果设备软重启但 USB 串口未断开，则继续写入同一文件，并插入 `DEVICE REBOOT / BOOT BANNER` 分隔线。
需要每次设备启动 banner 都切新文件时，可启动 `tests/serial_daemon.py --split-on-reboot`。

日志保留策略：默认只保留最近 5 个 `device_*.log`，多余文件按修改时间从旧到新删除；`daemon.log` 和 `daemon_restart.log` 由 `scripts/log_daemon.sh cleanup` 清理 1 天前文件。
一次性烧录抓取由 `scripts/flash_latest.sh` 生成 `tests/logs/flash_latest_YYYYMMDD_HHMMSS.log`。该脚本在正式 build 前执行 `idf.py reconfigure`，确保 `BIKEPOWER_GIT_SHA` 和 `BIKEPOWER_BUILD_TIME` 从当前源码状态重新注入，避免增量构建沿用旧 CMake 缓存；若检测到 `build/CMakeCache.txt` 残留 `sdkconfig.board.tmp`，会先清理该缓存，避免先跑 `scripts/build_all.sh` 后临时板型配置被删除导致 reconfigure 失败。

故障证据规则：守护进程检测到 `panic`、`abort`、`Backtrace`、`Guru Meditation`、`task_wdt`、`watchdog`、`WDT` 等关键字时，会生成 `tests/logs/crash_YYYYMMDD_HHMMSS.log`，保存触发行和最近上下文，避免关键故障被普通日志轮转覆盖。
如需关闭该行为，可使用 `tests/serial_daemon.py --no-crash-snapshot`。

## 第十一章：硬件约束

### 11.1 WiFi/BLE 射频共用

这是最关键的硬约束。
配网默认通过 coex 让 BLE 与 WiFi SoftAP 共用射频，进入配网不停止 BLE。
项目规则已明确要求：

- 禁止开机自启 WiFi
- 禁止绕过 `radio_ctrl` 直接调用 `esp_wifi_*`
- 共存失败兜底时，必须由 `radio_ctrl` 负责停 BLE 并设置 `radio_ctrl_ble_suspended()`
- 普通配网结束后关闭 WiFi 原地回 BLE，不通过重启恢复

### 11.2 中断上下文安全

按钮 ISR `btn_isr()` 内只调用：

- `xSemaphoreGiveFromISR()`
- 必要时 `portYIELD_FROM_ISR()`

禁止在 ISR 内：

- 日志
- BLE 调用
- 阻塞调用
- 重逻辑判断

### 11.3 GPIO 与板型适配

当前固件支持运行时板型检测，可对照 [hardware-pinout.png](../images/hardware-pinout.png)。

通用 ESP32-C3 开发板使用：

- `BTN_PIN = 9`
- `LED_PIN = 12`

M5Stamp C3 使用：

- 按钮：`GPIO3`
- SK6812 RGB LED：`GPIO2`
- 启动检测：`GPIO3` 输入下拉，按钮上拉读高判定为 M5Stamp C3

硬件规则里还明确：

- `GPIO12~17` 不应使用 QSPI 模式
- 通用板 `GPIO12` 当前用于低电平有效 LED

### 11.4 任务栈大小

当前主要任务栈大小：

- `button_task`：`2560`
- `led_task`：`2048`
- HTTP server：`8192`

这些值是当前版本运行下的稳定配置，不建议在无压测前提下随意缩小。

## 第十二章：构建与测试

### 12.1 构建

代码改动后的基础验证命令：

```bash
idf.py build
```

根据项目规则，C 代码改动后 `idf.py build` 必须通过。

发版构建使用 `scripts/build_all.sh`。该脚本支持 `--help`、`--dry-run` 和按板型选择构建；正式运行会先跑 host 单测、mock 配网页保存测试、3 分钟核心流程耐久测试和静态门禁，再为 generic-c3 / m5stamp-c3 生成 merged bin、SHA256、manifest、测试报告、核心流程明细、用户文档副本和 release 总包 zip。脚本不会打 git tag，也不会创建 Gitee Release。合并固件文件名格式为 `bikepower-c-板型-v版本-stable-fix-gGitID.bin`，例如 `bikepower-c-generic-c3-v4.1.1-stable-fix-g6d7eaaf.bin`，版本号只出现一次；总包文件名格式为 `bikepower-c-release-v版本-stable-fix-gGitID.zip`。

每次生成可分发固件时，`build/release/` 必须同时包含：

- `bikepower-c-<板型>-v<版本>-stable-fix-g<GitID>.bin`
- `*.bin.sha256`
- `manifest.txt`
- `test-report.md`
- `core-flow-endurance.md`
- `user-manual.md`
- `images/`
- `evidence/device-acceptance-generic-c3.json`（当前版本证据存在时收录）
- `evidence/device-acceptance-m5stamp-c3.json`（当前版本证据存在时收录）
- `bikepower-c-release-v<版本>-stable-fix-g<GitID>.zip`
- `*.zip.sha256`

`test-report.md` 由 `scripts/tools/generate_release_docs.py` 生成，必须记录已执行测试、未执行项、原因以及真机验收证据摘要；`core-flow-endurance.md` 由 `tests/test_core_flow_endurance.py` 输出，记录按钮、心率随动、网页编辑、模板和关闭 WiFi 等核心流程循环；`user-manual.md` 从 `docs/user/` 生成，要求先更新源用户文档再打包。

真机自动测试与人工确认由 `scripts/tools/device_acceptance.py` 按板型分别写入 `tests/reports/device-acceptance-generic-c3.json` 和 `tests/reports/device-acceptance-m5stamp-c3.json`。证据限定为 `PASS`、`FAIL`、`NOT_RUN`，并绑定 `FIRMWARE_VERSION`、Git ID 和板型。`tests/operation_gate.py --level device --board <板型>` 只更新对应板型实际执行的串口、BLE、重连、配网和广播项结果，不覆盖按钮、LED、手机等人工记录。

双板型完整构建结束后，`scripts/build_all.sh` 会调用 `python3 scripts/tools/check_release_artifacts.py --allow-dirty` 做本地产物门禁，校验 10 个上传附件、SHA256 旁文件、`manifest.txt` 和 release zip 内容。本地构建允许未提供证据；存在匹配当前版本的证据时，脚本会将其写入 manifest、测试报告和 release 总包。正式创建 Gitee Release 前必须运行：

```bash
python3 scripts/tools/device_acceptance.py validate --strict \
  --file tests/reports/device-acceptance-generic-c3.json
python3 scripts/tools/device_acceptance.py validate --strict \
  --file tests/reports/device-acceptance-m5stamp-c3.json
python3 scripts/tools/check_release_artifacts.py \
  --require-pushed \
  --require-device-evidence
```

严格模式要求两个板型各自全部 P0 项为 `PASS`，证据缺失、`FAIL`、`NOT_RUN`、板型、固件版本或 Git ID 不匹配都会阻断。`scripts/tools/publish_gitee_release.py` 上传前自动复用该检查。`python3 tests/test_release_artifacts.py` 提供无需真实固件的样例回归，覆盖标准产物、SHA256 篡改、zip 缺文件、单板证据缺失、证据过期和 P0 未完成。

### 12.2 领域层 host 单测

领域层测试位于：

- `test/test_domain`
- `test/test_domain.c`

该测试覆盖 `power_engine`、`hr_model`、`profile`、`config_store` 等纯 C 逻辑。
项目规则要求：只要改动了 `components/bikepower_domain/`，就应运行 host 单测。

### 12.3 BLE 真机自测

脚本为 `tests/ble_selftest.py`。
主要验证：

- 扫描并连接 `BikePower`
- 五服务特征是否齐全
- DIS 三字段是否符合预期
- CPS/HR/CSC/FTMS 通知是否工作
- FTMS 控制点是否返回 `0x80`
- `SetTargetPower` 后功率是否在目标值附近收敛

默认目标功率可通过命令行参数指定。

### 12.4 连接状态测试

脚本为 `tests/stage3_conn_test.py`。
作用是连接设备几秒后断开，验证：

- 状态机是否收到 `connected=1`
- 心跳日志是否正确反映连接状态

LED 物理变化仍需人工肉眼确认。

### 12.5 WiFi 门户测试

脚本为 `tests/stage5_portal_test.py`。
前提是设备已进入配网模式并连接到 `BikePower-XXXXXX` 热点。

默认会检查：

- `/`
- `/api/state`
- `/api/time`
- `/api/mem_stats`
- `/favicon.ico`

可选使用 `--renew` 额外验证 `POST /api/renew`。

### 12.6 建议的维护顺序

如果你要改动功能，建议按下面顺序验证：

1. 改领域层先跑 host 单测
2. 涉及配网页保存字段时跑 `python3 tests/test_portal_config_save.py`
3. 跑 `python3 tests/check_config_consistency.py`
4. 跑 `python3 scripts/tools/quality_gate.py` 聚合构建、host 单测和静态门禁
5. 烧录到 ESP32-C3
6. 看串口启动日志
7. 跑 `tests/ble_selftest.py`
8. 涉及配网时再跑 `tests/stage5_portal_test.py`
9. 涉及按钮和 LED 时做人工真机操作确认
10. 生成固件产物时同步输出测试报告和用户手册

## 结语

这个项目的复杂度不在“代码量很大”，而在“应用层协调、领域层模型、BLE/WiFi 射频共用”三者要同时成立。
维护时最容易出问题的地方通常不是算法本身，而是：

- 破坏了主循环节拍
- 绕过了 `radio_ctrl`
- 在错误线程里直接改引擎或发 BLE
- 忽略了 NVS 脏标记、配网收尾和射频兜底路径

只要守住这几个边界，后续继续迭代会比较稳。
