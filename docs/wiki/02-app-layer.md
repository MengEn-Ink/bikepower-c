# 02 - 应用层模块详解

应用层位于 `main/` 目录，负责把用户输入、BLE 通信、WiFi 配网、LED 提示和主循环拼成一个完整设备。

---

## app_main.c — 入口与主循环

**文件**：[app_main.c](file:///Users/bytedance/work/bikepower-c/main/app_main.c)
**TAG**：`"APP"`

### 全局状态

| 变量 | 类型 | 说明 |
|------|------|------|
| `s_engine` | `power_engine_t` | 全局唯一的功率引擎实例 |
| `s_app_queue` | `QueueHandle_t` | 事件队列，深度 16，元素 `app_evt_t` |
| `s_pending_erg_target` | `volatile int` | 队列满时 ERG 目标兜底缓存 |
| `s_has_pending_erg` | `volatile bool` | 是否有缓存的 ERG 目标待处理 |

### 关键回调函数

| 函数 | 运行上下文 | 职责 |
|------|-----------|------|
| `on_erg_target(int target_power)` | NimBLE host 任务 | FTMS 目标功率回调：范围校验 → 投递队列（队列满时关中断缓存） |
| `on_erg_clear(int reason)` | NimBLE host 任务 | FTMS 停止/复位回调：投递 `APP_EVT_ERG_CLEAR` |
| `on_run_control(...)` | NimBLE host 任务 | 跑步控制回调（速度/开始/停止/复位） |
| `on_wifi_trigger()` | 主任务（状态机调用） | 调用 `radio_ctrl_enter_wifi()` 进入配网 |
| `on_wifi_exit()` | 主任务（状态机调用） | 调用 `radio_ctrl_exit_wifi()` 退出配网 |
| `on_wifi_done()` | esp_timer 任务 | 配网结束（保存/超时）：重试投递 `APP_EVT_WIFI_DONE` 到队列 |

### `app_main()` 初始化顺序

```
1. nvs_flash_init()（失败则 erase 重试）
2. power_engine_init(&s_engine)
3. nvs_store_load(&s_engine) → power_engine_clear_dirty()
4. xQueueCreate(16, sizeof(app_evt_t))
5. board_detect() → board_firmware_name()
6. led_init()
7. app_state_init(&s_engine)
8. 注册 wifi_trigger / wifi_exit / done 回调
9. button_init(s_app_queue)
10. 注册 BLE 回调（erg/run control/profile/broadcast_mask/device_name）
11. ble_bikepower_start()（失败则 app_state_set_error()）
12. esp_task_wdt_add(NULL)
13. while(1) 主循环
```

### 主循环每 50ms 执行顺序

```
1. 非阻塞批量 drain s_app_queue → app_state_handle_event(&e)
2. 处理 pending ERG 目标/清理（关中断原子复制后处理）
3. 若 BLE 未就绪：
   - ble_suspended 则直接 delay 继续
   - 否则每 BLE_RETRY_TICKS(100 拍=5s) 重试 ble_bikepower_start()
4. PORTAL_TEST_AUTO_CONFIG 测试分支：BLE 就绪 3s 后自动触发配网
5. 轮询 ble_bikepower_conn_count()，变化则投递 APP_EVT_CONN_CHANGED
6. 处理 BLE restart 请求（profile 切换需断连后重建 GATT）
7. app_state_tick()
8. 骑行/跑步双分支快照：
   - SPORT_PROFILE_RUNNING: running_engine_snapshot → ble_bikepower_set_running_data
   - 否则: power_engine_snapshot → ble_bikepower_set_data
9. 每 200 拍（10s）心跳：
   - 遥测日志 "遥测 #N 堆=free/min p=... c=... hr=... m=... conn=... state=..."
   - 每 30 心跳（300s）输出内存基线
   - 堆 < HEAP_CRIT_BYTES(20KB) 打 LOGE，< HEAP_WARN_BYTES(80KB) 打 LOGW
   - dirty 则 nvs_store_save() → power_engine_clear_dirty()
10. esp_task_wdt_reset()
11. vTaskDelay(50ms)
```

---

## app_state.c — 高层状态机

**文件**：[app_state.c](file:///Users/bytedance/work/bikepower-c/main/app_state.c) / [app_state.h](file:///Users/bytedance/work/bikepower-c/main/app_state.h)

### 状态枚举

| 状态 | 值 | 说明 |
|------|----|------|
| `ST_BOOTING` | 0 | 启动中（实际初始化后立即切到 ST_BLE_ACTIVE） |
| `ST_BLE_ACTIVE` | 1 | BLE 广播/连接中（LED 由连接状态决定：未连慢闪、已连常亮） |
| `ST_WIFI_CONFIRM` | 2 | 长按后等待二次确认（LED 快闪） |
| `ST_WIFI_CONFIG` | 3 | WiFi 配网模式 |
| `ST_SAVING` | 4 | 预留（保存中） |
| `ST_REBOOTING` | 5 | 预留（重启中） |
| `ST_ERROR_RECOVERY` | 6 | BLE 错误恢复（LED 错误模式） |

详细状态机见 [05-状态机与事件](05-state-machine-and-events.md)。

### 内部状态变量

| 变量 | 说明 |
|------|------|
| `s_engine` | 绑定的功率引擎指针 |
| `s_state` | 当前状态 |
| `s_connected` | BLE 是否已连接 |
| `s_btn_pressed` | 按钮当前是否按下 |
| `s_press_start_ms` | 按下起始时间戳 |
| `s_auto_adjusted` | 本次长按是否发生过自动加功（用于回滚） |
| `s_power_before_press` | 按下前的基准功率（长按进配网时回滚） |
| `s_confirm_start_ms` | 确认窗口起始时间戳 |
| `s_wifi_trigger` / `s_wifi_exit` | 配网进入/退出回调 |

### 关键函数

| 函数 | 说明 |
|------|------|
| `app_state_init(engine)` | 设置 s_engine、s_state=ST_BLE_ACTIVE、刷新 idle LED |
| `app_state_handle_event(e)` | 事件分发入口，按当前状态调用对应 handler |
| `app_state_tick()` | 每拍调用，检查确认窗口超时（3s 无操作自动取消） |
| `app_state_set_wifi_trigger(cb)` | 注册配网触发回调 |
| `app_state_set_error()` / `app_state_clear_error()` | BLE 错误恢复态切换 |
| `update_idle_led()` | 根据连接状态设置 LED（未连 SLOW、已连 SOLID） |
| `enter_confirm_window()` | 进入确认窗口（回滚自动加功、LED 快闪、记录起始时间） |
| `trigger_wifi()` | 切 ST_WIFI_CONFIG、LED_OFF、调用 s_wifi_trigger() |

---

## button.c — 按钮驱动

**文件**：[button.c](file:///Users/bytedance/work/bikepower-c/main/button.c) / [button.h](file:///Users/bytedance/work/bikepower-c/main/button.h)
**TAG**：`"BTN"`

### 硬件连接

引脚由 `board_detect` 运行时决定：
- 通用板：`BTN_PIN_GENERIC = GPIO9`（内部上拉，按下低电平）
- M5Stamp C3：`BTN_PIN_M5STAMP = GPIO3`（外部上拉，按下低电平）

### 内部机制

```
GPIO 配置（上拉输入 + 任一沿中断）
        │
        ▼
btn_isr()（ISR 上下文）
  └─ xSemaphoreGiveFromISR(s_btn_sem)
        │
        ▼
button_task()（独立任务）
  └─ 循环等待信号量
      ├─ 读取当前电平（去抖：30ms 内再次采样确认）
      ├─ 按下沿：记录按下时间，投递 APP_EVT_BTN_DOWN
      ├─ 释放沿：计算按压时长，投递 APP_EVT_BTN_UP
      └─ 长按保持：每 BTN_AUTO_REPEAT_MS(200ms) 投递 APP_EVT_BTN_REPEAT
```

### ISR 安全规则

`btn_isr()` 内只允许：
- `xSemaphoreGiveFromISR()`
- `portYIELD_FROM_ISR()`（必要时）

**禁止**在 ISR 内调用日志、BLE、阻塞 API 或重逻辑。

### 关键函数

| 函数 | 说明 |
|------|------|
| `button_init(QueueHandle_t q)` | 初始化 GPIO、创建信号量、启动 button_task |

### 按钮时间阈值

定义在 [bikepower_config.h](file:///Users/bytedance/work/bikepower-c/main/bikepower_config.h)：

| 宏 | 值 | 含义 |
|----|-----|------|
| `SHORT_PRESS_MS` | 500 | 短按/长按分界 |
| `WIFI_BTN_HOLD_MS` | 2000 | 超长按触发配网确认 |
| `CONFIRM_WINDOW_MS` | 3000 | 确认窗口超时 |
| `BTN_AUTO_REPEAT_MS` | 200 | 长按自动重复间隔 |
| `BTN_POLL_MS` | 20 | 按下保持期间轮询间隔 |
| `BTN_DEBOUNCE_MS` | 30 | 边沿去抖时间 |

---

## led.c — LED 驱动

**文件**：[led.c](file:///Users/bytedance/work/bikepower-c/main/led.c) / [led.h](file:///Users/bytedance/work/bikepower-c/main/led.h)
**TAG**：`"LED"`

### 双驱动架构

根据板型检测结果选择驱动：
- `LED_DRV_GPIO`（通用板）：GPIO12 低电平有效 LED，简单闪烁通过 `gpio_set_level()` 切换
- `LED_DRV_SK6812`（M5Stamp C3）：GPIO2 SK6812 RGB LED，通过 RMT 外设发送 WS2812 时序

### LED 模式

| 模式 | 通用板表现 | M5Stamp C3 表现 | 含义 |
|------|-----------|-----------------|------|
| `LED_OFF` | 熄灭 | 熄灭 | 非活动态 |
| `LED_SLOW` | 1s 周期慢闪 | 蓝色 1s 慢闪 | BLE 广播中（未连接） |
| `LED_FAST` | 200ms 快闪 | 黄色 200ms 快闪 | 等待二次确认 |
| `LED_SOLID` | 常亮 | 绿色常亮 | BLE 已连接 |
| `LED_WIFI` | 3s 慢闪 | 青色 3s 慢闪 | WiFi 配网中 |

颜色可通过配网页面自定义（NVS 持久化）。

### 内部任务

`led_task()` 订阅 WDT，每 `LED_TICK_MS(20ms)` 执行：
1. 根据模式和相位计算当前 LED 状态（on/off/颜色）
2. 调用对应驱动（GPIO 或 RMT）
3. `esp_task_wdt_reset()`

### 关键函数

| 函数 | 说明 |
|------|------|
| `led_init()` | 初始化 GPIO/RMT、创建 led_task |
| `led_set_pattern(pattern)` | 设置 LED 模式（异步，投递到 led 任务） |
| `led_set_config(config)` | 从引擎应用自定义 LED 配置（模式+颜色） |

---

## board_detect.c — 板型检测

**文件**：[board_detect.c](file:///Users/bytedance/work/bikepower-c/main/board_detect.c) / [board_detect.h](file:///Users/bytedance/work/bikepower-c/main/board_detect.h)
**TAG**：`"BOARD"`

### 检测原理

启动时：
1. 将 GPIO3 配置为输入下拉（`GPIO_PULLDOWN_ENABLE`）
2. 延时 1ms 稳定后读取电平：
   - 读低 → 通用板（通用板 GPIO3 悬空，下拉读低）
   - 读高 → M5Stamp C3（按钮外部上拉到 3.3V，读高）
3. 恢复 GPIO3 配置（后续 button_init 重新配置为按钮输入）

### 板型描述表

```c
typedef struct {
    const char *name;           // 板型名（用于固件命名）
    int btn_pin;                // 按钮 GPIO
    int led_pin;                // LED GPIO
    led_drv_t led_drv;          // LED 驱动类型
    led_active_level_t led_active; // GPIO LED 极性
} board_desc_t;

static const board_desc_t s_board_desc[BOARD_TYPE_COUNT] = {
    [BOARD_GENERIC_C3]  = { BOARD_NAME_GENERIC, BTN_PIN_GENERIC, LED_PIN_GENERIC, LED_DRV_GPIO, LED_ACTIVE_LOW },
    [BOARD_M5STAMP_C3]  = { BOARD_NAME_M5STAMP, BTN_PIN_M5STAMP, LED_PIN_M5STAMP, LED_DRV_SK6812, 0 },
};
```

### 关键函数

| 函数 | 说明 |
|------|------|
| `board_detect()` | 执行板型检测（启动时调用一次） |
| `board_get_type()` | 获取检测到的板型 |
| `board_get_desc()` | 获取当前板型描述符 |
| `board_firmware_name(buf, len)` | 生成固件名：`板型-v版本-stable稳定版-fix修复版-gGitID` |

---

## radio_ctrl.c — BLE/WiFi 射频控制

**文件**：[radio_ctrl.c](file:///Users/bytedance/work/bikepower-c/main/radio_ctrl.c) / [radio_ctrl.h](file:///Users/bytedance/work/bikepower-c/main/radio_ctrl.h)
**TAG**：`"RADIO"`

> ⚠️ **这是 WiFi/BLE 射频操作的唯一入口**，禁止其他模块直接调用 `esp_wifi_*` 或 `ble_bikepower_stop()`。

### 配网流程（共存模式）

```
radio_ctrl_enter_wifi(engine)
    │
    ├─ 打日志："进入 WiFi 配网（共存模式）：BLE 保持在线"
    ├─ radio_ctrl_start_wifi_ap()
    │     ├─ esp_netif 初始化（AP 模式）
    │     ├─ DHCP 配置（Option 114 Captive Portal URI）
    │     ├─ esp_wifi_set_mode(WIFI_MODE_AP)
    │     └─ esp_wifi_start()（不触碰 NimBLE，coex 时间片仲裁）
    ├─ captive_portal_start_dns()
    └─ wifi_portal_start(engine)
```

### 失败兜底

只有当共存 SoftAP 初始化失败时，才执行：
1. `ble_bikepower_stop()`
2. 设置 `s_ble_suspended = true`
3. 等待 `RADIO_BLE_RELEASE_WAIT_MS(200ms)`
4. 重试 SoftAP
5. 若仍失败则 `esp_restart()`

### 关键函数

| 函数 | 说明 |
|------|------|
| `radio_ctrl_enter_wifi(engine)` | 进入 WiFi 配网（共存优先，BLE 不停） |
| `radio_ctrl_exit_wifi()` | 退出配网：停 HTTP/DNS/SoftAP，回到 BLE |
| `radio_ctrl_ble_suspended()` | BLE 是否被主动暂停（兜底停 BLE 时为 true） |
| `radio_ctrl_start_wifi_ap()` | 启动 SoftAP（SSID = BikePower-XXXXXX，密码 12345678） |

### WiFi AP 配置

| 宏 | 值 | 含义 |
|----|-----|------|
| `WIFI_SSID_PREFIX` | "BikePower" | SSID 前缀，实际追加 MAC 后 3 字节 |
| `WIFI_PASSWORD` | "12345678" | WPA2 密码 |
| `WIFI_AP_CHANNEL` | 1 | 信道 |
| `WIFI_PORT` | 80 | HTTP 端口 |
| `WIFI_SHUTDOWN_MS` | 180000 | 默认超时 3 分钟 |
| `WIFI_MAX_STA_CONN` | 2 | 最大客户端数 |

---

## captive_portal.c — DNS 劫持

**文件**：[captive_portal.c](file:///Users/bytedance/work/bikepower-c/main/captive_portal.c) / [captive_portal.h](file:///Users/bytedance/work/bikepower-c/main/captive_portal.c)
**TAG**：`"CAPTIVE"`

### 功能

启动 UDP 53 DNS 服务器，对所有 A 记录查询返回 `192.168.4.1`，让手机连上热点后自动弹出 Captive Portal 页面。

### 关键函数

| 函数 | 说明 |
|------|------|
| `captive_portal_start_dns()` | 创建 UDP socket、绑定 53 端口、启动 DNS 任务 |
| `captive_portal_stop_dns()` | 停止 DNS 任务、关闭 socket |

---

## nvs_store.c — NVS 配置桥接

**文件**：[nvs_store.c](file:///Users/bytedance/work/bikepower-c/main/nvs_store.c) / [nvs_store.h](file:///Users/bytedance/work/bikepower-c/main/nvs_store.h)
**TAG**：`"NVS"`

### Namespace 与键

- Namespace：`NVS_NAMESPACE = "bikepower"`
- 键定义在 [bikepower_config.h](file:///Users/bytedance/work/bikepower-c/main/bikepower_config.h)，约 40 个键（功率、模式、体重、FTP、LED、跑步参数等）

### 加载流程

```
nvs_store_load(engine)
    ├─ nvs_open("bikepower", NVS_READWRITE)
    ├─ 读取 schema version ("sv")
    ├─ 若 namespace 不存在：
    │     ├─ config_store_defaults(&c)
    │     ├─ config_to_engine(&c, engine)
    │     └─ nvs_store_save(engine)（回写默认值）
    ├─ 若存在旧值：
    │     ├─ 读取所有键值
    │     ├─ config_store_migrate(&c)（schema 升级）
    │     ├─ config_store_validate(&c)（范围夹紧）
    │     ├─ config_to_engine(&c, engine)
    │     └─ 若发生 migration/clamp，再保存一次修正值
    └─ nvs_close()
```

### 关键函数

| 函数 | 说明 |
|------|------|
| `nvs_store_load(engine)` | 从 NVS 加载配置到引擎（含 defaults/migrate/validate） |
| `nvs_store_save(engine)` | 从引擎保存配置到 NVS |
| `nvs_factory_reset()` | 擦除整个 "bikepower" namespace（未在主流程使用） |

---

## device_template_store.c — 设备模板存储

**文件**：[device_template_store.c](file:///Users/bytedance/work/bikepower-c/main/device_template_store.c) / [device_template_store.h](file:///Users/bytedance/work/bikepower-c/main/device_template_store.h)
**TAG**：`"TPL"`

管理 BLE 设备信息模板库（最多 5 个），独立 namespace `ble_tpl`，键 `tpl0`-`tpl4`，每个槽位存一个 blob。

### 关键函数

| 函数 | 说明 |
|------|------|
| `device_template_load_all(arr, count)` | 加载全部模板 |
| `device_template_save(idx, tpl)` | 保存模板到指定槽位 |
| `device_template_delete(idx)` | 删除指定槽位 |
| `device_template_apply_name(idx, engine)` | 应用模板的设备名到主配置 |

---

## ble_scanner.c — BLE Central/Observer

**文件**：[ble_scanner.c](file:///Users/bytedance/work/bikepower-c/main/ble_scanner.c) / [ble_scanner.h](file:///Users/bytedance/work/bikepower-c/main/ble_scanner.h)
**TAG**：`"SCAN"`

用于设备信息模板库功能：扫描附近 BLE 设备、连接读取 DIS/GATT、短时采样运动数据。

**注意**：仅在用户点击网页按钮时运行，不常驻扫描。扫描期间 NimBLE 同时作为 peripheral 和 central。

### 关键函数

| 函数 | 说明 |
|------|------|
| `ble_scanner_start(duration_ms)` | 启动 active scan（默认 8s） |
| `ble_scanner_stop()` | 停止扫描 |
| `ble_scanner_get_results(results, max)` | 获取扫描结果（最多 12 个，仅包含骑行相关设备） |
| `ble_scanner_read_device(addr, result)` | 临时连接指定设备读取 DIS（厂商/型号/版本） |
| `ble_scanner_analyze_device(addr, result)` | 临时连接并短时采样运动数据（9s 窗口） |

---

## ble_bikepower.c — BLE 服务

BLE 服务内容较多，详见 [07-BLE 服务详解](07-ble-services.md)。

---

## wifi_portal.c — HTTP 配网门户

配网门户内容较多，详见 [08-WiFi 配网门户](08-wifi-portal.md)。
