# BikePower C — 项目进度

> 基线方案：[docs/archive/01-cidf-nimble-rewrite.md](docs/archive/01-cidf-nimble-rewrite.md)
> 当前固件版本：v4.1.1（C + ESP-IDF v5.5.4 + NimBLE，ESP32-C3）
> 参考仓库：git@gitee.com:mengen4jv/bikepower-c.git

---

## 阶段进度总览

| 阶段 | 内容 | 状态 | 验收 |
|------|------|------|------|
| 0 | 工程骨架（CMake/分区/sdkconfig） | ✅ 完成 | 空工程启动日志 |
| 1 | NimBLE CPS 单服务最小闭环 | ✅ 完成 | 手机搜到 BikePower 看到功率 |
| 2 | 领域层移植 + 五服务 + FTMS ERG | ✅ 完成 | 五服务通知 + ERG 收敛 + host 单测持续扩展（本轮基线 5892 断言） |
| 3 | GPIO 中断按钮 + LED 任务 + 状态机 | ✅ 代码完成（物理按钮待人工确认） | 短/中/长按 + 二次确认 + LED 各态 |
| 4 | 配置持久化（NVS load/save/migrate） | ✅ 完成 | 调功后断电重启功率由260W恢复为200W，host 单测持续覆盖 |
| 5 | WiFi 配网（radio_ctrl 共存 + AP + HTTP，设备不重启） | ✅ 代码完成（配网真机流程待人工确认） | 长按确认→配网→保存→关 WiFi 回到 BLE，BLE 全程不断 |
| 6 | 稳定性与发版（WDT/coredump/老化/tag） | ✅ 完成 | v3.0.0 已发版 |
| 7 | M5Stamp C3 适配（板型检测/SK6812/配网页面修复） | ✅ 完成 | v3.1.0 已发版 |
| 8 | WiFi/BLE 共存配网（v3.1.3） | ✅ 代码完成（真机待确认） | BLE 全程在线 + 配置实时生效 + 不重启 |
| 9 | Captive Portal + OTA 实现（v3.1.5） | ✅ 完成 | DNS 劫持自动弹配网页 + 4MB OTA 分区表 + OTA 防降级/自检回滚，真机升级降级测试通过 |
| 10 | 家庭网络配置 + OTA 在线检查（v3.1.6-v3.1.8） | ✅ 代码完成（真机待确认） | WiFi 扫描选网 + STA 连接路由器 + SNTP + HTTPS 拉 latest.json + 在线下载升级；进度反馈 + JSON/XSS 加固 + fd 耗尽修复 + 密码不回显 |
| 11 | 配网门户前后端分离（v3.2.0） | ✅ 代码完成 + 真机 BLE 自测通过 | 21 HTML handler → 15 `/api/xxx` JSON + 前端三件套 `main/web/` + `inline_static.py` 编译时内联 + `mock_server.py` 本地调试 |
| 12 | 项目结构与文档梳理（v3.2.0） | ✅ 完成 | `test/` 与 `tests/` 合并评估（不合并）+ `tests/README.md` 7 大分类重写 + `docs/INDEX.md` 补全 workflows/domain 规则索引 |
| 13 | 移除 OTA + 家庭网络 + 网页端精简（v4.0.0） | ✅ 代码完成（真机待确认） | 删 ota_downloader/ota_update/ota_version/ota_selftest + `/api/ota/*`、`/api/wifi/*`、`/api/draft` 路由与前端视图；分区表双 OTA → 单 factory（仍 4MB）；关回滚配置；门户退化为纯 BLE 参数页（方案见 [docs/archive/07-remove-ota-plan.md](docs/archive/07-remove-ota-plan.md)） |
| 14 | Windows 开发环境适配 | ✅ 完成 | `idf_common.psm1` 共享模块 + `dev.ps1` 统一入口 + `build_all.ps1`/`flash_latest.ps1` 重构去重 + Python 脚本跨平台串口探测 |

---

## 2026-10-22 P0 协议与会话稳定性收口

当前工作区已完成源码修复与离线门禁，尚未形成新的双板真机验收结论：

