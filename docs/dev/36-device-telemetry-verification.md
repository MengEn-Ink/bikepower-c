# 设备状态遥测协议与验证

日期：2026-11-05。功能属于源码候选，正式 GitHub Release 与网页刷写固件尚不包含本轮新增遥测；Pages 可部署，旧固件显示缺失能力。

## 数据链路

`app_main` 每秒调用 `device_telemetry_sample()`，以 `MALLOC_CAP_8BIT` 读取同口径堆快照。总容量为当前空闲加已分配堆；空闲百分比不以芯片 SRAM 为分母。最低值属于设备启动会话，网页重连不重置。主任务栈高水位的 ESP-IDF 单位为字节。

CPU 使用 FreeRTOS 运行统计，时钟为 `esp_timer` 微秒计数。全机忙碌万分比为 `10000 × (Δtotal − Δidle) / Δtotal`；主任务占比采用同一分母，其他忙碌任务只显示合计。首窗口、零分母、计数矛盾返回无效；计数按 uint32 模运算处理回绕。统计包含被 RTOS 归属到任务的时间，不区分用户态、内核或 ISR；主频是配置值。没有每秒暂停调度扫描完整任务表。

NimBLE host callout 每秒查询连接、RSSI、MTU、interval、latency 与 timeout，GATT 访问只读缓存。RSSI 查询失败或返回 127 时标为缺失。每轮四组统一编码并一次发布；通知先复制整轮缓冲，主任务在发送途中更新不会混合序号。发送复用 BLE 通知生命周期锁，退订、断连与停止清理，host 退出后释放 callout。

## UUID 与布局

服务：`7a5a1000-4c7a-4f2a-9b3e-6d1f4a8c0b21`。不加入运动广播、不改变运动数据节拍、不修改 NVS schema。

全部记录固定 20 字节，支持 ATT MTU 23。整数为小端，`1002` 至 `1005` 的头部为版本 u8、有效位 u8、序号 u16。版本为 1，采样周期 1000ms。

| 特征尾段 | 属性 | 偏移 4 起的字段 |
|---|---|---|
| `1001` | Read | 元数据特殊布局：0 版本，1 组掩码 15，2 周期 u16，4 boot u32，8 三段版本 u8，11 板型（0 通用/1 M5），12 重启原因，13 七字节 Git SHA |
| `1002` | Read/Notify | 空闲 u32、最低 u32、最大连续块 u32、总堆 u32（字节）；flags bit0 有效 |
| `1003` | Read/Notify | uptime u32（秒）、忙碌 u16（万分比）、配置主频 u16（MHz）、主任务 u16（万分比）、主任务最低空闲栈 u32、状态 u8、profile u8；flags bit0 CPU 有效 |
| `1004` | Read/Notify | RSSI i8、connected u8、MTU u16、interval u16（1.25ms）、latency u16、timeout u16（10ms）、余下六字节保留；flags bit0 链路有效、bit1 RSSI 有效 |
| `1005` | Read/Notify | 提交成功、提交失败、mbuf 分配失败、通知锁跳过各 u32；flags bit0 有效 |

发送计数是本次启动以来的应用提交计数，包含运动与遥测通知，不能据此宣称无线送达。配置和 FTMS indication 队列不计入本记录。Meta 中版本/板型/Git SHA 来自构建，DIS 身份覆盖不影响它们。

## 网页行为

入口 `#/device-status`，共享 `singleDevice` 连接。常驻栏每秒显示空闲堆、CPU 忙碌和板端信号；内存/CPU 以百分比和占用条表达，蓝牙保持 dBm/ms。点击指标打开非模态窗口，支持独立暂停、折叠、放大、拖动；桌面可调整大小，小屏自适应。页面与窗口不会各自建立连接。

最多保留 301 份完整快照和八组在途序号；严格校验长度、版本、数值与同序组合，丢弃过时或重复记录。boot 改变清历史；后台暂停停更判断，恢复清在途窗口；数据间隔超过三个周期不连线。暂停只冻结画面，释放调用现有全工具/跨标签页交接。旧固件缺少服务时提示候选升级，不显示示例数值。

