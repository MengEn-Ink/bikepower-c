# WiFi/BLE 互斥规则（C / ESP-IDF 版）

<!-- stability: tier-1 (硬件级约束，不可绕过) -->
<!-- rule-id-prefix: WB-MUTEX -->
<!-- applies-to: main/radio_ctrl.c, main/wifi_portal.c, main/ble_bikepower.c, main/app_state.c -->

> WiFi/BLE 互斥的唯一规则入口。硬件细节见 `.trae/rules/hardware-constraints.md`。

## 核心约束

- **WB-MUTEX-1**：禁止在 BLE 仍激活时启动 WiFi。ESP32-C3 只有一路 RF。`esp_wifi_start()` 前必须先停止 NimBLE（停广播、断开所有连接、停止 host）。
- **WB-MUTEX-2**：禁止设备开机自动启动 WiFi。配网模式必须由用户长按按钮触发并经过二次确认（app_state ST_WIFI_CONFIRM → ST_WIFI_CONFIG）。不得在 `app_main` 启动阶段自动启 WiFi。

## 运行时约束

- **WB-MUTEX-3**：WiFi 启动前 BLE 必须完全停止广播和所有连接，等待 RF 释放，再启 WiFi AP。
- **WB-MUTEX-4**：配网阶段结束后，设备重启（`esp_restart()`）恢复 BLE。v3.0 不实现 WiFi/BLE 实时切换。
- **WB-MUTEX-5**：唯一的 RF 开关入口是 `radio_ctrl.c`（阶段5实现，方案 C：软切换 + 兜底重启）。其他模块不得直接操作 `esp_wifi_*` 与 NimBLE 启停。

## 阶段5 实现要点（radio_ctrl）

```
app_state ST_WIFI_CONFIG 触发 wifi_trigger 回调
  → radio_ctrl: ble_bikepower 停止（停广播/断连/停 host）
  → 等待 RF 释放
  → esp_wifi 初始化 + AP 启动
  → wifi_portal: esp_http_server 配网
  → 保存配置 / 180s 超时
  → esp_restart() 恢复 BLE
```

## 违反后果

| 违规行为 | 后果 |
|----------|------|
| BLE 未停即启 WiFi | RF 冲突，WiFi 启动失败 / 崩溃 |
| 开机自动启动 WiFi | 无限重启循环风险 |

## 相关规则

- `.trae/rules/hardware-constraints.md` — ESP32-C3 硬件参数与异常 SOP
- `docs/01-cidf-nimble-rewrite.md` — 阶段5 软切换方案细节
- `AGENTS.md` — Hard Rules
