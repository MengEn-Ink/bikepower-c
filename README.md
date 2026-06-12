# BikePower C

ESP32-C3 蓝牙功率计模拟器（C 重写版）。由 MicroPython 实现完全重写为 **C + ESP-IDF + NimBLE**，对外提供标准 BLE 功率计/踏频/心率服务与 FTMS ERG，可被主流骑行 App 识别。

## 项目信息

| 项 | 值 |
|------|------|
| 硬件平台 | 合宙 ESP32-C3（RISC-V 单核 160MHz，400KB SRAM，WiFi + BLE5.0） |
| 框架 | ESP-IDF v5.5.1 |
| BLE 栈 | NimBLE（peripheral） |
| RTOS | FreeRTOS（多任务 + Queue + 信号量） |
| 语言 | C11 |
| 目标版本 | v3.0.0 |

## 核心功能

- BLE 五服务：功率计（CPS）、踏频、心率、设备信息（DIS）、FTMS（含 ERG 模式）。
- 物理按钮调功（GPIO9 中断驱动）+ LED 四态指示（GPIO12）。
- WiFi 配网（长按 + 二次确认触发，与 BLE 射频互斥软切换）。

> 详细阶段进度、待办与已知限制见 [PROGRESS.md](PROGRESS.md)。

## 目录结构

```
bikepower-c/
├── main/                       — 应用层（入口/状态机/按钮/LED/BLE/配置常量）
├── components/bikepower_domain/ — 纯 C 领域层（功率引擎/心率模型/用户配置），可 host 单测
├── tests/                      — BLE 自测与串口抓取脚本
├── test/test_domain            — 领域层 host 单测
├── docs/                       — 设计与重写方案
├── partitions.csv              — 分区表
└── sdkconfig.defaults          — 默认配置
```

## 构建与烧录

需先安装并 source ESP-IDF v5.5.1 环境。

```bash
# 编译
idf.py build

# 烧录并打开串口监视（替换 <port> 为实际串口，如 /dev/ttyUSB0）
idf.py -p <port> flash monitor
```

### 领域层 host 单测

```bash
cd test/test_domain
idf.py build
```

## 硬件约束

- **WiFi/BLE 射频互斥**：ESP32-C3 只有一路 RF，启动 WiFi 前必须先停止 BLE，二者无法共存。
- **禁止开机自启 WiFi**：配网模式必须由用户长按 + 二次确认触发。

详见 [.trae/rules/hardware-constraints.md](.trae/rules/hardware-constraints.md) 与 [.trae/rules/wifi-ble-mutex.md](.trae/rules/wifi-ble-mutex.md)。
