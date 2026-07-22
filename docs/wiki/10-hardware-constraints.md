# 硬件约束与平台限制

> 本文档详细说明 ESP32-C3 平台的硬件约束、GPIO 分配、内存预算、中断安全规则和任务模型。违反这些约束会导致不稳定、崩溃或内存耗尽。

## 目录

- [ESP32-C3 硬件规格](#esp32-c3-硬件规格)
- [双板型 GPIO 分配](#双板型-gpio-分配)
- [内存约束](#内存约束)
- [WiFi/BLE 射频共用](#wifible-射频共用)
- [中断安全规则](#中断安全规则)
- [FreeRTOS 任务模型](#freertos-任务模型)
- [看门狗配置](#看门狗配置)
- [禁止事项](#禁止事项)
- [异常处理 SOP](#异常处理-sop)

---

## ESP32-C3 硬件规格

| 项目 | 参数 | 说明 |
|------|------|------|
| 内核 | RISC-V 单核 | 32位，160MHz |
| SRAM | 400 KB | 片上静态 RAM，**无 PSRAM** |
| ROM | 384 KB |  Boot ROM + 基础 ROM 代码 |
| Flash | 4 MB（外接） | 通过 SPI/QSPI，DIO 模式 80MHz |
| WiFi | 802.11 b/g/n | 2.4GHz，SoftAP + Station |
| 蓝牙 | BLE 5.0 | 仅 NimBLE host，无 Classic BT |
| GPIO | 22 个 | 部分被 Flash/USB/JTAG 占用 |
| 定时器 | esp_timer | 64位单调时钟，1us 分辨率 |

### 关键限制

- **无 PSRAM**：所有数据必须在片上 SRAM 中，大缓冲需静态预分配
- **单核**：没有真正的并行执行，多任务靠 RTOS 调度，临界区要考虑
- **单 RF**：WiFi 和 BLE 共享同一射频前端，需要 coex 仲裁
- **Flash 缓存**：执行 Flash 中代码时 cache 不可用，ISR 中禁止调用 Flash 代码（除非标记 IRAM_ATTR）

---

## 双板型 GPIO 分配

BikePower C 支持两种板型，通过运行时 `board_detect` 自动检测：

### 通用 ESP32-C3 开发板（默认）

| 外设 | GPIO | 方向 | 说明 |
|------|------|------|------|
| 按钮（BOOT） | GPIO9 | 输入，内部上拉 | 按下为低电平，中断 `GPIO_INTR_ANYEDGE` |
| LED（D4） | GPIO12 | 输出，高电平有效 | 普通 GPIO 驱动板载 LED |

引脚常量：
```c
#define BTN_PIN_GENERIC   9
#define LED_PIN_GENERIC   12
```

### M5Stamp C3

| 外设 | GPIO | 方向 | 说明 |
|------|------|------|------|
| 按钮（SW） | GPIO3 | 输入，内部上拉 | 按下为低电平 |
| SK6812 RGB LED | GPIO2 | 输出（RMT 驱动） | 板载 1 颗可寻址 RGB LED |

引脚常量：
```c
#define BTN_PIN_M5STAMP   3
#define LED_PIN_M5STAMP   2
```

### SK6812 RGB LED 时序参数（M5Stamp）

| 参数 | 值 | 说明 |
|------|----|------|
| T0H | 300ns | 0 码高电平时间 |
| T0L | 900ns | 0 码低电平时间 |
| T1H | 600ns | 1 码高电平时间 |
| T1L | 600ns | 1 码低电平时间 |
| Reset | ≥80μs | 帧复位低电平时间 |
| RMT 时钟 | 10 MHz | 100ns 分辨率 |
| 亮度 | 0x30（48/255） | 全局亮度上限，防止过亮刺眼 |
| LED 数量 | 1 | 板载仅 1 颗 |

### GPIO 使用禁忌

以下 GPIO 不可用作普通 IO：

| GPIO | 用途 |
|------|------|
| GPIO11 | Flash VDD（SPI 电源） |
| GPIO12-17 | QSPI Flash 数据线（通用板上 GPIO12 做 LED 是因为 Flash 用 DIO 模式未占用该线；M5Stamp 需用其他引脚） |
| GPIO18-19 | USB D-/D+（若 USB _serial_jtag 启用） |
| GPIO20 | USB D+ |

> ⚠️ 具体可用 GPIO 取决于板型和 Flash 工作模式（DIO vs QSPI），已在 `board_detect.c` 中为两种受支持板型做好分配。不建议用户自行修改引脚。

---

## 内存约束

### 内存预算

| 状态 | 空闲堆 | 说明 |
|------|--------|------|
| 基线（NimBLE 启动后，WiFi 关） | ~200-220 KB | 健康水平 |
| WiFi AP 启动峰值 | ~160-180 KB | HTTP + DNS + coex 额外占用 ~30-40KB |
| 告警线 | < 80 KB (`HEAP_WARN_BYTES`) | 主循环 WARN 日志，需要排查 |
| 严重告警线 | < 20 KB (`HEAP_CRIT_BYTES`) | 高风险，可能导致分配失败崩溃 |

### 内存常量

```c
#define HEAP_WARN_BYTES   81920   // 80KB 告警
#define HEAP_CRIT_BYTES   20480   // 20KB 严重告警
```

### 内存设计原则

| 原则 | 实现 |
|------|------|
| **大缓冲静态预分配** | BLE 通知缓冲区（g_cps_buf[9] 等）、HTTP 响应缓冲区（buf[3328]、buf[4096]）均为 `static` 全局变量 |
| **回调中禁止 malloc** | GAP/GATT 回调、通知定时器回调、ISR 中不分配堆内存 |
| **HTTP 请求体大小限制** | `HTTP_MAX_BODY_LEN=4096`，超限返回 413 |
| **扫描结果上限** | `BLE_SCAN_MAX_RESULTS=12`，避免配网态过多缓存占用 |
| **响应缓冲区固定** | `/api/state` 使用 `static char buf[3328]`，不动态扩展 |
| **HTTP 并发限制** | `max_open_sockets=7`，`keep_alive_enable=false` 短连接 |

### 内存监控

主循环以 `MAIN_HEARTBEAT_INTERVAL_TICKS`（200 × 50ms = 10s）间隔打印空闲堆：

```
I (xxxxx) MAIN: 心跳 conn=1 mode=steady p=150W c=90RPM hr=135bpm heap=198456
```

当空闲堆低于告警线时输出 WARN 日志。

---

## WiFi/BLE 射频共用

ESP32-C3 只有一路 RF 前端，但 sdkconfig 已启用 `CONFIG_ESP_COEX_SW_COEXIST_ENABLE`，支持软件共存。

### 共存模式（默认）

| 行为 | 说明 |
|------|------|
| BLE 状态 | 配网期间**保持在线**，不断开、不停止广播、不停 NimBLE host |
| coex 仲裁 | WiFi 和 BLE 由 coex 模块按时间片仲裁，无需手动切换 |
| 数据持续 | 主循环继续生成快照推 BLE 通知，骑行 App 实时数据不中断 |
| 配网后 | 关闭 HTTP + SoftAP 回到 BLE 模式，**不重启设备** |

### 失败兜底（独占降级）

仅当共存模式启动 WiFi 失败（资源耗尽/初始化失败）时，才降级为独占方案：

```
radio_ctrl_start_wifi_ap() 失败（共存模式）
  → ble_bikepower_stop()    // 断开连接、停广播、停 NimBLE
  → vTaskDelay(RADIO_BLE_RELEASE_WAIT_MS)  // 等 200ms 释放 RF
  → radio_ctrl_start_wifi_ap()  // 重试（独占模式）
  → 仍失败 → esp_restart()
```

时间常量：
```c
#define RADIO_BLE_RELEASE_WAIT_MS  200   // BLE 停后等 RF 释放
#define RADIO_RESTART_DELAY_MS     1000  // 重启前等待
```

### 禁止事项

| 禁止 | 原因 |
|------|------|
| ❌ 绕过 `radio_ctrl` 直接调用 `esp_wifi_*` | netif/handle 泄漏，重复初始化崩溃 |
| ❌ 开机自动启动 WiFi | 无限重启循环风险（用户不在场无法关闭） |
| ❌ 配网期间直接停 BLE（非降级路径） | 骑行 App 断连，违反"设备不重启"高优原则 |
| ❌ 配网结束后 `esp_restart()` | 骑行 App 断连，应原地回 BLE |

### 唯一 RF 开关入口

所有 WiFi 启停必须通过 `radio_ctrl.c`：

```c
// 进入配网
radio_ctrl_enter_wifi();    // 启动 AP + coex（或降级独占）
wifi_portal_start(engine);  // 启动 HTTP 服务

// 退出配网
wifi_portal_stop();         // 停止 HTTP
radio_ctrl_exit_wifi();     // 关闭 AP，恢复 BLE（如被停）
```

---

## 中断安全规则

### 按钮中断

按钮使用 `GPIO_INTR_ANYEDGE` 触发中断，ISR 极其精简：

```c
// ISR 中只做这一件事
static void btn_isr_handler(void *arg) {
    xSemaphoreGiveFromISR(btn_wake_sem, &higher_woken);
    if (higher_woken) portYIELD_FROM_ISR();
}
```

**ISR 禁止做的事**：

| 禁止 | 原因 |
|------|------|
| ❌ 调用阻塞 API（`vTaskDelay`、`xQueueSend` 不带 FromISR 等） | 死锁或崩溃 |
| ❌ 调用日志 API（`ESP_LOG*`） | 日志可能访问 Flash 或加锁，ISR 中不安全 |
| ❌ 调用 BLE API（NimBLE 任何函数） | NimBLE 不是 ISR-safe |
| ❌ 调用 NVS API | NVS 内部有锁和 Flash 操作 |
| ❌ malloc/free | 堆操作有锁，ISR 中不安全 |
| ❌ 直接处理去抖/计时/自动重复逻辑 | 这些必须在任务上下文完成 |

**ISR 中只允许**：
- `xSemaphoreGiveFromISR`
- 少量静态变量读写（需 `portENTER_CRITICAL_ISR` 保护）
- `portYIELD_FROM_ISR`

### 按钮任务上下文处理

所有按钮语义处理在 `button_task`（任务上下文）中完成：
- 去抖（`BTN_DEBOUNCE_MS=30ms`）
- 短按/长按判定（`SHORT_PRESS_MS=500ms` / `WIFI_BTN_HOLD_MS=2000ms`）
- 长按自动重复（`BTN_AUTO_REPEAT_MS=200ms`）
- 事件投递到 `app_queue`

---

## FreeRTOS 任务模型

### 任务列表

| 任务 | 栈大小 | 优先级 | 说明 |
|------|--------|--------|------|
| `app_main_task` (main) | 系统默认 | 1 | 入口任务，运行主循环和事件处理 |
| `nimble_host` | NimBLE 默认 | 系统默认 | NimBLE 协议栈事件循环 |
| `button_task` | `TASK_STACK_BTN=2560B` | `TASK_PRIO_BTN=5` | 按钮去抖和语义处理 |
| `led_task` | `TASK_STACK_LED=2048B` | `TASK_PRIO_LED=3` | LED 闪烁控制（含 SK6812 RMT 发送） |
| `httpd` (WiFi 时) | `TASK_STACK_HTTPD=12288B` | `TASK_PRIO_NET=5` | HTTP 服务器（含 BLE 扫描/分析 handler，需大栈） |
| `sys_evt` | 系统默认 | 系统默认 | WiFi/网络事件循环 |
| `esp_timer` | 系统默认 | 系统默认 | 定时器回调（BLE notify_tick、portal shutdown_check） |

### 任务间通信

| 机制 | 用途 | 配置 |
|------|------|------|
| `app_queue` | 事件投递（按钮/连接/ERG/WiFi done） | 深度 `APP_EVENT_QUEUE_LEN=16` |
| `btn_wake_sem` | 按钮 ISR → button_task 唤醒 | 二值信号量 |
| `g_notify_lock` | BLE 通知互斥（stop vs notify_tick 竞态） | 互斥量 |

### 主循环节拍

主循环运行在 app_main_task 中，以 `vTaskDelay(pdMS_TO_TICKS(50))` 为 50ms 周期：

```
每 50ms 主循环:
  1. 处理 app_queue 事件（非阻塞）
  2. app_state_tick()
  3. power_engine_tick() → 生成快照
  4. ble_bikepower_set_data() → 更新 BLE 共享变量
  5. running_engine_tick()（跑步模式）
  6. 脏标记检查 → 持久化到 NVS（10s 心跳）
  7. BLE 失败检测和重试
  8. WiFi 状态检查（BLE 重启请求）
  9. esp_task_wdt_reset() 喂狗
  10. 每 10s 打印心跳日志
```

### 通知定时器（esp_timer）

BLE 通知使用 `esp_timer`（不是 FreeRTOS 定时器），周期 `BLE_NOTIFY_INTERVAL_MS=1000ms`：
- esp_timer 回调在专门的 esp_timer 任务中运行
- 回调中拿 `g_notify_lock`（trylock，0 等待），防止 stop 过程中访问已释放的 NimBLE
- 锁持有时间极短（微秒级打包+mbu发送）

---

## 看门狗配置

| 参数 | 值 | 说明 |
|------|----|------|
| WDT 超时 | `WDT_TIMEOUT_MS=5000ms` | Task WDT 超时时间 |
| 订阅任务 | app_main_task, led_task | 这些任务必须定期喂狗 |

### 喂狗策略

- 主循环每 50ms 调用 `esp_task_wdt_reset()`
- LED 任务在 20ms 节拍中喂狗
- 长操作（如 WiFi 启动期间）需在循环中喂狗
- 阻塞式等待（如 `vTaskDelay`）不会导致 WDT 超时，IDF 的 WDT 知道任务在阻塞状态

---

## 按钮时间阈值

| 常量 | 值 | 说明 |
|------|----|------|
| `BTN_DEBOUNCE_MS` | 30ms | 边沿去抖窗口 |
| `BTN_POLL_MS` | 20ms | 按下保持期间轮询间隔 |
| `SHORT_PRESS_MS` | 500ms | 短按判定阈值（< 500ms 为短按） |
| `WIFI_BTN_HOLD_MS` | 2000ms | 长按阈值（≥ 2000ms 进入配网确认） |
| `CONFIRM_WINDOW_MS` | 3000ms | 配网确认窗口时长（3s 内再短按确认，超时回滚） |
| `BTN_AUTO_REPEAT_MS` | 200ms | 长按自动重复间隔（用于调功连续步进） |

### LED 闪烁参数

| 常量 | 值 | 模式 |
|------|----|------|
| `LED_BLINK_SLOW_MS` | 1000ms | 慢闪（等待连接） |
| `LED_BLINK_FAST_MS` | 200ms | 快闪（配网确认窗口） |
| `LED_BLINK_WIFI_MS` | 3000ms | 呼吸式（配网中） |
| `LED_TICK_MS` | 20ms | LED 任务节拍 |

---

## 禁止事项

### 绝对禁止（Deny）

| 禁止操作 | 后果 |
|---------|------|
| 绕过 `radio_ctrl` 直接调 `esp_wifi_start`/`esp_wifi_stop` | netif/handle 泄漏，重复初始化崩溃 |
| 开机自动启动 WiFi | 无限重启循环，用户无法关闭 |
| ISR 中调用阻塞/日志/BLE/NVS API | 死锁、崩溃、不稳定 |
| 在 GATT/GAP/定时器回调中做 malloc/复杂逻辑 | 阻塞协议栈，内存碎片，死锁 |
| 通知/GATT 回调中动态分配大缓冲 | 堆碎片 + 低内存崩溃 |
| 禁用 coex 改用手动停 BLE 方案（非降级路径） | 违反蓝牙优先原则 |
| 配网结束后 `esp_restart()` | 骑行 App 断连 |

### 需要确认（Ask）

| 操作 | 要求 |
|------|------|
| 修改 `bikepower_config.h` 常量 | 列出所有引用模块，同步检查文档 |
| 修改任务栈大小 | 实测确认，不随意缩小（可能栈溢出） |
| 修改按钮阈值 | 确认与 config 头文件一致 |
| 修改 BLE 通知格式 | 同步更新 `ble_selftest.py` 和文档 |
| 修改 NVS schema | 确认迁移方案，`CONFIG_VERSION` 递增 |

### 自动执行（Allow）

| 操作 | 说明 |
|------|------|
| 编译、格式化、lint | 低风险可逆，直接执行 |
| host 单测运行 | 无硬件依赖 |
| 静态门禁脚本运行 | 无副作用 |

---

## 异常处理 SOP

### 标准异常处理表

| 异常 | 标准操作 | 预防措施 |
|------|---------|---------|
| NimBLE 初始化失败 | 记录 + `ble_bikepower_start_failed()` → 主循环 stop + 5s 延时 + 重试 | 启动序列校验返回码，不传播到崩溃 |
| 广播启动失败 | `mark_start_failed()` → 主循环重试 | 检查广播字段大小不超 31 字节 |
| WiFi/BLE 共存冲突 | 走 `radio_ctrl` 统一入口；先 coex，失败降级停 BLE 兜底 | 启动前检查 RF 状态 |
| 通知锁等待超时 | stop 时 `BLE_STOP_LOCK_WAIT_MS=100ms` 超时后取消停用，不强制 deinit | 锁持有时间保持微秒级 |
| HTTP 请求体过大 | 返回 413 Payload Too Large | `HTTP_MAX_BODY_LEN=4096` 限制 |
| NVS 写入失败 | 返回 JSON 错误，运行时值保持不变，不崩溃 | 写临时副本+原子替换 |
| malloc 失败 | 返回错误，记录日志，不崩溃 | 关键路径预分配，错误路径释放资源 |
| Task WDT 超时 | 检查阻塞点，确保长任务订阅 WDT 并喂狗 | 主循环 50ms 喂狗，LED 任务 20ms 喂狗 |
| 空闲堆 < 80KB | 输出 WARN 日志 | 避免回调中 malloc，静态预分配 |
| 空闲堆 < 20KB | 输出 ERROR 日志 | 同上，考虑功能裁剪 |
| 串口被占用 | `flash_latest.sh` 自动停止占用的 `serial_daemon.py` | 部署脚本检测 |

### BLE 重试机制

BLE 启动失败不会直接崩溃，而是由主循环处理：

```
ble_bikepower_start() 返回失败 或 运行中 mark_start_failed()
  → 主循环检测到 ble_bikepower_start_failed()
  → 等待 BLE_RETRY_TICKS (100 × 50ms = 5s)
  → ble_bikepower_stop() 清理
  → 再等待 500ms
  → ble_bikepower_start() 重试
```

这避免了启动时 NimBLE 尚未完全就绪导致的永久失败。

### 配网失败回退

HTTP 启动失败或定时器创建失败时，主动调用 `portal_done()` 触发 done_cb，让状态机回 BLE 模式，不卡死在配网中。
