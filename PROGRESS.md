# BikePower C — 项目进度

> 基线方案：[docs/archive/01-cidf-nimble-rewrite.md](docs/archive/01-cidf-nimble-rewrite.md)
> 当前固件版本：v3.1.8（C + ESP-IDF v5.5.4 + NimBLE，ESP32-C3）
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
| 5 | WiFi 配网（radio_ctrl 共存 + AP + HTTP，设备不重启） | ✅ 代码完成（配网真机流程待人工确认） | 长按确认→配网→保存→关 WiFi 回到 BLE，BLE 全程不断 |
| 6 | 稳定性与发版（WDT/coredump/老化/tag） | ✅ 完成 | v3.0.0 已发版 |
| 7 | M5Stamp C3 适配（板型检测/SK6812/配网页面修复） | ✅ 完成 | v3.1.0 已发版 |
| 8 | WiFi/BLE 共存配网（v3.1.3） | ✅ 代码完成（真机待确认） | BLE 全程在线 + 配置实时生效 + 不重启 |
| 9 | Captive Portal + OTA 实现（v3.1.5） | ✅ 完成 | DNS 劫持自动弹配网页 + 4MB OTA 分区表 + OTA 防降级/自检回滚，真机升级降级测试通过 |
| 10 | 家庭网络配置 + OTA 在线检查（v3.1.6-v3.1.8） | ✅ 代码完成（真机待确认） | WiFi 扫描选网 + STA 连接路由器 + SNTP + HTTPS 拉 latest.json + 在线下载升级；进度反馈 + JSON/XSS 加固 + fd 耗尽修复 + 密码不回显 |

---

## 第七轮 P0 真机验收清单（当前交付口径）

本清单用于交付前集中跟踪“代码/自动化已就绪”和“仍需人工真机确认”的边界。未勾选项不得在发版说明中标为已完成；物理按钮、LED、系统级 Captive Portal 弹窗、72h 老化和 Gitee Release 创建均保留人工确认。

### 就绪交付物

- [x] 固件版本统一为 `v3.1.8`，DIS 固件字段来自 `FIRMWARE_VERSION = "3.1.8"`，`tests/ble_selftest.py` 会校验厂商/型号/固件三字段。
- [x] 普通配网链路走 `radio_ctrl` + coex 共存：长按二次确认进入配网，BLE 默认不停，保存或超时后关闭 WiFi 原地回 `ST_BLE_ACTIVE`。
- [x] 配网门户路由与前端校验已具备自动化覆盖：`tests/stage5_portal_test.py`、`tests/check_portal_structure.py`、`tests/check_portal_js.py`。
- [x] Captive Portal DNS 与 10 个系统探测端点已实现：Android/Chrome、iOS/macOS、Windows、Firefox、Kindle 探测路由统一 302 到首页。
- [x] OTA 在线检查/下载升级已实现：`/ota`、`/ota/check`、`/ota/status`、`/ota/upgrade`，家庭 WiFi 凭证从 `/more` 保存的 NVS namespace 读取，升级阶段进入 OTA 独占 STA。
- [x] OTA 页面安全与体验已补强：`/ota/upgrade/status` 轮询展示下载/写入进度，JSON 输出转义，远端 changelog 前端转义渲染，`tests/ota_local_mock.py --self-test` 覆盖进度与 XSS 回归。
- [x] 配网入口交互已补强：`/more` 个人信息前端范围校验，首页保存成功带 `saved=1` 强反馈，不关闭 WiFi 时可继续修改。
- [x] 配置导入/导出基础能力已落到领域层：`config_store_serialize` / `config_store_deserialize` 支持 JSON 往返、缺字段默认值和范围夹紧。
- [x] Release 产物标准化脚本已就绪：`scripts/build_all.sh` 可生成 merged bin、SHA256、manifest 与 `latest.json` 草案；脚本不自动打 tag、不创建 Gitee Release。
- [x] 自动化门禁入口已就绪：`tests/check_config_consistency.py` 与 `scripts/tools/quality_gate.py` 覆盖配置一致性、host 单测、构建和静态检查聚合。

### 待人工确认（P0）

