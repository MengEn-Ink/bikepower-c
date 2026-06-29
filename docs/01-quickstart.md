# BikePower C 快速上手

这份文档面向第一次接触 BikePower C 的普通用户。你不需要先理解代码或电路，只要跟着下面的步骤做，就能把设备接通、配对并开始使用。

## 1. 硬件清单

- 一块 ESP32-C3 开发板
- 一根可正常传输数据的 USB 线（部分充电线无法传输数据，请确认你的线支持数据传输）
- 一个稳定电源（电脑 USB 口或 5V 充电器均可）

如果你只是体验基础功能，上面三样就够了。想进一步做固定安装、外壳封装或外接传感器，可以再参考 [硬件指南](03-hardware.md)。

## 2. 烧录固件

> 如果你的开发板已经预装了 BikePower 固件，可以跳过此步骤，直接进入第 3 节。

### 2.1 下载固件

从项目的 [Release 页面](https://gitee.com/mengen4jv/bikepower-c/releases) 下载最新版本的 `bikepower-c-merged.bin` 文件。

这是合并固件，包含了引导加载器、分区表和应用程序，一个文件即可完成烧录。

下载后请校验文件完整性：

| 文件 | SHA-256 |
|------|---------|
| `bikepower-c-merged.bin` (v3.0.0) | `3ba1b3f0f56140123b90da232a5659a7f700eb537141b97eb44d793002210e7b` |

校验方法：

- **Windows**（PowerShell）：`CertUtil -hashfile bikepower-c-merged.bin SHA256`
- **macOS / Linux**：`sha256sum bikepower-c-merged.bin`

输出与上表一致即下载完整。

### 2.2 查找串口

ESP32-C3 内置 USB-Serial/JTAG，用 USB 线连接电脑后即可识别，无需额外安装驱动。

- **Windows**：打开"设备管理器" → "端口(COM 和 LPT)"，找到新增的 COM 口（如 `COM3`）
- **macOS**：终端运行 `ls /dev/tty.usb*`，找到类似 `/dev/tty.usbmodem-xxxx` 的设备
- **Linux**：终端运行 `ls /dev/ttyUSB*` 或 `ls /dev/ttyACM*`，找到对应设备

如果找不到串口，通常是 USB 线只支持充电，换一根数据线即可。

### 2.3 方法一：Windows 图形界面烧录（推荐新手）

使用乐鑫官方的 **Flash Download Tool**，无需命令行操作。

**第一步：下载工具**

从乐鑫官网下载 [Flash Download Tool](https://www.espressif.com/zh-hans/support/download/other-tools)（选择最新版本），解压后运行 `flash_download_tool_x.x.x.exe`。

**第二步：选择芯片类型**

- ChipType 选择 `ESP32-C3`
- WorkMode 选择 `Develop`
- 点击 OK 进入主界面

**第三步：配置烧录参数**

1. 第一行勾选框打勾，点击右侧 `...` 选择下载的 `bikepower-c-merged.bin` 文件
2. 地址栏填写 `0x0`（合并固件起始地址，必须为 0x0）
3. 下方参数设置：
   - SPI SPEED：`40MHz`
   - SPI MODE：`DIO`
   - COM：选择你的串口号（如 COM3）
   - BAUD：`460800`（烧录失败可降低为 `115200`）

**第四步：开始烧录**

1. 按住开发板上的 **BOOT** 键不要松开
2. 点击软件右下角的 **START** 按钮
3. 等待进度条出现并开始走，此时可以松开 BOOT 键
4. 显示 **FINISH** 即表示烧录成功
5. 按一下开发板上的 **RST**（复位）键，设备启动

> 如果 START 后进度条不动或报错，检查：COM 口是否正确、BOOT 键是否按住、USB 线是否支持数据传输。

### 2.4 方法二：命令行烧录（Windows / macOS / Linux）

适合有命令行经验的用户，跨平台通用。

**安装 esptool**

```bash
pip install esptool
```

> 如果没有 Python 环境，先从 [python.org](https://www.python.org/downloads/) 安装 Python 3。

**烧录固件**

将下面的串口号替换为你自己的（参见 2.2 节的查找方法）：

- Windows：`COM3`
- macOS：`/dev/tty.usbmodem-xxxx`
- Linux：`/dev/ttyUSB0`

```bash
esptool.py --chip esp32c3 -p COM3 -b 460800 write_flash --flash_mode dio --flash_size 2MB --flash_freq 80m 0x0 bikepower-c-merged.bin
```

出现 `Hash of data verified` 表示烧录成功。如果设备没有自动重启，按一下 RST 键。

> 如果报错 `Failed to connect to ESP32-C3`，按住 BOOT 键再运行命令，连接成功后松开。

### 2.5 方法三：ESP-IDF 烧录（适合开发者）

如果你已经安装了 ESP-IDF 开发环境：

```bash
git clone https://gitee.com/mengen4jv/bikepower-c.git
cd bikepower-c
idf.py -p /dev/ttyUSB0 flash monitor
```

此方法会从源码编译固件并烧录，适合需要修改代码的用户。

## 3. 第一次使用步骤

1. 给设备接电，等待设备启动
2. 打开手机或平板上的骑行 App，进入添加蓝牙设备页面
3. 在 BLE 设备列表里搜索 `BikePower`
4. 选择设备并完成配对或连接
5. 连接成功后，开始调功和骑行训练

如果 LED 处于慢闪，通常表示设备已经开机并正在等待蓝牙连接；连接成功后一般会变成常亮。

## 4. 配网指南

当你想改默认参数，例如功率、踏频、心率、设备名称等，可以进入设备自己的 WiFi 配网页面：

1. 长按按钮至少 `2` 秒后松开
2. 看到 LED 进入确认提示后，再按一次按钮
3. 手机连接 WiFi 热点 `BikePower`
4. 打开浏览器，访问 `192.168.4.1`
5. 按页面提示修改参数
6. 点击“保存设置”，配置会实时生效，页面仍可继续修改
7. 点击“保存并退出配网”，约 `5` 秒后设备关闭 WiFi 热点，BLE 保持或回到正常工作状态

配网模式是临时状态，不是开机默认状态。当前固件使用 WiFi/BLE 共存模式，普通配网不会主动断开 BLE，也不需要重启设备。

## 5. 常用骑行模式说明

- 日常自由骑：把它当成一个会广播骑行数据的蓝牙设备，连上就能用
- 手动调功：通过按钮快速上下调节训练强度，适合临时加练或暖身
- App 控制训练：支持 FTMS/ERG 类控制的 App 可以按训练计划自动下发目标功率
- 配网调整：当默认参数不适合你时，用手机连热点改配置即可

如果你只是想顺利用起来，不需要先理解底层协议。

## 6. 电源管理提示

- 建议使用稳定 USB 供电，避免劣质线材导致反复重启
- 如果设备需要长时间骑行使用，建议选择输出稳定的移动电源或车载供电方案
- 改配置时不要突然断电，避免保存过程中中断
- 如果发现连接不稳定，先排查供电是否不足，再看蓝牙环境是否拥挤

## 7. 常见问题 FAQ

### Q1：为什么我开机后搜不到设备？

先确认设备已经正常上电，然后观察 LED 是否在慢闪。如果没有任何灯效，先检查 USB 线和电源；如果仍然搜不到，再重启设备后重新搜索 `BikePower`。

### Q2：为什么进入不了配网？

进入配网需要两步：先长按至少 `2` 秒，再在确认窗口里补按一次。如果只做了第一步，设备会自动取消并回到蓝牙模式。

### Q3：手机连上热点后打不开页面怎么办？

确认手机连接的是 `BikePower` 热点，然后手动在浏览器输入 `192.168.4.1`。如果手机提示"此网络无互联网"，请选择继续连接，这是正常现象。

### Q4：保存配置后为什么 App 里很快看到新数值？

这是正常行为。点击“保存设置”后，功率、踏频、心率、模式等配置会实时写入并通过 BLE 通知更新；你仍然可以留在网页里继续调整。

### Q5：为什么配网时蓝牙还能继续工作？

当前版本启用了 WiFi/BLE 共存。普通配网时设备会打开临时 WiFi 热点，同时保持 BLE 在线；只有 OTA 升级或共存初始化失败兜底时，才可能短暂停用 BLE 或重启。
