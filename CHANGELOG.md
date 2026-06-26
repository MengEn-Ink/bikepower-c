# 更新日志

本项目遵循 [SemVer](https://semver.org/lang/zh-CN/)。版本号格式 `vMAJOR.MINOR.PATCH`。

## [未发布]

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
