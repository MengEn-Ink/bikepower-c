# BikePower · UI Theme Spec

> 来源：`/Users/bytedance/work/strava_photobook/runtime/dashboard/dashboard.css`、BikePower 现有页面源码和用户在该项目已确认的 Distance Manifesto 主题偏好。
> 资产完整度：部分。没有独立 logo 文件；方向稿沿用产品内已经出现的 `BikePower` 文字标识与 `BP` 简写，不把它冒充为正式品牌 Logo。

## 核心资产

- 产品名：`BikePower`
- 简写：`BP`（现有监控页已使用）
- 数字产品 UI：`tools/web/ble-monitor.html`、`main/web/index.html`
- 参考截图：`refs/current-monitor.png`、`refs/current-config.png`

## 主题令牌

### 颜色

- `--black: #0a0807`：主背景。
- `--brown: #17110e`：一级面板。
- `--brown-2: #211711`：二级控件和图表底。
- `--paper: #d8c7b5`：正文。
- `--light: #f4e8d8`：强标题/主数字。
- `--rust: #f05523`：唯一品牌主强调、主操作和当前状态。
- `--rust-dark: #70230f`：警告背景和图表低强度。
- `--line: #58463a`：结构线。
- `--muted: #aa998c`：次级信息。
- 状态扩展：成功 `#9fcb71`、危险 `#ff765d`、协议提示 `#a78bfa`；只作为语义色，不与锈橙争主视觉。

### 字型

- Display：`"Bodoni 72", Didot, serif`，仅用于表现型大数字/方向标题。
- Sans：`"Avenir Next", "PingFang SC", sans-serif`，操作与正文。
- Song：`"Songti SC", STSong, serif`，少量解释性文案。
- Mono：`Menlo, ui-monospace, monospace`，协议、时间、单位、日志和小标签。

### 间距与形状

- 基础间距：4 / 8 / 12 / 16 / 24 / 32 / 48。
- 主分区以 1px 结构线定义，不依赖大量悬浮卡片阴影。
- 圆角按方向解释：编辑型 0–4px；工业型 8–12px；极简型 0px。
- 触控目标不小于 44×44px，网页正文不小于 14px，移动正文不小于 16px。

## 组件词汇

- 状态：文字 + 实心小点 + 时间，不使用 emoji 作为状态主语义。
- 数据：数值、单位、采样来源三层分离；实时数据必须带“最后通知”或连接状态。
- 控制：主动作只保留一个锈橙实心按钮；关闭 WiFi 属风险动作，必须与普通保存区分。
- 图表：功率/心率/踏频共享时间轴，颜色由语义而非装饰决定。

## 禁区

- 不回到蓝紫发光通用 IoT Dashboard。
- 不用 emoji 充当核心图标。
- 不把每个字段都包成圆角卡片。
- 不把示例数据说成真机实测。
- 不隐藏首次 BLE 授权、WiFi 关闭、ERG 接管等真实状态与风险。
