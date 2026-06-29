# WiFi/BLE 射频共用规则（C / ESP-IDF 版）

<!-- stability: tier-1 (硬件级约束，不可绕过) -->
<!-- rule-id-prefix: WB-MUTEX -->
<!-- applies-to: main/radio_ctrl.c, main/wifi_portal.c, main/ble_bikepower.c, main/app_state.c, main/app_main.c -->

> WiFi/BLE 射频共用的唯一规则入口。硬件细节见 `.trae/rules/hardware-constraints.md`。
> ESP32-C3 单路 RF，但 sdkconfig 已启用 `CONFIG_ESP_COEX_SW_COEXIST_ENABLE`，支持软件共存（coex）。
> 默认采用共存模式（不停 BLE 不重启），仅在共存初始化失败时降级为独占方案（停 BLE + 重启）。

## 核心约束

- **WB-MUTEX-1**：禁止绕过 `radio_ctrl` 直接调用 `esp_wifi_*` 启停。所有 WiFi 启停必须走 `radio_ctrl_enter_wifi` / `radio_ctrl_exit_wifi`，确保 netif/handle 句柄按序管理。
- **WB-MUTEX-2**：禁止设备开机自动启动 WiFi。配网模式必须由用户长按按钮触发并经过二次确认（app_state ST_WIFI_CONFIRM → ST_WIFI_CONFIG）。不得在 `app_main` 启动阶段自动启 WiFi。
- **WB-MUTEX-3**：必须启用 `CONFIG_ESP_COEX_SW_COEXIST_ENABLE`。共存模式下 WiFi 与 BLE 由 coex 模块按时间片仲裁，无需停 BLE 即可启 WiFi。

## 运行时约束（共存模式）

- **WB-MUTEX-4**：进入配网（`radio_ctrl_enter_wifi`）时 BLE 保持在线继续通知，不停止广播、不断开现有连接、不停 NimBLE host。
- **WB-MUTEX-5**：配网期间主循环继续生成快照推 BLE 通知，确保骑行 App 实时数据不中断。
- **WB-MUTEX-6**：配网结束（保存成功或 180s 超时）由 `wifi_portal` 通过 done 回调投递 `APP_EVT_WIFI_DONE`，状态机收到后调用 `radio_ctrl_exit_wifi` 关闭 HTTP + SoftAP，回到 `ST_BLE_ACTIVE`。**不重启设备**。
- **WB-MUTEX-7**：配置通过 `power_engine_set_*` 系列 setter 实时生效，主循环 50ms 周期通过 BLE 通知新值；NVS 持久化在 setter 内同步完成。
- **WB-MUTEX-8**：唯一的 RF 开关入口是 `radio_ctrl.c`。其他模块不得直接操作 `esp_wifi_*` 与 NimBLE 启停。

## 失败兜底（独占降级）

仅当共存模式下 `radio_ctrl_start_wifi_ap()` 返回错误（资源耗尽/初始化失败）时，降级为独占方案：

1. 调用 `ble_bikepower_stop()` 停止 BLE 释放 RF。
2. 等待 RF 释放（`RADIO_BLE_RELEASE_WAIT_MS`）。
3. 重试 `radio_ctrl_start_wifi_ap()`。
4. 仍失败则 `esp_restart()` 恢复 BLE。

降级路径仅作为最后兜底，正常运行路径不应触发。

## 运行时数据流

```
按钮长按 + 二次确认
  → app_state ST_WIFI_CONFIG
  → radio_ctrl_enter_wifi（BLE 不停）
      → radio_ctrl_start_wifi_ap（coex 仲裁）
      → wifi_portal_start（HTTP 配网，配置通过 power_engine setter 实时生效到 BLE 通知）
  → 用户保存 / 180s 超时
  → wifi_portal done 回调 → APP_EVT_WIFI_DONE
  → app_state 收到事件 → radio_ctrl_exit_wifi（关 HTTP + SoftAP）→ ST_BLE_ACTIVE
```

## 违反后果

| 违规行为 | 后果 |
|----------|------|
| 绕过 radio_ctrl 直接 esp_wifi_start | netif/handle 泄漏，重复初始化崩溃 |
| 开机自动启动 WiFi | 无限重启循环风险 |
| 配网期间停 BLE | 骑行 App 断连，违反"设备不重启"高优原则 |
| 配网结束 esp_restart | 骑行 App 断连，违反"设备不重启"高优原则 |

## 相关规则

- `.trae/rules/hardware-constraints.md` — ESP32-C3 硬件参数与异常 SOP
- `docs/archive/01-cidf-nimble-rewrite.md` — 阶段5 软切换方案细节
- `AGENTS.md` — Hard Rules
