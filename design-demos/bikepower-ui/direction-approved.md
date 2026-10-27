# BikePower UI · 方向定稿

## 用户选择原话

> 先按01 · 信号宣言 改下pages监控页面,推送实现功能

## 已选方向

- 方向：`01 · 信号宣言`
- 设计逻辑：Experimental Jetset 式严格网格与 Distance Manifesto 深黑、锈橙主题。
- 页面主角：真实 BLE 通知驱动的功率数值、功率/踏频/心率共享时间轴和会话健康证据。
- 实施范围：`tools/web/ble-monitor.html` GitHub Pages 监控页面；本轮不修改固件内 Captive Portal 配置页。

## 已确认方向稿

- 桌面监控：`screenshots/dir-1-monitor.png`
- 移动配置：`screenshots/dir-1-config.png`
- 可交互原型：`dir-1.html`

## 生产实施约束

- 保留 ESP Web Tools 固件刷写、Web Bluetooth 主动授权、DIS/GATT 枚举、五类实时通知、异常检测、JSON 报告、模板、多设备比对和事件日志。
- 未连接时仅显示诚实空状态，不把方向稿演示数值或曲线冒充为设备数据。
- 页面默认进入实时监控，固件刷写与设备比对作为顶部次级入口。
- 同时验证 1440×900 与 375×812；可点元素不小于 44px，控制台不得出现错误。