- [x] CPS/CSC、FTMS Bike/Treadmill、RSC/HRS 统一经生产 `ble_payload_codec` 编码；独立 JSON golden vectors 同时验证 C encoder 与 Python parser。
- [x] RSC 距离单位修正为 0.1m，RSC Feature 修正为 2 字节 `0x0007`；FTMS Treadmill flags 修正为 `0x0504`。
- [x] 跑步 Start/Stop/Reset 接入非持久化 running 会话；暂停归零瞬时运动量并冻结距离/用时，Reset 清累计量且保持运行/暂停态。
- [x] ERG 改为非持久化会话；设定/清除目标不污染基础功率，持久化映射统一由 `config_store_from_engine()` 规范化，旧 NVS ERG 值加载时回退 steady。
- [x] WiFi 完成通知增加 pending latch 与可确认回调，主循环可靠消费，错误态退出仍统一幂等清理 HTTP/DNS/SoftAP。
- [x] 无设备门禁增加控制点接纳/标准 Status 契约、双次同设备重连、running 四阶段、profile-aware fake harness 与 WiFi done 静态契约；真机 profile harness 测试时强制 mask=7 并恢复原 profile/mask 与完整 running 持久配置，running 自测按四阶段逐 opcode indication + 多样本严格判定。
- [x] 整合后执行完整 `quality_gate.py`：81 PASS、2 个预期 dirty-worktree WARN、0 FAIL；包含 `idf.py build`、5892/0 host 断言与全部离线协议/harness 门禁。
- [ ] generic-c3 与 m5stamp-c3 分别执行 cycling、running `--control`、双次独立重连、广播开关、配网退出和标准客户端验证；结果写入当前 Git ID 对应证据。

历史 release/tag 的 PASS 只说明当时固件，不覆盖上述当前工作区。离线 vectors 与 fake harness 也不能替代 NimBLE GATT 和物理链路验收。

---

## 第七轮 P0 真机验收清单（当前交付口径）

本清单用于交付前集中跟踪“代码/自动化已就绪”和“仍需人工真机确认”的边界。未勾选项不得在发版说明中标为已完成；物理按钮、LED、系统级 Captive Portal 弹窗、72h 老化和 Gitee Release 创建均保留人工确认。

### 就绪交付物

- [x] 固件版本统一为 `v4.1.1`，DIS 固件字段与门户展示包含板型、版本号、稳定版、修复版和最后 Git 短提交 ID，`tests/ble_selftest.py` 会校验厂商/型号/固件三字段。
- [x] 普通配网链路走 `radio_ctrl` + coex 共存：长按二次确认进入配网，BLE 默认不停，保存或超时后关闭 WiFi 原地回 `ST_BLE_ACTIVE`。
- [x] 配网门户路由与前端校验已具备自动化覆盖：`tests/stage5_portal_test.py`、`tests/check_portal_structure.py`、`tests/check_portal_js.py`。
- [x] Captive Portal DNS 与 10 个系统探测端点已实现：Android/Chrome、iOS/macOS、Windows、Firefox、Kindle 探测路由统一 303 到首页，并注册 404 兜底覆盖未知探测路径。
- [x] 核心数值可配置已就绪：固定/配速机器人模式分段切换、功率/踏频/心率浮动幅度（有界平滑游走）、心率锁定/联动，均经 `/api/config` 持久化到 NVS。
- [x] 配网入口交互已补强：设备名铅笔编辑、保存成功强反馈、不关闭 WiFi 时可继续修改。
- [x] 配置导入/导出基础能力已落到领域层：`config_store_serialize` / `config_store_deserialize` 支持 JSON 往返、缺字段默认值和范围夹紧。
- [x] Release 产物标准化脚本已就绪：`scripts/build_all.sh` 可生成文件名含板型、单一版本号、stable/fix 标识和 Git ID 的 merged bin、SHA256、manifest；脚本不自动打 tag、不创建 Gitee Release。
- [x] 自动化门禁入口已就绪：`tests/check_config_consistency.py` 与 `scripts/tools/quality_gate.py` 覆盖配置一致性、host 单测、构建和静态检查聚合；`tests/operation_gate.py --level full` 已接入 release 产物门禁和核心流程耐久。
- [x] 稳定交付证据链已接入：`scripts/tools/device_acceptance.py` 统一记录真机自动项与人工项，`operation_gate.py --level device` 可写入自动结果，正式 Gitee 发布要求当前版本与 Git ID 的 P0 证据全部通过。
- [x] 历史 generic-c3 真机自动化记录：v4.1.1 release 固件曾通过启动、骑行 BLE 与一次连接/断开，详见 `CHANGELOG.md` 的“v4.1.1 通用版真机验收”。该记录不覆盖 2026-10-22 当前工作区；新协议、跑步会话、双次重连、配网门户、广播开关、物理按钮/LED 和 m5stamp-c3 均须按当前 Git ID 重新验收。

