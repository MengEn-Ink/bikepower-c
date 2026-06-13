# 更新日志

本项目遵循 [SemVer](https://semver.org/lang/zh-CN/)。版本号格式 `vMAJOR.MINOR.PATCH`。

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
