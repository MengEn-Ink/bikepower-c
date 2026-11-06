# Pages 全站布局与排版更新

## 范围

后续用户已批准将本次版式与移动精简/适配采集、WiFi配网引导提交main并部署。本记录未提交/未部署是本地实施阶段状态；当前发布以新提交对应的GitHub Actions为准，历史本地触屏失败证据仍保留。本次提交前重新同步手机来源与独立操作HTML、截图前滚动至iframe，原严格手机专项0像素差异且坐标触屏保存通过；不据此推定此前失败根因，也不代替真机/Safari。

按用户确认调整整个 Pages 网站。参考 https://hzdavy.github.io/MoteDeck/ 的轻导航、标题层级、主操作演示和分段讲解，保留 BikePower 自身的双主题、金色强调和已有功能。没有复制参考站代码或素材。

不修改 `main/web/` 设备热点配网页、固件 C、BLE 协议、NVS、设备算法或发布链路。原有未跟踪草稿保留；本轮没有提交、推送、部署或烧录。

## 变更

- `tools/web/ble-monitor.html`：三分区下拉导航，全部七个原有工具路由保留；首页短标题与两项主入口；实时监控、设备比对、刷写、通知和健康检查统一标题说明。
- `tools/web/operation-demos.js`：首页以介绍、演示、任务入口、工具入口和详细目录的顺序组织，基础信息和目录按需展开；保留各章节与原有演示。
- `tools/web/pages-experience.css`：统一宽度、间距、排印、控件与主题；首页移除常驻长目录；手机讲解上下排列，桌面手机演示图文并排；七个工具页保持各自必要的信息密度。
- `tools/web/pages-experience.js`：导航选中后关闭菜单，点击外部关闭，Escape 关闭并恢复焦点；不干预设备会话。
- `scripts/tools/sync_user_docs.py` 与 `docs/user/pages-mobile-guide.html`：离线手册样式加载顺序与 Pages 一致。
- `tests/test_pages_layout_browser.cjs`：七路由、分区激活、关闭菜单、键盘焦点、首屏顺序、横向溢出和顶栏文字回归；已接入原浏览器测试入口。
- `tests/test_pages_experience_browser.cjs`：按新导航先展开分区，再选择工具，保留原断言。

## 验证记录

- `npm test --prefix tests/web-flash`：46 项全部通过，初始化 DOM 与遥测专项通过。
- `python tests/check_ble_monitor.py`：页面静态契约通过，含内联 JS、配置编码与原功能入口。
- `python tests/test_prepare_web_flash_site.py`：14 项全部通过。
- `python scripts/tools/sync_user_docs.py --check`：最终版与 Pages 来源一致。
- Markdown 链接：67 份文档、0 错误；`git diff --check` 通过。
- 新版式浏览器：浅深主题 × 900/1280/1600 px，七路由、菜单、首屏顺序、顶栏不重叠、帮助不换行、无横向溢出全部通过。
- 实际 ESP Web Tools 10.4.0：取消选择、显式擦除/安装确认、初始化错误及恢复回归通过，未接硬件。
- 既有 Pages 教学：浅深主题 × 360/390/768/900/1280/1600 px 全部通过。
- 操作演示：浅深主题 × 320/390/900/1280 px，确认、保存失败重试、0W、交接、键盘与减少动画通过；离线演示无网络请求通过。
- 遥测：浅深主题 × 320/390/900/1280 px，路由、百分比、暂停/恢复、断开与布局通过。
- 桥接与标准解析：双主题八组保存/绑定保留/旧固件/焦点、CSC/回绕/FTMS/截断/来源/混合快照拒绝通过。
- 手机专项的控件几何、生产 CSS/JS 一致、保存失败重试、关闭、扫描、0W、跑步、名称、骑行、生命周期和隔离步骤通过；像素比较保留10像素阈值。截图比较前统一减少动画后通过，避免动画采样相位差；Windows源码比较仅规范化CRLF为LF。
- **未通过：手机触屏坐标保存**。独立 Chromium 141 与 Chrome 156 的缩放 iframe 自动点击均未在内部控件产生事件；显式 locator tap/click 探针同样没有内部事件，保存输出保持200。没有足够证据判断属于自动化环境还是页面问题，故完整 `npm run browser` 仍为失败，不能宣称全部浏览器回归通过。保留原坐标断言，不放宽阈值，不跳过失败用例；真实手机/另一平台复验待完成。

## 本地预览与边界

本轮使用本机已有 Chromium 141、Chrome 156 的独立无头实例（不使用用户浏览器登录资料）。当前依赖版本所需的无头组件未安装，下载缓慢后停止，测试由显式指定的现有浏览器执行。项目已声明的 pngjs/pixelmatch 依赖补装在 `tests/web-flash/node_modules`，没有修改全局浏览器配置；没有伪造浏览器验证。

`tests/test_phone_operation_demos_browser.cjs` 仅将源码读取的CRLF规范化为LF，并在截图比较前让两侧采用相同减少动画偏好；原10像素阈值及触屏坐标保存断言保留。

本地预览产物位于 `design-demos/bikepower-ui/pages-refresh/`，使用现有站点生成器复制最终代码和引用配图，监听 `127.0.0.1:28763`（只对本机开放）。正式固件刷写在本地预览中明确禁用，Release 固件未被复制或重新发布。演示数据不替代真实设备通知。

新版源代码在 `tools/web/`，预览目录是生成副本，后续修改应编辑源代码并重新生成，不手改副本。

未执行真机蓝牙、USB 刷写、iPhone/Safari 与实体按钮/LED 验收；本轮只验证 Pages 的代码与浏览器交互。没有执行固件编译，因为未改固件源码。