### 待人工确认（P0）

- [ ] 按钮短按：短按一次后串口出现 `短按 功率-20W`，BLE 通知功率同步下降 20W。
- [ ] 按钮长按调功：steady 模式下按住约 1s，串口出现多次 `adjust`，功率按 20W 步进上升。
- [ ] 按钮长按确认：按住 ≥2s 松开后出现 `长按2秒，LED快闪等待二次确认`，LED 进入确认快闪。
- [ ] 二次确认进入配网：确认窗口内再按一次，串口出现 `二次确认，进入WiFi配网模式`，设备开放 `BikePower-XXXXXX` 热点。
- [ ] 确认窗口超时：不二次按键等待 3s，串口出现 `确认窗口超时` 并回到 BLE 活跃态。
- [ ] LED 灯效人工观察：通用板广播慢闪/连接常亮/确认快闪；M5Stamp C3 蓝色慢闪/绿色常亮/黄色快闪。
- [ ] 配网真机流程：手机连接 `BikePower-XXXXXX` 热点，访问 `192.168.4.1`，切换固定/配速机器人模式、修改功率/踏频/心率/浮动幅度/设备名并保存，确认 BLE 保持在线且参数实时生效。
- [ ] 配网关闭流程：点击“保存并关闭 WiFi”后 5s 收尾退出，HTTP/SoftAP/DNS 停止，设备不重启并回到 BLE。
- [ ] 配网超时流程：180s 无操作自动关闭 WiFi，回到 BLE，串口无异常重启。
- [ ] Captive Portal 多系统弹窗：iOS、Android、Windows 分别连接 `BikePower-XXXXXX` 热点后能自动弹出配网页；不弹窗时手动访问 `192.168.4.1` 仍可配网。
- [ ] Captive Portal 生命周期：退出配网后串口确认 DNS 任务停止，不再响应劫持。
- [ ] 72h 老化：长时间运行无 coredump、无 WDT、无堆持续下降；建议周期性跑 `tests/ble_selftest.py` 和 `tests/stage5_portal_test.py` 采样。

上述未完成项继续保持待验收状态。执行时按板型写入 `tests/reports/device-acceptance-<板型>.json`；`NOT_RUN` 不视为通过，正式双板发布前必须分别对 generic-c3 与 m5stamp-c3 运行 `python3 scripts/tools/device_acceptance.py validate --strict --file <证据文件>`。
- [x] Release tag：`v4.1.1` 已创建并推送，指向提交 `a9f127c`。
- [ ] Release 人工复核：确认 Gitee Release 的 merged bin、manifest、测试报告和总包附件完整；源码仓库本身不把远端附件状态冒充为已验证。

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
| 二次确认 | 窗口内再按 | 通过 `wifi_trigger` 回调触发 WiFi 配网 |
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

- NVS namespace `bikepower`，schema v11；短 key 包含 `sv`/`power`/`cadence`/`mode`/`hr`/`hashr`/`pfloat`/`cfloat`/`hrfloat`/`pstep`/`weight`/`ftp`/`thr_hr`/`ride`/`cwkglo`/`cwkghi`/`cname`/`sport`/`bmask`/`rspeed`/`rpacef`/`rcad`/`rcadf`/`rmode`/`rpacer`/`rplo`/`rphi`/`rcname`/`devname` 以及 `led*` 场景配置。
- 旧 MicroPython JSON 配置不迁移，首刷重置为默认

### 真机验证（已通过）

- 首刷启动用默认值并回写：`p=260W c=90RPM mode=0 hr=150 w=70 ftp=300 thr=190 ride=0`
- 短按 3 次（每次 -20W）→ 等待落盘 → 断电重启 → 功率恢复为 `p=200W`，证明持久化生效
- 当时 host 单测 5841 断言全过（阶段 4 历史记录）；2026-10-22 本轮基线为 5892 断言，最终提交以最新完整执行结果为准。

