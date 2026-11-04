# BikePower 设备门户产品事实

核对日期：2026-11-04

- 产品：BikePower C，ESP32-C3 + ESP-IDF v5.5.4 + NimBLE。
- 当前源码固件版本：`4.2.1`。
- 板型：`generic-c3`、`m5stamp-c3`。
- 运动 Profile：cycling / running。
- WiFi 配网由物理按钮触发，不开机自启。
- 设备门户在 SoftAP Captive Portal 中运行。
- BLE 配置服务支持完整 NVS 事务读写；Pages 已支持设备名、输出参数、跑步参数和 LED 配置。
- 真实桥接首版只绑定一个 CPS 功率计，踏频来自同一功率计的曲柄字段。
- 心率仅模拟或关闭，不连接外部心率带。
- 本原型数值均为演示数据，不代表真机采样。
