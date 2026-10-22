# 06 - 启动流程与运行时数据流

本文描述从上电到 BLE 广播的启动链路，以及主循环 50ms 节拍的执行顺序。

---

## 启动流程

参考图片：[main-loop-flow.png](../images/main-loop-flow.png)

### 完整初始化顺序

`app_main()` 的执行顺序如下（序号对应代码行）：

```
1. [日志] ESP_LOGI(TAG, "BikePower C 启动 v%s...", FIRMWARE_VERSION)

2. [NVS 初始化]
   nvs_flash_init()
   若返回 ESP_ERR_NVS_NO_FREE_PAGES 或 ESP_ERR_NVS_NEW_VERSION_FOUND:
       nvs_flash_erase() → nvs_flash_init() 重试

3. [领域层初始化]
   power_engine_init(&s_engine)
     ├─ 默认值：260W / 90RPM / 150BPM / Steady
     ├─ profile_init()
     ├─ hr_model_init()
     └─ power_engine_reset_mode_state()

4. [配置加载]
   nvs_store_load(&s_engine)
     ├─ 打开 namespace "bikepower"
     ├─ 无旧值：config_store_defaults() → config_to_engine() → nvs_store_save()（回写默认）
     └─ 有旧值：读取所有键 → config_store_migrate()（schema 升级）
                  → config_store_validate()（范围夹紧）
                  → config_to_engine()
                  → 若有迁移/夹紧，再次保存
   power_engine_clear_dirty(&s_engine)  // 初始加载后清脏标记

5. [事件队列创建]
   s_app_queue = xQueueCreate(16, sizeof(app_evt_t))
   失败则 esp_restart()

6. [板型检测]
   board_detect()
     ├─ GPIO3 配置为输入下拉，延时 1ms 读电平
     ├─ 高电平 → M5Stamp C3，低电平 → 通用 ESP32-C3
     └─ 恢复 GPIO3 配置
   board_firmware_name(fw_name, sizeof(fw_name))  // 生成 "板型-v版本-stable-fix-gGitID"

7. [LED 初始化]
   led_init()
     ├─ 根据板型选择 GPIO 或 SK6812(RMT) 驱动
     ├─ 创建 led_task（栈 2048B，优先级 3）
     └─ led_task 订阅 WDT
   失败则 esp_restart()

8. [状态机初始化]
   app_state_init(&s_engine)
     ├─ s_engine = engine
     ├─ s_state = ST_BLE_ACTIVE
     ├─ s_connected = false
     └─ update_idle_led()（未连 → LED_SLOW）

9. [回调注册]
   app_state_set_wifi_trigger(on_wifi_trigger)
   app_state_set_wifi_exit(on_wifi_exit)
   wifi_portal_set_done_callback(on_wifi_done)

10. [按钮初始化]
    button_init(s_app_queue)
      ├─ 根据板型配置按钮 GPIO（上拉输入+任一沿中断）
      ├─ 创建 s_btn_sem 二进制信号量
      └─ 创建 button_task（栈 2560B，优先级 5）
    失败仅打 LOGE 并降级（设备仍可作为纯 BLE 功率计，不进 ERROR_RECOVERY）

11. [BLE 回调注册]
    ble_bikepower_set_erg_callback(on_erg_target)
    ble_bikepower_set_erg_clear_callback(on_erg_clear)
    ble_bikepower_set_run_control_callback(on_run_control)
    ble_bikepower_set_profile(s_engine.sport_profile)
    ble_bikepower_set_broadcast_mask(s_engine.broadcast_mask)
    ble_bikepower_set_device_name(s_engine.device_name)  // 用 NVS 加载的设备名

12. [BLE 启动]
    ble_bikepower_start()
      ├─ nimble_port_init()
      ├─ GATT 服务注册（五服务：CPS/HRS/CSC/FTMS/DIS/RSC）
      ├─ 广播配置
      ├─ 创建 BLE 通知定时器（1s 周期 esp_timer）
      └─ nimble_port_freertos_init()（启动 nimble_host 任务）
    失败则 app_state_set_error()（进入 ST_ERROR_RECOVERY，主循环周期重试）

13. [看门狗订阅]
    esp_task_wdt_add(NULL)  // 主循环订阅 WDT
    失败打 LOGW（不致命）

14. [进入主循环]
    初始化 tick=0, last_conn=0, ble_retry_tick=0, hb_count=0
    while (1) { ... }
```

---

## 主循环 50ms 节拍

每拍执行时间必须 < 50ms（WDT 超时 5s 提供宽松保护，但不应阻塞）。

### 执行顺序详解