---

## 阶段5 完成情况

### 已实现

| 模块 | 文件 | 说明 |
|------|------|------|
| 射频控制 | [main/radio_ctrl.c](main/radio_ctrl.c) | 唯一 WiFi/BLE 射频入口；配网走 coex 共存（BLE 不停），共存失败才降级为停 BLE 兜底 |
| 配网门户 | [main/wifi_portal.c](main/wifi_portal.c) / [main/web/](main/web) | esp_http_server（SoftAP 由 radio_ctrl 启动）；前端三件套 `main/web/` 由 `scripts/inline_static.py` 编译时内联到固件；C 侧仅保留静态首页、`/api/xxx` JSON、Captive Portal 探测路由和 404 兜底 |
| 状态机接入 | [main/app_state.c](main/app_state.c) | ST_WIFI_CONFIRM → ST_WIFI_CONFIG，经 `wifi_trigger` 回调触发 radio_ctrl |

- 超时方案：180s（`WIFI_SHUTDOWN_MS`）无续期自动关闭 WiFi 回到 BLE，保存并关闭后 5s 收尾再退出配网
- 前端每秒轮询 `/api/time` 显示剩余时间，`/api/renew` 续期；`/api/mem_stats` 返回堆内存 JSON，`/favicon.ico` 返回 204
- 配网表单可改：功率/踏频/心率/模式/体重/FTP/阈值心率/配速机器人/设备名，保存后写入 power_engine + NVS

### 待人工确认（需配网真机流程）

- [ ] 长按 + 二次确认 → 进入配网，手机连 `BikePower-XXXXXX` 热点
- [ ] 浏览器访问 `192.168.4.1` → 修改参数 → 保存并关闭 WiFi → BLE 保持/恢复在线且参数生效
- [ ] 180s 无操作 → 自动超时关闭 WiFi 回到 BLE
- [ ] 连上热点后跑 `python3 tests/stage5_portal_test.py` 校验 HTTP 路由（首页、`/api/state`、`/api/time`、`/api/config`、`/api/renew`、`/api/close-wifi`、Captive Portal 探测与 404 兜底）

---

## 阶段6 完成情况

### 已实现

| 项 | 文件 | 说明 |
|------|------|------|
| Task WDT 覆盖 | [main/app_main.c](main/app_main.c) / [main/led.c](main/led.c) | app_main 主循环与 led_task 均 `esp_task_wdt_add(NULL)` + 周期 `esp_task_wdt_reset()`；IDLE 任务由 `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0` 监控 |
| 低内存监控 | [main/app_main.c](main/app_main.c) | 心跳检查空闲堆：低于 `HEAP_WARN_BYTES`(80KB) 告警，低于 `HEAP_CRIT_BYTES`(20KB) 严重告警 |
| Coredump | [partitions.csv](partitions.csv) / [sdkconfig.defaults](sdkconfig.defaults) | 新增 `coredump` 分区（64KB @ 0x1d0000），ELF 格式写 flash，CRC32 校验 |

- button_task 为中断驱动、空闲时无限阻塞等唤醒，**刻意不订阅 WDT**（订阅会在无按键 5s 时误复位）
- 阈值常量集中在 [main/bikepower_config.h](main/bikepower_config.h)：`HEAP_WARN_BYTES` / `HEAP_CRIT_BYTES`

### 待人工确认（需真机 + 不可逆操作）

