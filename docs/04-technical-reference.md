# BikePower C 技术参考

本文面向开发者、维护者和后续接手者。
内容基于当前仓库实现撰写，重点对应 `main/`、`components/bikepower_domain/`、`tests/` 和 `docs/images/`。
阅读顺序建议配合 [architecture.png](images/architecture.png)、[main-loop-flow.png](images/main-loop-flow.png)、[button-led-state.png](images/button-led-state.png) 和 [rf-wifi-flow.png](images/rf-wifi-flow.png) 一起看。

当前目标固件版本为 `v3.1.0`。
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
- `radio_ctrl.c`：BLE -> WiFi 射频切换唯一入口
- `wifi_portal.c`：SoftAP + `esp_http_server` 配网页面
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

对应关系可直接对照 [architecture.png](images/architecture.png)。
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

1. 从键 `sv`、`power`、`cadence`、`mode`、`tune`、`weight`、`ftp`、`thr_hr`、`ride`、`devname` 读取
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
广播字段中只放两个 16-bit UUID：

- `POWER_SERVICE_UUID`
- `HR_SERVICE_UUID`

这样做是因为广播包空间有限，完整能力依赖连接后的 GATT 发现。

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

- `g_cps_buf[8]`
- `g_hr_buf[2]`
- `g_csc_buf[5]`
- `g_ftms_bike_buf[8]`
- `g_ftms_resp_buf[3]`

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
- `FIRMWARE_VERSION = "3.0.0"`

测试脚本 `tests/ble_selftest.py` 也会校验这三项。

### 4.4 FTMS 控制点 OpCode 状态机

控制点写回调是 `ftms_cp_cb()`。
支持的 OpCode 为：

- `FTMS_OP_REQUEST_CONTROL = 0x00`
- `FTMS_OP_RESET = 0x01`
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

- 短按 `< SHORT_PRESS_MS(500ms)`：`power_engine_adjust_power(-POWER_STEP_W)`
- 长按 `>=500ms`：每 `BTN_AUTO_REPEAT_MS(200ms)` 自动 `+20W`
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
- `mode_tuning_road`
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
- `MODE_TUNE_DEFAULT = 50`
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

### 6.4 Steady 模式

`build_steady()` 的特点：

- 功率在 `base_power` 上叠加 `[-5, +5]` 波动
- 踏频在 `cadence_base` 上叠加 `[-2, +2]` 波动
- 如果 `has_user_hr` 为真，则走 `hr_model_steady_user()`
- 否则按 FTP 区间推导目标心率，再走平滑和 HRV

### 6.5 Road 模式

`build_road()` 的特点：

- 按当前 `ride_type` 取 `ROAD_PACERS[]`
- 在对应 `wkg_low` 到 `wkg_high` 之间随机取样
- 计算公式约为 `power = weight * wkg / 10`
- 再按功率推导目标心率

当前配速机器人表定义在 `profile.c`：

- `Coco`：`2.4-2.8 W/kg`
- `Yumi`：`2.7-3.1 W/kg`
- `Jacques`：`3.0-3.5 W/kg`
- `Genie`：`3.5-3.9 W/kg`
- `Constance`：`4.0-4.4 W/kg`

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

因此 FTMS 下发目标功率会改变后续持久化结果，不只是一次性的临时控制。

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
最核心的硬约束是：ESP32-C3 只有一路 RF，BLE 和 WiFi 不共存。

![WiFi/BLE 射频互斥流程](images/rf-wifi-flow.png)

### 8.1 射频互斥入口

唯一入口是 `radio_ctrl_enter_wifi(power_engine_t *engine)`。
执行顺序固定：

1. 打日志 `"进入 WiFi 配网：停用 BLE 释放射频"`
2. `ble_bikepower_stop()`
3. `vTaskDelay(200ms)`
4. `wifi_portal_start(engine)`

这个 200ms 的释放窗口是为了给控制器留出 RF 资源切换时间。

### 8.2 SoftAP 启动

`wifi_portal_start()` 内部调用 `start_softap()`。
SoftAP 关键配置来自 `bikepower_config.h`：

- `WIFI_SSID = "BikePower"`
- `WIFI_PASSWORD = "12345678"`
- `WIFI_AP_CHANNEL = 1`
- `WIFI_PORT = 80`
- `WIFI_SHUTDOWN_MS = 180000`
- `WIFI_RENEW_MS = 180000`

`start_softap()` 中的运行时配置还包括：

- `ap.ap.max_connection = 2`
- `ap.ap.authmode = WIFI_AUTH_WPA2_PSK`

### 8.3 HTTP 服务与 11 路由

`start_httpd()` 使用 `HTTPD_DEFAULT_CONFIG()`，并设置：

- `cfg.server_port = WIFI_PORT`
- `cfg.lru_purge_enable = true`
- `cfg.max_uri_handlers = 12`
- `cfg.stack_size = 8192`

已注册路由共 11 个：