```
┌─────────────────────────────────────────────────────────────────┐
│  while (1) 主循环                                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ① [事件队列消费] 非阻塞批量 drain                               │
│     while xQueueReceive(s_app_queue, &e, 0) == pdTRUE:          │
│         app_state_handle_event(&e)                              │
│                                                                 │
│  ② [ERG 缓存处理] 关中断原子复制 pending ERG 目标/清理            │
│     portDISABLE_INTERRUPTS()                                    │
│     if s_has_pending_erg: 复制 → 投递 APP_EVT_ERG_TARGET         │
│     if s_has_pending_erg_clear: 复制 → 投递 APP_EVT_ERG_CLEAR   │
│     portENABLE_INTERRUPTS()                                     │
│                                                                 │
│  ③ [BLE 就绪检查]                                               │
│     if !ble_bikepower_is_ready():                               │
│         if radio_ctrl_ble_suspended():  // 兜底停 BLE 配网中     │
│             WDT reset + delay → continue                        │
│         if ++ble_retry_tick >= BLE_RETRY_TICKS(100拍=5s):       │
│             ble_retry_tick = 0                                  │
│             if ble_bikepower_start_failed():                    │
│                 ble_bikepower_stop()  // 清理资源重试            │
│             ble_bikepower_start()                               │
│             成功 → app_state_clear_error()                      │
│         WDT reset + delay → continue                            │
│                                                                 │
│  ④ [测试固件分支] PORTAL_TEST_AUTO_CONFIG                        │
│     BLE 首次就绪后 3s（60拍）自动触发配网                          │
│                                                                 │
│  ⑤ [连接状态轮询]                                               │
│     conn = ble_bikepower_conn_count()                           │
│     if conn != last_conn:                                       │
│         last_conn = conn                                        │
│         投递 APP_EVT_CONN_CHANGED                               │
│                                                                 │
│  ⑥ [BLE 重启请求] profile 切换需断连后重建 GATT                   │
│     if wifi_portal_ble_restart_requested():                     │
│         if conn == 0:                                           │
│             ble_bikepower_stop() → 等待 200ms → 重设参数        │
│             → ble_bikepower_start()                             │
│             成功则 consume 请求                                  │
│         else: 每 20 拍打 LOGW 等待断开                          │
│                                                                 │
│  ⑦ [状态机 tick]                                                │
│     app_state_tick()  // 检查确认窗口超时                        │
│                                                                 │
│  ⑧ [快照生成 + BLE 数据更新]                                     │
│     if sport_profile == RUNNING:                                │
│         rs = running_engine_snapshot(&s_engine.running, dt)    │
│         running_hr = power_engine_running_heartrate(&s_engine,  │
│                                                     rs.speed)   │
│         ble_bikepower_set_running_data(rs, running_hr)          │
│     else:                                                       │
│         s = power_engine_snapshot(&s_engine)                   │
│         ble_bikepower_set_data(s.power, s.hr, s.cadence)       │
│     ble_bikepower_set_broadcast_mask(s_engine.broadcast_mask)  │
│                                                                 │
│  ⑨ [10s 心跳] tick 从 0 累加到 200                               │
│     if ++tick >= 200:                                           │
│         tick = 0; hb_count++                                    │
│         heap = esp_get_free_heap_size()                         │
│         min_heap = esp_get_minimum_free_heap_size()             │
│         ESP_LOGI("遥测 #%d 堆=%lu/%lu p=%dW(%d) c=%dRPM ...",   │
│                  hb_count, heap, min_heap, ...)                │
│         每 30 心跳（5分钟）：输出内存基线                          │
│         堆 < 20KB → LOGE（严重）                                 │
│         堆 < 80KB → LOGW（警告）                                 │
│         if power_engine_is_dirty():                             │
│             nvs_store_save() → power_engine_clear_dirty()       │
│                                                                 │
│  ⑩ [喂狗 + 延时]                                               │
│     esp_task_wdt_reset()                                        │
│     vTaskDelay(50ms)                                            │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## BLE 通知数据流

主循环与 BLE 通知是两个独立的时间尺度：

```
┌──────────────┐    50ms     ┌──────────────────┐
│  主循环       │ ──────────→ │  g_power_w       │
│  power_      │             │  g_hr_bpm        │ ←── 共享变量
│  engine_     │             │  g_cadence       │     （短互斥锁）
│  snapshot()  │             │  g_running_data  │
└──────────────┘             └────────┬─────────┘
                                     │
                                     │ 1000ms (BLE_NOTIFY_INTERVAL_MS)
                                     ▼
                            ┌──────────────────┐
                            │  notify_tick()   │
                            │  (esp_timer 回调) │
                            │                  │
                            │  pack_all()      │
                            │    ├─ pack_cps() │
                            │    ├─ pack_hr()  │
                            │    ├─ pack_csc() │
                            │    └─ pack_ftms()│
                            │                  │
                            │  notify_one() ×4 │
                            │  （静态预分配buf） │
                            └──────────────────┘
