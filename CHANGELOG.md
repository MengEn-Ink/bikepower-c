# 更新日志

本项目遵循 [SemVer](https://semver.org/lang/zh-CN/)。版本号格式 `vMAJOR.MINOR.PATCH`。

## [未发布]

### 改进（ESP-IDF 升级）
- ESP-IDF v5.5.1 → v5.5.4：phy_version 1201→1232，BT controller 2edb0b0→b7de11e，工具链 esp-14.2.0_20241119→esp-14.2.0_20260121。固件体积 +2.8KB（0xf7350→0xf7e60），编译/真机验证通过。
- 配网启动/停止路径加堆内存诊断日志（`wifi_portal.c`/`radio_ctrl.c`），便于内存监控。

### 改进（文档）
- 新增 `docs/memory-and-feature-assessment.md`：各场景内存实测、满负载分析、OTA/Captive Portal/FTMS 扩展可行性评估与规划建议。

## [3.1.3] - 2026-06-27

### 改进（WiFi/BLE 共存配网，设备不重启）
- 配网期间 BLE 保持在线继续通知，骑行 App 全程不断连。原方案进入配网会停 BLE + 配网结束 `esp_restart()`，现改为共存模式（`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`），coex 模块按时间片仲裁 WiFi/BLE 射频。
- 配网结束（保存成功或 180s 超时）不再重启设备：`wifi_portal` 通过 done 回调投递 `APP_EVT_WIFI_DONE`，状态机收到后调用 `radio_ctrl_exit_wifi` 关闭 HTTP + SoftAP，回到 `ST_BLE_ACTIVE`。
- 配置变更实时生效：网页表单提交后通过 `power_engine_set_*` 系列 setter 同步内存与 NVS，主循环 50ms 周期通过 BLE 通知新值，无需重启。
- 失败兜底：仅当共存模式下 `radio_ctrl_start_wifi_ap()` 返回错误（资源耗尽/初始化失败）时，降级为旧独占方案（停 BLE + 重试 + 必要时 `esp_restart`）。

### 改进（配网页面交互）
- 首页底部按钮拆分为"保存设置"（仅保存+实时生效，不关 WiFi，可继续修改）和"保存并关闭 WiFi"（保存+实时生效+5s 后关 WiFi 回到 BLE）两个按钮，通过 `formaction` 区分目标路由。
- 去掉"恢复默认"按钮及对应 `resetForm` 函数，简化界面操作。
- 保存成功后首页 URL 带 `saved=1` 参数触发 toast 提示"已保存并实时生效"，2s 自动消失。

### 修复（代码逻辑漏洞）
- `wifi_portal.c` `h_home` 缺 `s_closing` 守卫：关 WiFi 收尾阶段浏览器刷新首页会续期 deadline 破坏收尾流程。修复：开头加守卫，收尾期间刷新跳转成功页。
- `wifi_portal.c` `h_draft` 草稿污染引擎：`apply_form_fields` 置 dirty 后主循环 10s 心跳会自动落盘 NVS，违反草稿语义。修复：草稿后立即 `power_engine_clear_dirty`。
- `wifi_portal.c` NVS 失败回滚 BLE 跳变：`apply_form_fields` 已改 `s_engine` 并触发 BLE 通知，回滚后下次通知旧值造成跳变。修复：改写临时副本，NVS 成功后才 swap 回主引擎。
- `wifi_portal.c` `send_save_error` 返回 `ESP_FAIL` 导致 httpd 重复发错误响应。修复：改返回 `ESP_OK`（500 响应已通过 SEND 完整发送）。
- `ble_bikepower` 设备名网页修改后不实时生效：BLE 广播名只在启动时调一次。新增 `ble_bikepower_refresh_device_name()`，保存路径检测设备名变化时刷新 GAP 服务+重启广播。
- `nvs_store.c` NVS 并发竞争：app_main 主循环（按钮调功 dirty 落盘）与 httpd 任务（网页保存）并发 `nvs_store_save` 无锁。修复：`StaticSemaphore` 互斥保护，100ms 超时跳过保留 dirty。
- `radio_ctrl.c` `esp_netif_deinit` 无条件调用：可能释放别人 init 的 netif。修复：加 `s_netif_inited` 标志，仅自建时 deinit。

### 修复（超时卡死 bug）
- `app_main.c` `on_wifi_done` 的 `xQueueSend(..., 0)` 零等待投递，队列瞬满时事件丢失；`portal_done` 的 `s_closing` 守卫又阻止重试，导致永久卡在 `ST_WIFI_CONFIG`，定时器继续跑每秒打印噪音。修复：改为 5 次重试投递（每次 10ms）+ 诊断日志。
- `wifi_portal.c` `shutdown_check_cb` 日志在 `portal_done` 幂等检查之前打印，导致每秒重复打印"到期"噪音。修复：加 `s_closing` 守卫，收尾后不再重复打印。
- `wifi_portal.c` `portal_done` 加链路诊断日志，确认 `done_cb` 调用与 `s_done_cb` 有效性。

### 修复（收尾流程逻辑漏洞，真机验证发现）
- `wifi_portal.c` `s_closing` 语义冲突导致"保存并关闭 WiFi"后 WiFi 永不关闭：`h_close_wifi` 设 `s_closing=true` 表示"5s 收尾窗口"，但 `portal_done` 和 `shutdown_check_cb` 都用 `s_closing` 做幂等守卫，语义冲突导致 `portal_done` 永不执行。修复：引入独立 `s_done_called` 标志做 `portal_done` 幂等，`s_closing` 仅表示"收尾窗口已启动/已结束"，两者解耦。
- `wifi_portal.c` `shutdown_check_cb` 守卫误用 `s_closing`：上一轮止噪修复用 `s_closing` 守卫，但也误杀了收尾窗口到期触发 `portal_done` 的能力。修复：改用 `s_done_called` 守卫，`s_closing` 状态下仍检查 deadline。
- `wifi_portal.c` `wifi_portal_start` 失败路径卡死状态机：`start_httpd`/`esp_timer_create`/`esp_timer_start_periodic` 任一失败直接 return，`APP_EVT_WIFI_DONE` 永不投递，状态机卡 `ST_WIFI_CONFIG`。修复：三处失败路径都调 `portal_done()` 触发 done_cb，状态机走 `radio_ctrl_exit_wifi` 幂等清理。
- `radio_ctrl.c` `wifi_portal_start` 失败注释过时：原注释说"等 APP_EVT_WIFI_DONE"，但该事件原实现不会来。修复：注释更新为"portal_done 已投递事件"。

### 改进（按钮诊断）
- `button.c` `post` 函数加按钮事件诊断日志，确认 ISR→button_task→app_queue 链路通畅。

### 改进（规则同步）
- `wifi-ble-mutex.md` 改名为"WiFi/BLE 射频共用规则"，更新 WB-MUTEX-1~8 条款，新增失败兜底降级路径。
- `ai-challenge.md` 硬件约束冲突条款更新为支持共存；操作权限 Deny 项由"BLE+WiFi 共存"改为"绕过 radio_ctrl 直接调 esp_wifi_*"。
- `coding-style.md` 禁止事项更新为"禁止绕过 radio_ctrl 直接调用 esp_wifi_*"。
- `best-practices.md` WiFi 健壮性、架构一致性、配网链路图同步更新。
- `README.md` 配网说明、LED 灯效表更新为共存方案。

### 测试报告
- `idf.py build`：0 error，固件大小 0xf70a0 字节（约 1MB），分区剩余 45%。
- host 单测：918 通过 0 失败。
- 真机测试：BLE 连接稳定（堆 180KB），网页保存实时生效，device_name 刷新验证通过。

## [3.1.2] - 2026-06-26

### 修复
- 配网页面表单值丢失：用户从首页切换到"更多配置"再返回后，首页显示的功率/踏频/心率/模式恢复为旧值，最终保存到 NVS 的也是错误默认值。根因有二：
  1. 浏览器缓存了配网首页 HTML，返回时显示缓存版本而非服务端最新状态。修复：首页响应添加 `Cache-Control: no-store, no-cache, must-revalidate, max-age=0`。
  2. "更多配置"页隐藏表单只包含 `device_name`/`profile_*` 字段，缺少 `mode`/`power`/`cadence`/`heartrate`。用户点"应用并回首页"提交到 `/draft` 时这四项未回传，`apply_form_fields` 无法恢复之前选择的值。修复：补全隐藏字段并新增 `mode_to_id()` 将整数模式映射为表单字符串。

### 改进（规则/记忆）
- 新增 `WIFI-28`/`WIFI-29` 规则：动态 HTML 必须禁缓存；多页表单每页隐藏字段必须覆盖全部可变字段。
- 踩坑记忆补充两条 WARNING：浏览器缓存与多页表单隐藏字段完整性。

### 测试报告
- `idf.py build`：0 error。
- 真机烧录 + 串口确认：固件 `v3.1.2`，BLE 五服务启动正常。
- Host 单测：581 通过，0 失败。

### 测试工具（沿用上版）
- `scripts/tools/quality_gate.py` 过时项修正：
  - 项目路径硬编码 `/Users/bytedance/...` 改为按脚本位置自动推导，跨机器可用。
  - IDF 激活从硬编码 `~/esp/esp-idf-v5.5.4` 改为默认 `/home/mengen1014/work/esp32-power/esp-idf`，并增加 `idf.py --version` 探测，已激活则不重复 source。
  - 子进程由 `/bin/sh` 改为显式 `bash -c`，修复 IDF `export.sh` bash 语法在 dash 下静默失败。
  - 编译判定由文本匹配 `Project build complete` 改为 `rc==0 且输出含 build complete 或 write_flash`，避免 `tail` 截断误判。
  - Host 单测判定增加 `None` 保护，原 `assertions.group(0)` 在输出异常时崩溃。
  - 必备资产清单同步实际目录。

## [3.1.1] - 2026-06-16

### 修复
- 板型自动检测误判：原用 GPIO12 电平判断，合宙通用板 GPIO12 接 LED 电路被内部下拉淹没，导致误判为 M5Stamp C3 走 SK6812 驱动致 LED 不亮。改用 GPIO3 检测（M5Stamp 按钮外部上拉读高，通用板悬空内部下拉读低），逻辑更可靠。
- 配网提交表单报 "Header fields are too long"（HTTP 431）：手机浏览器请求头超过默认 1024 字节，`CONFIG_HTTPD_MAX_REQ_HDR_LEN` 由 1024 提升到 2048。
- 配网首页表单跳转"更多配置"再返回后新填数值丢失：首页"更多配置"链接改为先把当前表单经 `/draft` 暂存到内存引擎再跳转；`/draft` 草稿保存扩展为也接受 `mode`/`power`/`cadence`/`heartrate`，返回首页时正确回显。

### 改进
- SoftAP 并发连接数 `max_connection` 由 3 降为 2，降低配网期内存峰值。
- 板型差异收敛为描述表：`board_detect.c` 用 `s_board_desc[]` 集中管理各板型的按钮/LED 引脚、LED 驱动类型与固件包名；新增 `led_drv_t` 驱动类型枚举，`led.c` 改为按驱动类型分发；新增板型只需追加枚举与一行描述，无需改动 led/button 等调用方。
- 新增编译期板型选择与双板型构建脚本（`scripts/build_all.sh`）。

### 文档同步
- 修正源码头注释与日志中残留的 ESP-IDF 版本号 v5.5.1 → v5.5.4（`app_main.c`/`button.h`/`wifi_portal.h`/`radio_ctrl.h`/`app_state.h`/`app_events.h`/`led.h`/`sdkconfig.defaults`）。
- 修正 `wifi_portal.c` `recv_body` 注释请求体上限 2048 → 4096，与代码一致。
- 同步 `docs/04-technical-reference.md` 中 `max_connection` 3 → 2。
- 新增 `.clangd` 配置适配 RISC-V 工具链，消除 IDE 伪报错。

### 测试报告
- host 单测：581 通过，0 失败。
- 双板编译（`scripts/build_all.sh`）：generic-c3 / m5stamp-c3 均成功，app 分区 44% free。
- 真机测试：板型检测正确（generic-c3 GPIO3=0），BLE 五服务齐全，FTMS ERG 收敛正常，堆内存稳定 ~171KB 无泄漏。

## [3.1.0] - 2026-06-14

### 新增
- M5Stamp C3 开发板适配：运行时自动检测板型（GPIO12 电平判断），无需手动配置。
- SK6812 RGB LED 驱动（RMT bytes encoder）：M5Stamp C3 使用 GPIO2 驱动板载 RGB LED，颜色映射为蓝色（广播）/ 绿色（连接）/ 黄色（确认）。
- 板型检测模块 `board_detect.c/h`：启动时自动识别通用板或 M5Stamp C3，动态切换 LED/按钮引脚。

### 修复
- 配网页面模式切换失效：`syncMode()` 函数多余闭合花括号导致 JS 语法错误，整个 script 块无法执行。

### 测试报告
- `idf.py build`：0 error。
- 真机串口确认：板型检测正确（M5Stamp C3），SK6812 LED 正常闪烁，BLE 广播/连接/FTMS ERG 正常。
- 空闲堆：173KB（> 80KB 阈值）。
- 配网页面模式切换：已修复，三段切换正常。

## [3.0.0] - 2026-06-13

### 新增（阶段6 稳定性）
- Task WDT 覆盖：`app_main` 主循环与 `led_task` 订阅看门狗并周期喂狗；启用 `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0` 监控 IDLE 任务。
- 低内存监控：心跳日志按 `HEAP_WARN_BYTES`(40KB) / `HEAP_CRIT_BYTES`(20KB) 分级告警。
- Coredump：新增 `coredump` 分区（64KB @ 0x1d0000），ELF 格式写 flash + CRC32 校验。

### 新增（阶段5 WiFi 配网）
- `radio_ctrl`：唯一 BLE/WiFi 射频开关，方案 A 软切换（停 BLE → 释放射频 → 启 WiFi AP → 超时/保存后 `esp_restart()` 恢复 BLE）。
- `wifi_portal`：SoftAP + esp_http_server 配网门户，移植旧版完整 UI（iOS 三卡 + 训练区间染色 + 动态碎片页）；11 路由含 `/` `/config` `/success` `/more` `/draft` `/ui/module` `/closing` `/time` `/renew` `/mem_stats` `/favicon.ico`。
- 配网表单可改：功率/踏频/心率/模式/体重/FTP/阈值心率/配速机器人/设备名。
- `/success` 直访守卫（未提交配置 302 回首页）、`/favicon.ico` 返回 204、`/mem_stats` 返回堆内存 JSON。
- 测试脚本 `tests/stage5_portal_test.py`：HTTP 路由只读自测。

### 改进（测试脚本）
- `tests/ble_selftest.py`：补充 DIS 设备信息读取校验（厂商 `BikePower` / 型号 `ESP32-C3` / 固件 `3.0.0`），纳入总判定，与"五服务齐全"声明一致。

### 新增（阶段4 配置持久化）
- 领域层 `config_store`：配置 schema（defaults/validate/migrate），`CONFIG_VERSION=4`。
- `nvs_store`：NVS namespace `bikepower`，10 短 key 含 `devname`，脏标记周期落盘。

### 新增（阶段0-3 基线）
- 工程骨架（CMake/分区/sdkconfig）。
- NimBLE 五服务：CPS/HRS/CSC/FTMS/DIS + FTMS 控制点 ERG。
- 纯 C 领域层：功率引擎/心率模型/用户配置（host 单测可跑）。
- GPIO9 中断按钮 + GPIO12 LED 任务 + 7 态状态机。

### 测试报告
- host 单测（`test/test_domain`）：573 断言全过。
- `idf.py build`：通过，`bikepower-c.bin` 约 0xfd9a0 bytes，应用分区 43% free。
- 分区表核验：`coredump` 64K @ 0x1d0000 已写入。
- 真机老化、配网全链路、物理按钮/LED：待人工确认（见 [PROGRESS.md](PROGRESS.md)）。

### 已知限制
- ESP-IDF v5.5.4（已从 v5.5.1 升级，修复 NimBLE 断连问题）。
- WiFi/BLE 射频互斥，配网前必停 BLE。
- 发版前需将日志级别改为 `CONFIG_LOG_DEFAULT_LEVEL_WARN`。
