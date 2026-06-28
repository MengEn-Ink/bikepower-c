# 硬件约束规则（C / ESP-IDF 版）

<!-- stability: tier-1 (极稳定，仅随硬件变更) -->
<!-- rule-id-prefix: HC -->
> 只记录约束和禁止项，详细行为见 docs/05-cidf-nimble-rewrite.md 对应章节。

## ESP32-C3 硬件限制

### WiFi/BLE 射频共用
- ESP32-C3 只有一路 RF，但当前 sdkconfig 启用 `CONFIG_ESP_COEX_SW_COEXIST_ENABLE`，配网默认由 coex 模块仲裁 WiFi/BLE 时间片。
- **禁止**：绕过 `radio_ctrl` 直接调用 `esp_wifi_*` 启停 WiFi。
- **必须**：普通配网保持 BLE 在线；OTA 下载进入独占 STA、或共存初始化失败兜底时，才由 `radio_ctrl` 先停 NimBLE（停广播、断连接、`nimble_port_stop` / 关闭 host）。
- 详见 `.trae/rules/wifi-ble-mutex.md`。

### 内存约束
| 区域 | 大小 | 说明 |
|------|------|------|
| 片上 SRAM | 400 KB | 无 PSRAM |
| NimBLE host 启动后空闲堆 | ~200-220 KB（实测基线） | 低于 40KB 告警 / 低于 20KB 严重告警（HEAP_WARN_BYTES / HEAP_CRIT_BYTES） |
| WiFi AP 启动 | 额外 ~30-40 KB | 配网期间峰值 |

### Socket / HTTP 限制
- 配网用 `esp_http_server`，并发连接数受限，保持精简（少量并发）。
- 所有 socket/连接资源在出错路径也要释放。

### BLE 约束（NimBLE）
- 使用 NimBLE host（peripheral 角色）。
- **禁止**：通知/GATT 回调、ISR 中做阻塞或重分配大缓冲。
- **必须**：MTU、广播间隔、通知间隔、UUID、连接数从 `bikepower_config.h` 读取。
- **必须**：BLE/NimBLE 初始化失败时走可控错误恢复（记录 + 必要时 `esp_restart()`）。

## GPIO 约束

### 按钮（GPIO9, 内部上拉，按下为低电平）
- 中断类型 `GPIO_INTR_ANYEDGE`；ISR 仅 `xSemaphoreGiveFromISR` 唤醒任务。
- 去抖、计时、自动重复在 button_task（任务上下文）完成，不在 ISR。
- 阈值与 `bikepower_config.h` 一致（SHORT_PRESS_MS / WIFI_BTN_HOLD_MS / CONFIRM_WINDOW_MS / BTN_AUTO_REPEAT_MS）。

### LED（GPIO12, 高电平有效）
- 独立 led_task 驱动，四态 OFF/SLOW(1s)/SOLID/FAST(200ms)。
- **禁止**：使用 GPIO12~17 的 QSPI 模式（被 Flash 占用）。

## FreeRTOS / 中断安全

| 约束 | 说明 |
|------|------|
| ISR 内 API | 只用 `*FromISR` 版本，禁止阻塞/日志/BLE |
| 任务间通信 | Queue（app_queue）+ 二值信号量（button wake） |
| Task WDT | 主循环 `esp_task_wdt_reset()` 喂狗；长任务订阅 WDT |
| 栈大小 | 每个 `xTaskCreate` 栈大小经实测确认，不随意缩小 |

## 异常处理标准操作

| 异常 | 标准操作 | 预防 |
|------|---------|------|
| NimBLE 初始化失败 | 记录 + `esp_restart()` | 启动序列校验返回码 |
| WiFi/BLE 共存冲突 | 统一走 radio_ctrl；配网 coex，共存失败才停 BLE 兜底 | 启动前检查 RF 状态 |
| Task WDT 超时 | 检查阻塞点，喂狗覆盖全部长任务 | 主循环周期喂狗 |
| 串口被占用 | 释放占用进程 | 部署脚本检测 |