- [ ] 72h 老化测试：长时间运行无 coredump、无堆持续下降（跑 `tests/stage5_portal_test.py` / `ble_selftest.py` 辅助）
- [x] 发版前 `sdkconfig.defaults` 日志级别改 `CONFIG_LOG_DEFAULT_LEVEL_WARN`（已设为 WARN）
- [x] DIS 固件版本号与 `FIRMWARE_VERSION` 同步确认（generic-c3 真机 `ble_selftest.py 8` 已校验厂商/型号/固件三字段）
- [x] `v4.1.1` tag 已创建并推送
- [ ] Gitee Release 附件完整性人工复核

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
| Captive Portal | [main/captive_portal.c](main/captive_portal.c) / [main/captive_portal.h](main/captive_portal.h) | 自研轻量 DNS 服务器（UDP 53 劫持所有 A 查询返回 192.168.4.1）+ OS 探测端点 303 重定向 handler |
| DNS 生命周期 | [main/radio_ctrl.c](main/radio_ctrl.c) | AP 启动后启动 DNS，退出配网时停止（captive_portal_start_dns / captive_portal_stop_dns） |
| 探测端点路由 | [main/wifi_portal.c](main/wifi_portal.c) | 注册 10 个 OS 探测端点（/generate_204、/hotspot-detect.html、/ncsi.txt 等），统一 303 重定向到首页 |
| 分区表 | [partitions.csv](partitions.csv) | v4.0.0 起改为单 factory（3.875MB），仍 4MB Flash（阶段 9 时为 ota_0/ota_1 双 OTA 布局，已随 OTA 移除废弃） |

> 注：本阶段的在线 OTA（4MB 双 OTA 分区 + 回滚 + HTTPS 下载）已于 **v4.0.0 整体移除**，详见阶段 13 与 [docs/archive/07-remove-ota-plan.md](docs/archive/07-remove-ota-plan.md)。以下 Captive Portal 部分仍然有效。

### 覆盖的 OS 探测端点

Android/Chrome、iOS/macOS、Windows、Firefox、Kindle 共 10 个端点，统一 303 重定向触发系统自动弹出 Captive Portal 窗口。

### 资源占用

- DNS 任务栈 3072 字节 + UDP 缓冲 512 字节（栈上）
- 固件增量 ~2KB
- 配网态空闲堆仍 >80KB 告警线

### 待人工确认（需真机验证）

- [ ] iOS 连上 BikePower-XXXXXX 热点后自动弹出配网页
- [ ] Android 连上后自动弹出配网页
- [ ] Windows 连上后浏览器提示需登录
- [ ] 不弹窗时手动访问 192.168.4.1 仍可配网（降级兼容）
- [ ] 退出配网后 DNS 任务已停止（串口日志）
- [ ] 从旧双 OTA 分区切到当前单 factory 分区时需 `esptool.py erase_flash`；当前仍是 4MB Flash。

### OTA 实施（v3.1.6 引入，v4.0.0 已移除）

在线 OTA（家庭 WiFi + SNTP + HTTPS 拉 `latest.json` + 流式 `esp_ota_write` + 自检回滚 + 防降级）曾在 v3.1.6~v3.1.8 实现，已于 **v4.0.0 整体移除**（连同家庭网络配置与双 OTA 分区表）。移除范围与理由见阶段 13 与 [docs/archive/07-remove-ota-plan.md](docs/archive/07-remove-ota-plan.md)。当前固件升级走电脑端网页烧录，入口为 `https://easyhey.com/?firmware=MOD220`；手机端不支持烧录。分区表变更场景仍需 `erase_flash`。

---

## 已知限制 / 注意事项

- ESP-IDF 已升级至 v5.5.4（修复了 NimBLE host 潜在断连问题；工具链 esp-14.2.0_20260121）。
- WiFi/BLE 共存：配网期间 BLE 保持在线（`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`），仅资源初始化失败时降级为停 BLE + 重启（见 [.trae/rules/wifi-ble-mutex.md](.trae/rules/wifi-ble-mutex.md)）
- 物理按钮/LED 灯效需人工确认，自动化测试只能覆盖 BLE 连接驱动的状态迁移
- 配网门户动态测试（stage5_portal_test.py）需本机连 BikePower-XXXXXX 热点；本机不能切换 WiFi 时改用 `tests/mock_server.py` 本地调试，或换另一台机器执行
- `test/` 与 `tests/` 目录职责不同不合并（评估结论见 [tests/README.md](tests/README.md) 第六章）：`test/` 放 ESP-IDF 约定的 host 单测（纯 C），`tests/` 放所有 Python/Shell 设备测试与静态门禁
- 当前 P0 协议与可靠性缺陷及实施顺序见 [docs/dev/12-top5-protocol-stability-design.md](docs/dev/12-top5-protocol-stability-design.md) 和 [docs/dev/13-project-review-and-optimization.md](docs/dev/13-project-review-and-optimization.md)。在对应修复和真机验收完成前，不把现有自动化全绿等同于协议正确。
