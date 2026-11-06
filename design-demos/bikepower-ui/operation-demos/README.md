# BikePower 操作演示

直接打开 `index.html`，选择要做的事。每个演示可播放、暂停、逐步查看。

| 文件 | 内容 |
|---|---|
| `index.html` | 设备/手机双入口 + 15 类补充教程，支持浅深主题和离线查看；手机屏幕复用真实配置页 |
| `wifi.gif` / `wifi.mp4` | 配网：按住、松开回滚、二次确认 |
| `phone.gif` / `phone.mp4` | 手机：编辑、保存失败、保留草稿、重试成功、确认关闭；GIF 720 × 960，MP4 960 × 1280 |
| `operations.mp4` | 设备操作 7 秒 + 手机操作约 14.5 秒，960 × 1280 |
| `phone-edit.png` / `phone-save-failed.png` / `phone-save.png` / `phone-closed.png` | 真实页面的编辑、失败、成功与退出画面 |
| `phone-light.png` / `phone-dark.png` / `phone-mobile.png` | 手机主体的双主题与移动端效果 |
| `phone-bridge-light/dark.png` / `phone-running-light/dark.png` | 功率计测试及跑步真实页面 |
| `pages-phone-390/1280-light/dark.png` | 最终本地站点的手机操作预览 |

先前的 `save.png`、`bridge.png`、`handoff.png`、`desktop-*.png`、`pages-*.png` 为第一阶段演示截图。最新手机界面以 `phone-*.png` 和 `pages-phone-*.png` 为准。

**注意：** 配网要先松开再确认；保存成功后再退出；连接 App 前释放网页连接。灯色以你的设置为准。

生产来源为 `main/web/index.html/app.css/app.js` 与 `tools/web/` 中的设备/手机演示模块。Pages、手机离线手册与本目录共用来源；可用 `python3 scripts/tools/build_phone_demo.py --check` 检查漂移，用 `python3 scripts/tools/export_operation_demo.py` 重建独立 HTML。本目录为静态交付快照，验证与实施边界见 [记录](../../../docs/dev/39-pages-operation-demos.md)。

手机外壳、系统状态栏和接口数据是演示。内页由 Chromium 渲染；已核对页面位置和触屏模拟，尚未进行 iOS 真机 / Safari、系统弹窗及第三方 App 实录验收。