- `GET /`
- `POST /config`
- `GET /success`
- `GET /more`
- `POST /draft`
- `GET /ui/module`
- `GET /closing`
- `GET /time`
- `GET /renew`
- `GET /mem_stats`
- `GET /favicon.ico`

其中：

- `/config`：真正保存配置并进入 5 秒后重启
- `/draft`：只把更多配置页字段回写到内存，不保存不重启
- `/time`：返回剩余秒数
- `/renew`：续期
- `/mem_stats`：返回 `free` 和 `min_free`
- `/success`：带 PRG 守卫，未提交时直访返回 302 到 `/`

### 8.4 超时恢复

配网超时控制基于 `esp_timer`。
关键变量：

- `s_shutdown_timer`
- `s_deadline_us`
- `s_rebooting`

流程为：

1. `arm_deadline()` 设定截止时间
2. `esp_timer_start_periodic(..., 1000000)` 每秒检查一次
3. `shutdown_check_cb()` 判断是否超时
4. 超时或保存完成后统一 `esp_restart()`

所以当前版本不实现“配网结束后无重启恢复 BLE”，而是走更稳妥的重启回 BLE 路线。

## 第九章：配置持久化

### 9.1 NVS namespace 与键

NVS 实现位于 `main/nvs_store.c`。
namespace 固定为：

`NVS_NS = "bikepower"`

键定义如下：

- `K_SCHEMA_VER = "sv"`
- `K_POWER = "power"`
- `K_CADENCE = "cadence"`
- `K_MODE = "mode"`
- `K_TUNE = "tune"`
- `K_WEIGHT = "weight"`
- `K_FTP = "ftp"`
- `K_THR_HR = "thr_hr"`
- `K_RIDE = "ride"`
- `K_DEVNAME = "devname"`

### 9.2 schema 版本

领域层配置版本在 `domain_config.h` 中：

`CONFIG_VERSION = 4`

`config_store_migrate()` 当前没有细粒度迁移逻辑。
只要 `schema_ver != CONFIG_VERSION`，就直接 `config_store_defaults()`。

这意味着当前策略是：

- 新版本不尝试修修补补兼容旧字段
- 版本不一致就重置为默认配置

### 9.3 校验与夹紧

`config_store_validate()` 会对以下字段做边界修正：

- 功率 `POWER_MIN` 到 `POWER_MAX`
- 踏频 `CADENCE_MIN` 到 `CADENCE_MAX`
- 模式合法性
- `tune_road`
- 体重 `PROFILE_WEIGHT_MIN` 到 `PROFILE_WEIGHT_MAX`
- FTP `PROFILE_FTP_MIN` 到 `PROFILE_FTP_MAX`
- 阈值心率 `PROFILE_THR_HR_MIN` 到 `PROFILE_THR_HR_MAX`
- `ride_type` 合法性
- 设备名空串回退为 `DEVICE_NAME_DEFAULT`

### 9.4 脏标记落盘策略

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

## 第十一章：硬件约束

### 11.1 WiFi/BLE 互斥

这是最关键的硬约束。
进入配网前必须先执行 `ble_bikepower_stop()`，再启 WiFi。
项目规则已明确要求：

- 禁止开机自启 WiFi
- 禁止 BLE 活着时启 WiFi
- 配网结束后通过 `esp_restart()` 恢复 BLE

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

当前固件支持运行时板型检测，可对照 [hardware-pinout.png](images/hardware-pinout.png)。

通用 ESP32-C3 开发板使用：

- `BTN_PIN = 9`
- `LED_PIN = 12`

M5Stamp C3 使用：

- 按钮：`GPIO3`
- SK6812 RGB LED：`GPIO2`
- 启动检测：`GPIO12` 悬空读低判定为 M5Stamp C3

硬件规则里还明确：

- `GPIO12~17` 不应使用 QSPI 模式
- 通用板 `GPIO12` 当前用于高电平有效 LED

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
前提是设备已进入配网模式并连接到 `BikePower` 热点。

默认会检查：

- `/`
- `/more`
- `/ui/module?name=device`
- `/time`
- `/mem_stats`
- `/favicon.ico`
- `/success` 直访守卫

可选使用 `--renew` 额外验证 `/renew`。

### 12.6 建议的维护顺序

如果你要改动功能，建议按下面顺序验证：

1. 改领域层先跑 host 单测
2. 跑 `idf.py build`
3. 烧录到 ESP32-C3
4. 看串口启动日志
5. 跑 `tests/ble_selftest.py`
6. 涉及配网时再跑 `tests/stage5_portal_test.py`
7. 涉及按钮和 LED 时做人工真机操作确认

## 结语

这个项目的复杂度不在“代码量很大”，而在“应用层协调、领域层模型、BLE/WiFi 射频互斥”三者要同时成立。
维护时最容易出问题的地方通常不是算法本身，而是：

- 破坏了主循环节拍
- 绕过了 `radio_ctrl`
- 在错误线程里直接改引擎或发 BLE
- 忽略了 NVS 脏标记和重启恢复路径

只要守住这几个边界，后续继续迭代会比较稳。