手机主页仍是使用引导，状态直链和手机目录中的“设备状态”可以进入真实工具布局。目录使用公网绝对链接，让单文件离线手册也能打开线上状态页。Safari/Firefox 不支持标准 Web Bluetooth；页面可阅读不等于能连接 BLE。

## 已完成验证

- `python3 tests/test_device_telemetry.py`：C 人工 golden bytes、短缓冲拒绝、CPU 首窗口/回绕/异常恢复、实际 GATT 回调与通知生命周期 ASan/UBSan。覆盖未订阅无发送、同轮复制、锁外协议栈访问、退订/断开、RSSI 缺失、计数与 callout 释放。
- `node tests/test_device_telemetry.cjs`：解码、同序组合、序号回绕、历史容量、boot 切分、订阅失败回滚、旧固件与并发启动取消。
- `npm test --prefix tests/web-flash`：既有串口/DOM/教学回归及新增遥测客户端。
- `npm run browser --prefix tests/web-flash`：既有网页刷写/手机教学矩阵，加 390/1280px 浅深主题设备状态测试；百分比、独立暂停、恢复、断开清空和横向溢出验证。受控 BLE 输入仅用于网页验证。
- 桌面深色与手机浅色截图已逐项核对；小屏图表使用实际容器宽度保证坐标文字可读。界面 detector 对新增 CSS/JS 返回空问题列表。
- BLE 标准生产回归、运行快照、协议 vectors 与 Host 6115/0 已通过。
- `python3 scripts/tools/quality_gate.py --no-build`：115 PASS、2 工作区未提交 WARN、0 FAIL。构建由双板脚本单独执行，不计为此命令已构建。
- 双板正式配置编译通过，app 为 `0x115690` 字节（约 1.084 MiB），factory 分区 `0x3e0000` 字节，剩余 `0x2ca970` 字节（72%）。这只是 Flash 静态容量，不代表运行时空闲堆。
- 首次打包因历史设备证据 Git ID 与当前基线不一致而拒绝收录；候选重新构建使用独立空证据目录，真机状态保持 `NOT_RUN`。
- `DEVICE_ACCEPTANCE_DIR=/tmp/bikepower-telemetry-no-device-evidence bash scripts/build_all.sh`：双板构建与打包 exit 0，180.4 秒 mock/host 核心流程耐久通过（15 轮）。独立附件检查通过：10 个标准附件、ZIP 内 15 个文件；两个合并 bin 均为 1,201,808 字节，总包 1,449,421 字节，SHA256 与 manifest 一致。候选由提交前工作区构建，Git 元数据基线为 `d892c9a`；不作为正式发布证据。
- 真实 HTTP 源页面经浏览器验证状态直链、主题切换、CPU 窗口开关、未连接缺失值与无横向溢出；Python 真机脚本解码器人工字节校验通过，仍属于离线验证。
- 文档门禁 79 PASS、2 工作区未提交 WARN、0 FAIL，62 份 Markdown 链接零错误。CI/Pages 状态见交付记录。

## 真机边界

当前 macOS 只有系统虚拟串口，未识别 ESP32 USB。本轮烧录、新 GATT 服务读取、CPU 开销、>80 KiB 堆余量、WiFi/BLE 共存、通知长时稳定为 `NOT_RUN`，不能用网页协议替身或历史候选验收覆盖。

连接实物后运行：

```bash
python3 tests/ble_telemetry_e2e.py --seconds 30 --report tests/reports/telemetry-device.json
```

该脚本只读、不改配置，验证两次连接、boot 不变、四组完整快照、CPU 有效、RSSI、堆门限、MTU 与退订。完整烧录流程遵循 real-device-test；真实 App 测试前释放网页连接。

## 交付记录

- `6eacfb5`：真实采样、独立 GATT 服务及协议测试，已推送 main。
- `50ca3c8`：设备状态 Pages、手机直链及浏览器回归，已推送 main。
- 文档批同步协议、手册、构建结果和实物边界。最终 CI/Pages 与公网验证在流水线结束后补录。