```

**关键设计**：
- 主循环每 50ms 更新共享变量（写）
- BLE 通知定时器每 1s 读取共享变量打包发送（读）
- 通过 `BLE_NOTIFY_LOCK_WAIT_MS(10ms)` 短互斥锁保护（锁持有时间微秒级）
- 缓冲区全部静态预分配（当前旧兼容 CPS 使用 `g_cps_buf[9]`），不在通知回调内 malloc；标准 8 字节修复见 `docs/dev/12-top5-protocol-stability-design.md`

---

## 配网完整数据流

参考图片：[rf-wifi-flow.png](../images/rf-wifi-flow.png)

```
[ST_BLE_ACTIVE] 按钮长按 ≥2s 释放
       │
       ▼
[ST_WIFI_CONFIRM] LED 快闪，3s 窗口
       │
       ├─ 3s 内再次按下 ──→ trigger_wifi() ──────────┐
       │                                              │
       └─ 3s 超时 ──→ 回到 ST_BLE_ACTIVE             │
                                                      │
       ▼                                              │
[ST_WIFI_CONFIG] LED_OFF → LED_WIFI（青色慢闪）       │
       │                                              │
       ├─ radio_ctrl_enter_wifi(engine):              │
       │    ├─ radio_ctrl_start_wifi_ap()             │
       │    │    ├─ esp_netif（AP 模式，192.168.4.1）│
       │    │    ├─ DHCP Option 114（Captive Portal）│
       │    │    ├─ SoftAP: BikePower-XXXXXX          │
       │    │    │  密码 12345678，信道 1，WPA2      │
       │    │    └─ coex 共存（BLE 不停）              │
       │    ├─ captive_portal_start_dns()（UDP 53）  │
       │    └─ wifi_portal_start(engine)              │
       │         ├─ esp_timer 1s 周期超时检查         │
       │         ├─ HTTP 服务器 :80                   │
       │         └─ 注册 27 个 URI handler            │
       │                                              │
       │  [用户手机连接热点]                           │
       │  [浏览器访问 192.168.4.1]                     │
       │  [GET / 返回内联 HTML（CSS/JS 已内联）]      │
       │  [前端 SPA 通过 XHR 调用 /api/xxx]            │
       │  [POST /api/config 保存配置到引擎+dirty]      │
       │  [GET /api/state 拉取实时状态]                │
       │                                              │
       ├─ 超时（180s）或 POST /api/close-wifi:       │
       │    arm_closing() → 5s 收尾 → portal_done()  │
       │                                              │
       ▼                                              │
portal_done()（esp_timer 任务）                       │
  └─ for 5次重试: xQueueSend(APP_EVT_WIFI_DONE) ──────┘
       │
       ▼
主循环消费 APP_EVT_WIFI_DONE:
  └─ app_state 切回 ST_BLE_ACTIVE
  └─ on_wifi_exit() → radio_ctrl_exit_wifi()
       ├─ wifi_portal_stop()（停 HTTP + 定时器）
       ├─ captive_portal_stop_dns()
       └─ radio_ctrl_stop_wifi_ap()（停 SoftAP）
       │
       ▼
[ST_BLE_ACTIVE] BLE 全程未断，回到正常骑行状态
```

---

## 脏标记落盘策略

```
配置变更发生时：
  - 按钮调功 → power_engine_adjust_power() → dirty = true
  - 网页保存 → h_config() → 直接 nvs_store_save()（即时落盘）
  - ERG 目标 → power_engine_set_erg_target() → dirty = true
  - 模式切换 → power_engine_set_mode() → dirty = true

主循环心跳（每 10s）：
  if power_engine_is_dirty():
      nvs_store_save(&s_engine)
      power_engine_clear_dirty(&s_engine)
```

这个策略保证：
- 按钮调功后最长 10s 内自动持久化，断电不丢失
- 配网页保存即时落盘，用户看到"已保存"时已写入 NVS
- 不每次调功都写 flash（减少 flash 磨损）

---

## 异常恢复路径

| 异常 | 检测点 | 恢复策略 |
|------|--------|----------|
| NVS 损坏/版本不匹配 | 启动时 nvs_flash_init | erase → 重新初始化 |
| 队列创建失败 | app_main | esp_restart() |
| LED 初始化失败 | app_main | esp_restart() |
| 按钮初始化失败 | app_main | 打 LOGE 降级（纯 BLE 模式） |
| BLE 启动失败 | app_main | set_error() → 主循环每 5s 重试 |
| BLE 运行时断连 | GAP 事件 | 自动重启广播 |
| 配网 WiFi 启动失败 | radio_ctrl | 停 BLE → 重试 → 仍失败则 esp_restart |
| WIFI_DONE 队列满 | portal_done | 重试 5 次 × 10ms 等待 |
| 空闲堆 < 20KB | 10s 心跳 | LOGE 告警（不自动重启，留给 coredump） |
| 看门狗超时 | 硬件 TWDT | coredump 写 flash → 重启（配置为 panic handler） |
