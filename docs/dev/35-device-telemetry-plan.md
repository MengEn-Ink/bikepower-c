# 设备遥测实施计划

**目标：** 将已批准的设备状态原型接入真实 BLE 内存、CPU 和连接指标，完成协议、网页、双板构建与 Pages 部署。

**架构：** 主任务每秒发布固定容量快照，NimBLE host 每秒查询并缓存连接参数；只读独立 GATT 服务使用四组 20 字节记录，保持 MTU 23 可用。网页复用现有连接，按序号组合记录并保留至多 301 个完整样本。

**技术：** ESP-IDF 5.5.4、FreeRTOS esp_timer 运行统计、NimBLE、纯 C 编码器、原生 JavaScript/CSS。

## 1. 固件与协议

- [x] 先添加 `test/test_device_telemetry.c`，验证人工字节真值、短缓冲拒绝、CPU 首窗口无效、回绕和异常计数；执行 `python3 tests/test_device_telemetry.py` 确认缺失实现时失败。
- [x] 新建 `components/bikepower_domain/device_telemetry.c` 和 `include/device_telemetry.h`：固定四组编码、百分比窗口函数；接入 component 与 host 构建。
- [x] 新建 `main/device_telemetry_runtime.c/.h`：同口径 MALLOC_CAP_8BIT 堆、运行时间、重启原因、原生构建身份；主任务最低空闲栈单位为字节。
- [x] 新建 `main/ble_telemetry_service.c/.h`：元数据读取、四组 Read/Notify、订阅清理、host callout RSSI 缓存。GATT 回调只复制缓存，不查询 HCI、不分配大缓冲。
- [x] 修改 `main/app_main.c`、`main/ble_bikepower.c`、`main/bikepower_config.h`、`main/CMakeLists.txt`、`sdkconfig.defaults`；UUID/采样周期集中，运行统计使用 esp_timer，CCCD 容量覆盖新增四组。
- [x] 运行 host、协议与生产 BLE 回归；双板编译；设备存在时烧录并执行 `tests/ble_telemetry_e2e.py`，否则记录 NOT_RUN。

CPU 以 `10000 × (Δtotal - Δidle) / Δtotal` 得到万分比；首窗口、零分母、idle 大于 total 均标为缺失。总计数和 idle 计数按 uint32 模运算处理回绕。主任务使用 `vTaskGetInfo(NULL, …, pdFALSE, eInvalid)`，不暂停调度读取全任务表；其余忙碌任务只展示合计，不冒充单任务排行。

## 2. Pages

- [x] 先添加 `tests/test_device_telemetry.cjs`，验证严格长度/版本、同序组合、boot 切分、旧固件、订阅失败回滚与关闭；确认失败后实现 `tools/web/device-telemetry.js`。
- [x] 新建 `tools/web/device-status.js/.css`；在 `ble-monitor.html` 增加状态路由、共享连接挂钩与服务授权；站点打包已自动复制 tools/web 全目录，无需修改生成器。
- [x] 常驻百分比、分段条、真实时间趋势、任务合计、独立浮窗、暂停、拖动/调整大小、报告导出、小屏双主题与减少动画。
- [x] 所有缺失值显示“—”；后台暂停停更判定，恢复开启观测窗口；历史断层不连线；旧固件显示升级提示，释放走现有全工具连接交接。
- [x] Playwright 测试实际页面交互和受控 BLE 输入，截图核对桌面/手机双主题；mock 结果只用于 UI/生命周期验证。

## 3. 交付

- [x] 同步 `CHANGELOG.md`、`PROGRESS.md`、技术参考、用户手册、BLE Wiki、规则结构与文档索引。
- [x] 运行 `python3 scripts/tools/quality_gate.py` 与双板构建，记录资源静态容量和实测缺口；构建未留同配置基线，未声称精确增量。
- [x] 按固件、Pages、文档分批新建中文 commit 并立即 push；检查 GitHub CI 和 Pages 公网入口。
- [x] 不创建固件 tag/Release；当前正式刷写目录不能冒充已含遥测功能。
