# BikePower C — 项目进度

> 基线方案：[docs/05-cidf-nimble-rewrite.md](docs/05-cidf-nimble-rewrite.md)
> 目标固件版本：v3.1.2（C + ESP-IDF v5.5.4 + NimBLE，ESP32-C3）
> 参考仓库：git@gitee.com:mengen4jv/bikepower-c.git

---

## 阶段进度总览

| 阶段 | 内容 | 状态 | 验收 |
|------|------|------|------|
| 0 | 工程骨架（CMake/分区/sdkconfig） | ✅ 完成 | 空工程启动日志 |
| 1 | NimBLE CPS 单服务最小闭环 | ✅ 完成 | 手机搜到 BikePower 看到功率 |
| 2 | 领域层移植 + 五服务 + FTMS ERG | ✅ 完成 | 五服务通知 + ERG 收敛 + host 单测 573 断言 |
| 3 | GPIO 中断按钮 + LED 任务 + 状态机 | ✅ 代码完成（物理按钮待人工确认） | 短/中/长按 + 二次确认 + LED 各态 |
| 4 | 配置持久化（NVS load/save/migrate） | ✅ 完成 | 调功后断电重启功率由260W恢复为200W，host 单测 568 断言 |
| 5 | WiFi 配网（radio_ctrl 软切换 + AP + HTTP） | ✅ 代码完成（配网真机流程待人工确认） | 长按确认→配网→保存→重启→BLE 恢复 |
| 6 | 稳定性与发版（WDT/coredump/老化/tag） | ✅ 完成 | v3.0.0 已发版 |
| 7 | M5Stamp C3 适配（板型检测/SK6812/配网页面修复） | ✅ 完成 | v3.1.0 已发版 |

---

## 阶段3 完成情况（当前）

### 已实现

| 模块 | 文件 | 说明 |
|------|------|------|
| 按钮 | [main/button.c](main/button.c) | GPIO9 ANYEDGE 中断 → 信号量唤醒 button_task → 去抖/计时/自动重复 → app_queue |
| LED | [main/led.c](main/led.c) | GPIO12 独立任务，OFF/SLOW(1s)/SOLID/FAST(200ms) 四态 |
| 状态机 | [main/app_state.c](main/app_state.c) | 7 态枚举，消费按钮/连接事件，调功 + 二次确认窗口 + LED 驱动 |
| 事件 | [main/app_events.h](main/app_events.h) | BTN_DOWN/REPEAT/UP + CONN_CHANGED |
| 主循环 | [main/app_main.c](main/app_main.c) | 事件队列 drain + 50ms 快照 + 连接轮询 + TWDT 喂狗 |

### 按钮语义（与 MicroPython 行为一致）

| 操作 | 时长 | 动作 |
|------|------|------|
| 短按 | <500ms | 功率 -20W |
| 长按调功 | ≥500ms | 每 200ms 功率 +20W（仅 steady 模式） |
| 长按确认 | ≥2000ms 释放 | 回滚本次自动调功，进入确认窗口（LED 快闪） |
| 二次确认 | 窗口内再按 | 触发 WiFi 配网（阶段5接入 wifi_trigger 回调） |
| 窗口超时 | 3000ms | 取消，恢复 BLE_ACTIVE |

### 真机验证（已通过）

- 三任务启动：LED/STATE/BTN 日志正常，state=ST_BLE_ACTIVE
- BLE 连接事件驱动状态机：连接 `connected=1`，断开 `connected=0`
- 空闲堆 220KB

### 待人工确认（需物理操作）

- [ ] 短按一次 → 串口出现 `短按 功率-20W`
- [ ] 按住 1s 松开 → 串口出现多次 `adjust` 且功率上升（steady 模式下）
- [ ] 按住 ≥2s 松开 → `长按2秒，LED快闪等待二次确认`，LED 快闪
- [ ] 快闪窗口内再按 → `二次确认，进入WiFi配网模式`（当前阶段3 wifi_trigger 未注册，会打印"未就绪"并恢复）
- [ ] 不按等待 3s → `确认窗口超时`
- [ ] LED 灯效：广播慢闪 / 连接常亮 / 确认快闪

---

## 阶段4 完成情况

### 已实现

| 模块 | 文件 | 说明 |
|------|------|------|
| 配置 schema | [components/bikepower_domain/config_store.c](components/bikepower_domain/config_store.c) | 纯 C，defaults/validate（逐字段越界夹紧）/migrate（版本不匹配重置默认），可 host 单测 |
| NVS 桥接 | [main/nvs_store.c](main/nvs_store.c) | namespace=bikepower 短key，load（缺失/版本不匹配用默认并回写）+ save（脏标记落盘） |
| 主循环接入 | [main/app_main.c](main/app_main.c) | 启动 load + clear_dirty，10s 心跳脏标记落盘 |

- NVS namespace `bikepower`，短 key：`sv`/`power`/`cadence`/`mode`/`hr`/`hashr`/`weight`/`ftp`/`thr_hr`/`ride`/`devname`（schema v5：心率持久化替代已移除的 tune_road）
- 旧 MicroPython JSON 配置不迁移，首刷重置为默认

### 真机验证（已通过）

- 首刷启动用默认值并回写：`p=260W c=90RPM mode=0 hr=150 w=70 ftp=300 thr=190 ride=0`
- 短按 3 次（每次 -20W）→ 等待落盘 → 断电重启 → 功率恢复为 `p=200W`，证明持久化生效
- host 单测 568 断言全过（新增 config_store 23 断言）

---

## 阶段5 完成情况

### 已实现