- [ ] 按钮短按：短按一次后串口出现 `短按 功率-20W`，BLE 通知功率同步下降 20W。
- [ ] 按钮长按调功：steady 模式下按住约 1s，串口出现多次 `adjust`，功率按 20W 步进上升。
- [ ] 按钮长按确认：按住 ≥2s 松开后出现 `长按2秒，LED快闪等待二次确认`，LED 进入确认快闪。
- [ ] 二次确认进入配网：确认窗口内再按一次，串口出现 `二次确认，进入WiFi配网模式`，设备开放 `BikePower` 热点。
- [ ] 确认窗口超时：不二次按键等待 3s，串口出现 `确认窗口超时` 并回到 BLE 活跃态。
- [ ] LED 灯效人工观察：通用板广播慢闪/连接常亮/确认快闪；M5Stamp C3 蓝色慢闪/绿色常亮/黄色快闪。
- [ ] 配网真机流程：手机连接 `BikePower` 热点，访问 `192.168.4.1`，修改功率/踏频/心率/模式/设备名并保存，确认 BLE 保持在线且参数实时生效。
- [ ] 配网关闭流程：点击“保存并关闭 WiFi”后 5s 收尾退出，HTTP/SoftAP/DNS 停止，设备不重启并回到 BLE。
- [ ] 配网超时流程：180s 无操作自动关闭 WiFi，回到 BLE，串口无异常重启。
- [ ] Captive Portal 多系统弹窗：iOS、Android、Windows 分别连接 `BikePower` 热点后能自动弹出配网页；不弹窗时手动访问 `192.168.4.1` 仍可配网。
- [ ] Captive Portal 生命周期：退出配网后串口确认 DNS 任务停止，不再响应劫持。
- [ ] OTA 真机在线检查：在 `/more` 保存家庭 WiFi 后进入 `/ota`，点击检查更新可完成 STA 连接、SNTP、HTTPS 拉取 `latest.json`，失败时页面给出明确错误。
- [ ] OTA 真机升级：准备远端更高版本 `latest.json` 和 merged bin，点击升级后下载写入 OTA 分区，重启进入新固件并通过自检确认。
- [ ] OTA 防降级/无更新：远端版本低于或等于本机 `3.1.8` 时不得解锁升级按钮，不写 flash。
- [ ] 72h 老化：长时间运行无 coredump、无 WDT、无堆持续下降；建议周期性跑 `tests/ble_selftest.py` 和 `tests/stage5_portal_test.py` 采样。
- [ ] Release 人工步骤：老化通过后执行 `scripts/build_all.sh 3.1.8`，人工填写 `latest.json` changelog，校验 SHA256，打 `v3.1.8` tag，创建 Gitee Release 并上传 merged bin/manifest/latest.json。

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
- [ ] 快闪窗口内再按 → `二次确认，进入WiFi配网模式`，进入阶段5配网链路
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
| 射频控制 | [main/radio_ctrl.c](main/radio_ctrl.c) | 唯一 WiFi/BLE 射频入口；配网走 coex 共存（BLE 不停），共存失败才降级为停 BLE 兜底 |
| 配网门户 | [main/wifi_portal.c](main/wifi_portal.c) | esp_http_server（SoftAP 由 radio_ctrl 启动），内嵌 snprintf HTML 分块发送；移植旧版完整 UI（iOS 三卡 + 训练区间染色 + 动态碎片页）；路由 `/` `/config` `/success` `/more` `/draft` `/ui/module` `/closing` `/time` `/renew` `/mem_stats` `/favicon.ico` |
| 状态机接入 | [main/app_state.c](main/app_state.c) | ST_WIFI_CONFIRM → ST_WIFI_CONFIG，经 `wifi_trigger` 回调触发 radio_ctrl |

- 超时方案：180s（`WIFI_SHUTDOWN_MS`）无续期自动关闭 WiFi 回到 BLE，保存并关闭后 5s 收尾再退出配网
- 前端每秒轮询 `/time` 显示剩余时间，`/renew` 续期；`/mem_stats` 返回堆内存 JSON，`/favicon.ico` 返回 204
- 配网表单可改：功率/踏频/心率/模式/体重/FTP/阈值心率/配速机器人/设备名，保存后写入 power_engine + NVS

### 待人工确认（需配网真机流程）

