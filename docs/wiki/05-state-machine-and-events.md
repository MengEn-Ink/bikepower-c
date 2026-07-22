# 05 - 状态机与事件系统

本文描述高层应用状态机、事件类型和按钮语义。

---

## 状态机概览

状态机定义在 [app_state.h](file:///Users/bytedance/work/bikepower-c/main/app_state.h)，是应用层高层行为的唯一决策中心。

### 状态枚举

```
ST_BOOTING → ST_BLE_ACTIVE → ST_WIFI_CONFIRM → ST_WIFI_CONFIG
                                  ↑                  │
                                  └──── (超时) ──────┘
                                       
ST_ERROR_RECOVERY ← (BLE 启动失败) → (BLE 重试成功) → ST_BLE_ACTIVE
```

| 状态 | 值 | LED 表现 | 说明 |
|------|----|----------|------|
| `ST_BOOTING` | 0 | — | 启动中（初始化后立即切到 BLE_ACTIVE） |
| `ST_BLE_ACTIVE` | 1 | 未连：慢闪<br>已连：常亮 | BLE 广播/连接正常工作 |
| `ST_WIFI_CONFIRM` | 2 | 快闪 | 长按 >2s 释放后等待二次确认（3s 窗口） |
| `ST_WIFI_CONFIG` | 3 | 熄灭（WiFi 慢闪） | WiFi 配网模式（BLE 保持在线） |
| `ST_SAVING` | 4 | — | 预留（当前未使用） |
| `ST_REBOOTING` | 5 | — | 预留（当前未使用） |
| `ST_ERROR_RECOVERY` | 6 | 错误模式 | BLE 启动失败，主循环周期重试 |

状态机图参考：[button-led-state.png](../images/button-led-state.png)

---

## 事件类型

所有事件通过 `app_queue` 投递，由 `app_state_handle_event()` 串行消费。

| 事件 | data 字段含义 | 来源 | 状态机响应 |
|------|--------------|------|-----------|
| `APP_EVT_BTN_DOWN` | 0 | button_task（按下沿 ISR） | 记录按下时间、功率基准、auto_adjusted 标志 |
| `APP_EVT_BTN_REPEAT` | 0 | button_task（长按保持每 200ms） | Steady 模式下 power_step 加功，置 auto_adjusted |
| `APP_EVT_BTN_UP` | 按压时长(ms) | button_task（释放沿） | <500ms：短按-减功<br>500-2000ms：（已由 REPEAT 处理）<br>≥2000ms：进确认窗口 |
| `APP_EVT_CONN_CHANGED` | 0=断开/1=连接 | app_main 轮询 | 刷新 idle LED |
| `APP_EVT_ERG_TARGET` | 目标功率(W) | on_erg_target（FTMS 回调） | power_engine_set_erg_target() |
| `APP_EVT_ERG_CLEAR` | 原因码 | on_erg_clear（FTMS 回调） | power_engine_clear_erg_target() |
| `APP_EVT_RUN_TARGET_SPEED` | 速度(×100km/h) | on_run_control（FTMS 回调） | running_engine_set_speed() |
| `APP_EVT_RUN_START/STOP/RESET` | 0 | on_run_control | 跑步状态控制 |
| `APP_EVT_WIFI_DONE` | 0 | on_wifi_done（esp_timer） | radio_ctrl_exit_wifi() → 切回 BLE_ACTIVE |

---

## 按钮事件语义

按钮物理层见 [button.c](file:///Users/bytedance/work/bikepower-c/main/button.c)，语义层由状态机处理。

### 时间阈值

| 阈值 | 值 | 含义 |
|------|-----|------|
| `BTN_DEBOUNCE_MS` | 30ms | 硬件去抖窗口 |
| `SHORT_PRESS_MS` | 500ms | 短按/长按分界 |
| `BTN_AUTO_REPEAT_MS` | 200ms | 长按自动重复间隔 |
| `WIFI_BTN_HOLD_MS` | 2000ms | 超长按触发配网确认 |
| `CONFIRM_WINDOW_MS` | 3000ms | 二次确认窗口超时 |

### 按钮交互流程

```
按钮按下
  │
  ├─ < 500ms 释放 ─→ 短按：功率 -= power_step（默认 -20W）
  │
  ├─ ≥ 500ms 保持 ─→ 每 200ms 自动重复：功率 += power_step（默认 +20W）
  │                     （仅 Steady 模式；置 s_auto_adjusted = true）
  │
  └─ ≥ 2000ms 释放 ─→ 进入 ST_WIFI_CONFIRM：
                         1. 若 s_auto_adjusted，回滚到按下前基准功率
                         2. LED 快闪
                         3. 启动 3s 确认窗口
                         
     确认窗口（3s 内）：
       ├─ 再次按下 ─→ trigger_wifi()：切 ST_WIFI_CONFIG，调用 s_wifi_trigger()
       └─ 超时无操作 ─→ 回到 ST_BLE_ACTIVE，LED 恢复 idle
```

### 长按进配网的回滚机制

`enter_confirm_window()` 中的关键设计：
- 如果本次长按过程中发生过自动加功（`s_auto_adjusted == true`）
- 先调用 `power_engine_set_power(s_engine, s_power_before_press)` 回滚
- 再切到确认窗口

这保证了"误长按不想配网"和"长按调功后想进配网"两种场景都不会叠加副作用。

---

## 状态迁移表

### ST_BLE_ACTIVE 状态

| 事件 | 动作 | 下一个状态 |
|------|------|-----------|
| BTN_DOWN | 记录 press_start、power_before_press、auto_adjusted=false | ST_BLE_ACTIVE |
| BTN_REPEAT | 若 Steady 模式：adjust_power(+step)、auto_adjusted=true | ST_BLE_ACTIVE |
| BTN_UP (duration < 500) | adjust_power(-step) | ST_BLE_ACTIVE |
| BTN_UP (500 ≤ duration < 2000) | （REPEAT 已处理） | ST_BLE_ACTIVE |
| BTN_UP (duration ≥ 2000) | enter_confirm_window()（回滚+快闪） | ST_WIFI_CONFIRM |
| CONN_CHANGED | 更新 s_connected、刷新 LED | ST_BLE_ACTIVE |
| ERG_TARGET | set_erg_target()（可能自动切 ERG 模式） | ST_BLE_ACTIVE |
| ERG_CLEAR | clear_erg_target() | ST_BLE_ACTIVE |
| RUN_* | 跑步控制 | ST_BLE_ACTIVE |

### ST_WIFI_CONFIRM 状态

| 事件 | 动作 | 下一个状态 |
|------|------|-----------|
| BTN_DOWN | trigger_wifi() → s_wifi_trigger() | ST_WIFI_CONFIG |
| tick（超时） | 取消确认、回到 idle LED | ST_BLE_ACTIVE |
| 其他事件 | 忽略（确认窗口内不处理调功） | ST_WIFI_CONFIRM |

### ST_WIFI_CONFIG 状态

| 事件 | 动作 | 下一个状态 |
|------|------|-----------|
| WIFI_DONE | s_wifi_exit() → radio_ctrl_exit_wifi()、刷新 LED | ST_BLE_ACTIVE |
| 其他事件 | 忽略（配网中不处理按钮调功） | ST_WIFI_CONFIG |

> 注意：配网期间主循环**继续生成快照和 BLE 通知**，只是不处理按钮调功。

### ST_ERROR_RECOVERY 状态

| 事件 | 动作 | 下一个状态 |
|------|------|-----------|
| BLE 重试成功 | app_state_clear_error() | ST_BLE_ACTIVE |
| BLE 重试失败 | 保持错误态、周期重试 | ST_ERROR_RECOVERY |

---

## app_state_tick() — 周期检查

主循环每拍调用 `app_state_tick()`，处理依赖时间推进的逻辑：

1. 若当前状态为 `ST_WIFI_CONFIRM`：
   - 检查 `(now - confirm_start_ms) >= CONFIRM_WINDOW_MS`
   - 超时则取消确认，回到 `ST_BLE_ACTIVE`
2. 其他状态：无定时任务（WiFi 超时由 wifi_portal 的 esp_timer 管理）

---

## app_state_refresh_led() — LED 刷新

网页保存 LED 配置后调用，根据当前状态和连接状态重新应用 LED 模式：

| 当前状态 | LED 模式 |
|----------|----------|
| ST_BLE_ACTIVE | 未连→LED_SLOW（慢闪），已连→LED_SOLID（常亮） |
| ST_WIFI_CONFIRM | LED_FAST（快闪） |
| ST_WIFI_CONFIG | LED_WIFI（WiFi 慢闪） |
| ST_ERROR_RECOVERY | LED 错误模式（配置中的 err 场景） |