| 模块 | 文件 | 说明 |
|------|------|------|
| 射频控制 | [main/radio_ctrl.c](main/radio_ctrl.c) | 唯一 BLE/WiFi 开关，方案 A：停 BLE → 延时释放射频 → 启 WiFi AP；配网结束/超时 `esp_restart()` 恢复 BLE |
| 配网门户 | [main/wifi_portal.c](main/wifi_portal.c) | esp_http_server（SoftAP 由 radio_ctrl 启动），内嵌 snprintf HTML 分块发送；移植旧版完整 UI（iOS 三卡 + 训练区间染色 + 动态碎片页）；路由 `/` `/config` `/success` `/more` `/draft` `/ui/module` `/closing` `/time` `/renew` `/mem_stats` `/favicon.ico` |
| 状态机接入 | [main/app_state.c](main/app_state.c) | ST_WIFI_CONFIRM → ST_WIFI_CONFIG，经 `wifi_trigger` 回调触发 radio_ctrl |

- 超时方案 A：180s（`WIFI_SHUTDOWN_MS`）无续期自动重启恢复 BLE，配网保存后 5s 延迟重启
- 前端每秒轮询 `/time` 显示剩余时间，`/renew` 续期；`/mem_stats` 返回堆内存 JSON，`/favicon.ico` 返回 204
- 配网表单可改：功率/踏频/心率/模式/体重/FTP/阈值心率/配速机器人/设备名，保存后写入 power_engine + NVS

### 待人工确认（需配网真机流程）

- [ ] 长按 + 二次确认 → 进入配网，手机连 `BikePower` 热点
- [ ] 浏览器访问 `192.168.4.1` → 修改参数 → 保存 → 设备重启 → BLE 恢复且参数生效
- [ ] 180s 无操作 → 自动超时重启恢复 BLE
- [ ] 连上热点后跑 `python3 tests/stage5_portal_test.py` 校验 HTTP 路由（首页/more/碎片/time/mem_stats/favicon/success 守卫）

---

## 阶段6 完成情况

### 已实现

| 项 | 文件 | 说明 |
|------|------|------|
| Task WDT 覆盖 | [main/app_main.c](main/app_main.c) / [main/led.c](main/led.c) | app_main 主循环与 led_task 均 `esp_task_wdt_add(NULL)` + 周期 `esp_task_wdt_reset()`；IDLE 任务由 `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0` 监控 |
| 低内存监控 | [main/app_main.c](main/app_main.c) | 心跳检查空闲堆：低于 `HEAP_WARN_BYTES`(40KB) 告警，低于 `HEAP_CRIT_BYTES`(20KB) 严重告警 |
| Coredump | [partitions.csv](partitions.csv) / [sdkconfig.defaults](sdkconfig.defaults) | 新增 `coredump` 分区（64KB @ 0x1d0000），ELF 格式写 flash，CRC32 校验 |

- button_task 为中断驱动、空闲时无限阻塞等唤醒，**刻意不订阅 WDT**（订阅会在无按键 5s 时误复位）
- 阈值常量集中在 [main/bikepower_config.h](main/bikepower_config.h)：`HEAP_WARN_BYTES` / `HEAP_CRIT_BYTES`

### 待人工确认（需真机 + 不可逆操作）

- [ ] 72h 老化测试：长时间运行无 coredump、无堆持续下降（跑 `tests/stage5_portal_test.py` / `ble_selftest.py` 辅助）
- [x] 发版前 `sdkconfig.defaults` 日志级别改 `CONFIG_LOG_DEFAULT_LEVEL_WARN`（已设为 WARN）
- [x] DIS 固件版本号与 `FIRMWARE_VERSION` 同步确认（代码已对齐 v3.1.2，`ble_selftest.py` 已自动校验厂商/型号/固件三字段，真机待跑）
- [ ] 打 tag v3.1.2 + Gitee Release（老化通过后由人工执行）

---

## 阶段7 完成情况（v3.1.0）

### 已实现

| 模块 | 文件 | 说明 |
|------|------|------|
| 板型检测 | [main/board_detect.c](main/board_detect.c) | 运行时自动检测通用 ESP32-C3 或 M5Stamp C3（GPIO3 电平判断：M5Stamp 按钮上拉读高，通用板悬空下拉读低） |
| SK6812 LED | [main/led.c](main/led.c) | M5Stamp C3 使用 GPIO2 RMT bytes encoder 驱动 RGB LED，颜色映射：蓝=广播/绿=连接/黄=确认 |
| 按钮适配 | [main/button.c](main/button.c) | 引脚改为运行时 `board_btn_pin()` 获取，M5Stamp 用 GPIO3 |
| 配网修复 | [main/wifi_portal.c](main/wifi_portal.c) | 修复 `syncMode()` 多余 `}` 导致 JS 语法错误，模式切换恢复正常 |
| host 单测修复 | [components/bikepower_domain/hr_model.c](components/bikepower_domain/hr_model.c) | 添加 `M_PI` 兼容定义，host 单测编译通过 |

### 真机验证（已通过）

- 板型检测：`自动检测板型：M5Stamp C3（GPIO3=1）`
- SK6812 RGB LED：蓝色慢闪（广播）/ 绿色常亮（连接）/ 黄色快闪（确认）
- BLE 五服务 + FTMS ERG：正常
- 空闲堆：173KB（> 80KB 阈值）
- 配网页面模式切换：三段切换正常
- host 单测：581 通过，0 失败

---

## 已知限制 / 注意事项

- ESP-IDF 已升级至 v5.5.4（修复了 NimBLE host 潜在断连问题；工具链 esp-14.2.0_20260121）。
- WiFi/BLE 射频互斥：进入配网前必须先停 BLE（见 [.trae/rules/wifi-ble-mutex.md](.trae/rules/wifi-ble-mutex.md)）
- 物理按钮/LED 灯效需人工确认，自动化测试只能覆盖 BLE 连接驱动的状态迁移