- [ ] 长按 + 二次确认 → 进入配网，手机连 `BikePower` 热点
- [ ] 浏览器访问 `192.168.4.1` → 修改参数 → 保存并关闭 WiFi → BLE 保持/恢复在线且参数生效
- [ ] 180s 无操作 → 自动超时关闭 WiFi 回到 BLE
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
- [x] DIS 固件版本号与 `FIRMWARE_VERSION` 同步确认（代码已对齐 v3.1.8，`ble_selftest.py` 已自动校验厂商/型号/固件三字段，真机待跑）
- [ ] 打 tag v3.1.8 + Gitee Release（老化通过后由人工执行）

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

## 阶段9 完成情况（v3.1.4 Captive Portal + OTA 规划）

### 已实现

| 模块 | 文件 | 说明 |
|------|------|------|
| Captive Portal | [main/captive_portal.c](main/captive_portal.c) / [main/captive_portal.h](main/captive_portal.h) | 自研轻量 DNS 服务器（UDP 53 劫持所有 A 查询返回 192.168.4.1）+ OS 探测端点 302 重定向 handler |
| DNS 生命周期 | [main/radio_ctrl.c](main/radio_ctrl.c) | AP 启动后启动 DNS，退出配网时停止（captive_portal_start_dns / captive_portal_stop_dns） |
| 探测端点路由 | [main/wifi_portal.c](main/wifi_portal.c) | 注册 10 个 OS 探测端点（/generate_204、/hotspot-detect.html、/ncsi.txt 等），统一 302 重定向到首页 |
| 4MB OTA 分区表 | [partitions.csv](partitions.csv) | ota_0/ota_1 互切（各 1.75MB）+ otadata，无 factory；固件 1017KB，每分区 45% 空闲 |
| OTA 回滚支持 | [sdkconfig.defaults](sdkconfig.defaults) | CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y，新固件 PENDING_VERIFY 自检通过才确认 |
| OTA 落地方案 | [docs/archive/05-ota-implementation-plan.md](docs/archive/05-ota-implementation-plan.md) | 详细方案：HTTPS 在线下载 + 流式 esp_ota_write + 10s 自检回滚 + 防降级（上传方式已移除） |

### 覆盖的 OS 探测端点

Android/Chrome、iOS/macOS、Windows、Firefox、Kindle 共 10 个端点，统一 302 重定向触发系统自动弹出 Captive Portal 窗口。

### 资源占用

- DNS 任务栈 3072 字节 + UDP 缓冲 512 字节（栈上）
- 固件增量 ~2KB
- 配网态空闲堆仍 >80KB 告警线

### 待人工确认（需真机验证）

- [ ] iOS 连上 BikePower 热点后自动弹出配网页
- [ ] Android 连上后自动弹出配网页
- [ ] Windows 连上后浏览器提示需登录
- [ ] 不弹窗时手动访问 192.168.4.1 仍可配网（降级兼容）
- [ ] 退出配网后 DNS 任务已停止（串口日志）
- [ ] 首次烧录需 `esptool.py erase_flash`（2MB→4MB 分区表变更）

### OTA 实施（v3.1.6 已完成）

详见 [docs/user/04-ota-user-guide.md](docs/user/04-ota-user-guide.md)，核心实现：
1. `main/ota_downloader.c` HTTPS 在线下载版本信息 + 固件，分块 esp_ota_write
2. `main/ota_update.c` PENDING_VERIFY 自检确认/回滚 + 语义版本比较工具（上传方式已移除）
3. `main/wifi_portal.c` `/ota` 在线检查更新页 + `/ota/check`/`/ota/status`/`/ota/upgrade` 异步任务
4. 家庭 WiFi 凭证统一在 `/more` 家庭网络配置，OTA 页只读展示，从 NVS 读取
5. 防降级：`ota_version_compare > 0` 仅远端更新才提示升级

---

## 已知限制 / 注意事项

- ESP-IDF 已升级至 v5.5.4（修复了 NimBLE host 潜在断连问题；工具链 esp-14.2.0_20260121）。
- WiFi/BLE 共存：配网期间 BLE 保持在线（`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`），仅资源初始化失败时降级为停 BLE + 重启（见 [.trae/rules/wifi-ble-mutex.md](.trae/rules/wifi-ble-mutex.md)）
- 物理按钮/LED 灯效需人工确认，自动化测试只能覆盖 BLE 连接驱动的状态迁移
