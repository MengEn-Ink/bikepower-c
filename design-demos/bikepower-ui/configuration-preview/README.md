# 配置页设计与本地实现

[打开新版离线交互页](configuration.html)：运行当前生产 HTML/CSS/JS，接口使用隔离模拟数据。主题可切换并在浏览器允许存储时保持；修改与保存只作用于本页模拟数据，刷新重置模拟配置。文件打开后不请求真实设备或网络。

新版截图：

- [手机浅色](production-mobile-light.png) · [手机深色](production-mobile-dark.png)
- [桌面浅色](production-desktop-light.png) · [桌面深色](production-desktop-dark.png)

`index.html`、`screen.html`、`preview.css` 及不含 `production-` 前缀的图片是用户批准前的静态设计参考，文案与样本保持当时版本；不代表新版设备页面已部署。

重新生成离线交互页：`python3 scripts/tools/export_configuration_demo.py`。正式资源与测试结果见 [设计文档](../../../docs/dev/53-device-configuration-design.md)。
