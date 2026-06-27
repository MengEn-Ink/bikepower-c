# BikePower C

BikePower C 是一个运行在 ESP32-C3 上的蓝牙骑行功率计模拟器，面向智能骑行台、训练 App 和 DIY 骑行设备场景。它把原来的 MicroPython 版本完整重写为 C + ESP-IDF + NimBLE，让设备更稳定，也更适合长期使用和二次开发。

## 项目简介

如果你只想知道它能做什么，可以把它理解成一块“小型骑行训练控制盒”：开机后通过蓝牙广播，被骑行 App 配对后就能模拟功率、踏频、心率等数据；需要改配置时，再用按钮进入 WiFi 配网页面。

## 适配开发板

固件支持自动检测板型，无需手动配置。当前适配的开发板：

| 开发板 | LED | 按钮 | 检测方式 |
|--------|-----|------|----------|
| 通用 ESP32-C3 开发板 | GPIO12 普通 LED（高电平点亮） | GPIO9（内部上拉） | 默认板型 |
| [M5Stamp C3](https://shop.m5stack.com/products/m5stamp-c3-mate-with-pin-headers) | GPIO2 SK6812 RGB LED | GPIO3（外部上拉） | GPIO3 按钮上拉自动识别 |

**自动检测原理**：启动时将 GPIO3 配置为输入下拉，读取电平——通用板 GPIO3 悬空下拉读低，M5Stamp C3 的 GPIO3 接按钮（外部上拉到 3.3V）读高。检测完成后 GPIO3 恢复，后续按钮初始化重新配置。

**M5Stamp C3 LED 颜色映射**：

| LED 颜色 | 设备状态 |
|----------|----------|
| 蓝色慢闪 | BLE 广播中 |
| 绿色常亮 | BLE 已连接 |
| 黄色快闪 | 等待二次确认 |
| 熄灭 | 非活动态或配网中 |

## 硬件接线图说明

下表列出当前固件默认使用的 ESP32-C3 主要引脚和接口用途：

| 引脚 / 接口 | 用途 | 说明 |
|------|------|------|
| `GPIO9` / `GPIO3` | 物理按钮 | 通用板用 GPIO9，M5Stamp C3 用 GPIO3；内部上拉，按下为低电平 |
| `GPIO12` / `GPIO2` | 状态 LED | 通用板用 GPIO12 普通 LED，M5Stamp C3 用 GPIO2 SK6812 RGB LED |
| `5V / USB` | 供电 | 可通过开发板 USB 口供电，也可由稳定 5V 电源输入 |
| `3V3` | 板载逻辑电源 | 给外设供电时要注意总电流预算 |
| `GND` | 地线 | 所有外设地必须共地 |
| `UART0` | 调试串口 | 用于烧录、日志输出和故障排查；通常走开发板自带 USB 转串口 |

![硬件引脚对照](docs/images/hardware-pinout.png)

更详细的硬件建议见 [docs/03-hardware.md](docs/03-hardware.md)。

## 核心功能清单

- 支持标准 BLE 骑行相关服务，可被主流骑行 App 识别
- 模拟功率、踏频、心率、设备信息等训练数据
- 支持 FTMS ERG 控制，适合训练模式联动
- 提供实体按钮操作，不依赖屏幕也能快速调功
- 提供 LED 状态提示，方便判断当前设备状态
- 支持 WiFi 配网门户，可在手机浏览器里修改核心参数
- 支持配置保存，重启后继续使用上一次设置

## 软件架构

项目采用三层结构，目的是把“设备怎么接”“训练逻辑怎么算”“用户怎么操作”分开处理，后续维护更清晰：

- 应用层：位于 `main/`，负责按钮、LED、BLE、WiFi 配网、状态机和主循环
- 领域层：位于 `components/bikepower_domain/`，负责功率引擎、心率模型、用户配置和训练逻辑
- 硬件层：基于 ESP-IDF、FreeRTOS、NimBLE、GPIO、NVS、WiFi 等底层能力，为上层提供驱动和运行环境

![架构图](docs/images/architecture.png)

按钮与灯效状态切换可参考：

![状态机](docs/images/button-led-state.png)

主循环和数据更新流程可参考：

![数据流](docs/images/main-loop-flow.png)

WiFi 配网的射频共用流程可参考（v3.1.3 起为共存模式，BLE 不停）：

![WiFi/BLE 射频共用流程](docs/images/rf-wifi-flow.png)

## 构建与烧录步骤

先准备好 ESP-IDF 环境，再在项目根目录执行：

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p <port> flash
idf.py -p <port> monitor
```

`<port>` 请替换为你的实际串口，例如 macOS 下常见 `/dev/cu.usbmodem*`，Linux 下常见 `/dev/ttyUSB0` 或 `/dev/ttyACM0`。

## 使用说明

设备的日常操作很简单，主要靠一个按钮：

- 短按：功率减小 `20W`
- 按住约 `0.5s` 以上：持续调功，每 `200ms` 增加 `20W`
- 长按并保持到 `2s` 以上再松开：进入“配网确认”窗口
- 在确认窗口内再按一次：正式进入 WiFi 配网模式
- 如果确认窗口内没有再次按键：约 `3s` 后自动取消，回到蓝牙工作状态

## 配网说明

进入配网模式后，设备会保持 BLE 在线继续通知，同时打开自己的 WiFi 热点。普通用户按下面顺序操作即可：

1. 长按按钮至少 `2s`，松开后看到 LED 进入确认提示
2. 在确认时间内再按一次按钮
3. 手机连接设备发出的 WiFi AP：`BikePower`
4. 打开浏览器访问 `192.168.4.1`
5. 修改需要的配置参数
6. 点击保存，配置实时生效到 BLE 通知，骑行 App 立即看到新值
7. 倒计时结束后 WiFi 自动关闭，蓝牙全程未断开，回到骑行 App 继续骑行即可

> 注：WiFi 与 BLE 共存（`CONFIG_ESP_COEX_SW_COEXIST_ENABLE`），配网期间骑行 App 不会断连；仅在 WiFi 资源初始化失败的极端情况下才会降级为停 BLE + 重启。

## LED 灯效说明

| LED 表现 | 设备状态 | 用户如何理解 |
|------|------|------|
| 慢闪 | BLE 广播中 | 设备已开机，正在等待手机或 App 连接 |
| 常亮 | BLE 已连接 | 已配对成功，正在正常工作 |
| 快闪 | 等待二次确认 | 你刚完成长按，设备在等你确认是否进入配网 |
| 慢闪 | WiFi 配网中 | 设备已切到配网模式，可连热点改配置 |
| 熄灭 | 非活动态或配网中 | 设备暂时不在正常广播状态 |

## 项目目录结构

```text
bikepower-c/
├── README.md
├── AGENTS.md
├── PROGRESS.md
├── main/
│   ├── app_main.c
│   ├── app_state.c
│   ├── board_detect.c
│   ├── button.c
│   ├── led.c
│   ├── ble_bikepower.c
│   ├── radio_ctrl.c
│   ├── wifi_portal.c
│   └── bikepower_config.h
├── components/
│   └── bikepower_domain/
├── docs/
│   ├── 01-quickstart.md
│   ├── 02-user-manual.md
│   ├── 03-hardware.md
│   ├── 04-technical-reference.md
│   ├── 05-cidf-nimble-rewrite.md
│   ├── 06-micropython-vs-c-comparison.md
│   ├── INDEX.md
│   └── images/
├── tests/
├── test/
│   └── test_domain
├── partitions.csv
└── sdkconfig.defaults
```

## 引用

- [PROGRESS.md](PROGRESS.md)：查看当前开发进度、已完成阶段和已知限制
- [AGENTS.md](AGENTS.md)：查看本项目的协作规范、验证要求和文档入口
