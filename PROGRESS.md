# BikePower C — 项目进度

> 基线方案：[docs/01-cidf-nimble-rewrite.md](docs/01-cidf-nimble-rewrite.md)
> 目标固件版本：v3.0.0（C + ESP-IDF v5.5.1 + NimBLE，ESP32-C3）
> 参考仓库：git@gitee.com:mengen4jv/bikepower-c.git

---

## 阶段进度总览

| 阶段 | 内容 | 状态 | 验收 |
|------|------|------|------|
| 0 | 工程骨架（CMake/分区/sdkconfig） | ✅ 完成 | 空工程启动日志 |
| 1 | NimBLE CPS 单服务最小闭环 | ✅ 完成 | 手机搜到 BikePower 看到功率 |
| 2 | 领域层移植 + 五服务 + FTMS ERG | ✅ 完成 | 五服务通知 + ERG 收敛 + host 单测 545 断言 |
| 3 | GPIO 中断按钮 + LED 任务 + 状态机 | ✅ 代码完成（物理按钮待人工确认） | 短/中/长按 + 二次确认 + LED 各态 |
| 4 | 配置持久化（NVS load/save/migrate） | ✅ 完成 | 调功后断电重启功率由260W恢复为200W，host 单测 568 断言 |
| 5 | WiFi 配网（radio_ctrl 软切换 + AP + HTTP） | ⬜ 未开始 | 长按确认→配网→保存→重启→BLE 恢复 |
| 6 | 稳定性与发版（WDT/coredump/老化/tag） | ⬜ 未开始 | 72h 老化无 coredump，打 tag v3.0.0 |

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

- NVS namespace `bikepower`，短 key：`sv`/`power`/`cadence`/`mode`/`tune`/`weight`/`ftp`/`thr_hr`/`ride`
- 旧 MicroPython JSON 配置不迁移，首刷重置为默认

### 真机验证（已通过）

- 首刷启动用默认值并回写：`p=260W c=90RPM mode=0 tune=50 w=70 ftp=300 thr=190 ride=0`
- 短按 3 次（每次 -20W）→ 等待落盘 → 断电重启 → 功率恢复为 `p=200W`，证明持久化生效
- host 单测 568 断言全过（新增 config_store 23 断言）

---

## 待完成项（后续阶段）

### 阶段5：WiFi 配网
- 新建 `main/radio_ctrl.c`（唯一 BLE/WiFi 开关，方案 C：软切换 + 兜底重启）
- `main/wifi_portal.c`：esp_wifi AP + esp_http_server + EMBED_FILES HTML
- 路由表对齐 MicroPython `09-web-api-reference.md`
- app_state 注册 wifi_trigger 回调，ST_WIFI_CONFIG 期间暂停 BLE 通知
- 180s 超时关闭 + 重启恢复 BLE

### 阶段6：稳定性与发版
- 所有长任务订阅 Task WDT；coredump 分区；低内存监控
- 72h 老化测试
- release 编译 + CHANGELOG + 测试报告 + tag v3.0.0 + DIS 固件版本同步

---

## 已知限制 / 注意事项

- 当前 ESP-IDF 基线 v5.5.1-dirty（本地工具链）；方案推荐 v6.0.1 或 v5.5.4，量产前需升级并记录理由
- WiFi/BLE 射频互斥：进入配网前必须先停 BLE（见 [.trae/rules/wifi-ble-mutex.md](.trae/rules/wifi-ble-mutex.md)）
- 物理按钮/LED 灯效需人工确认，自动化测试只能覆盖 BLE 连接驱动的状态迁移
