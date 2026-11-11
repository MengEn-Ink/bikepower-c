# 54 - 设备配置页优化实施计划

**目标：** 落实已批准的工作台布局、深浅主题切换与浏览器持久化。

**架构：** 保留生产页面三件套、配置事务与设备接口。CSS 共用布局并以语义变量覆盖主题；JS 独立管理 `bikepower.portal.theme.v1`，不写入设备配置。手机演示与离线手册由现有生成器同步。

**技术：** 原生 HTML/CSS/JS、localStorage、Playwright、现有 Python 门禁。

## 执行步骤

- [x] 新增 `tests/test_portal_theme_browser.cjs`，先验证未实现时失败。覆盖系统初始深色、主题切换/刷新保持、存储不可用、键盘、草稿保留、不触发保存与续期。
- [x] 修改 `main/web/index.html` 顶栏，增加带文字和可访问标签的主题按钮。保持现有 ID 与内联入口。
- [x] 修改 `main/web/app.js`，读取并校验持久主题；无有效值时跟随系统，手动选择后优先使用选择。存储访问用 try/catch，主题按钮不进入配置与续期事件链。
- [x] 融合 `main/web/app.css`：340 像素独立桌面摘要、紧凑手机摘要、可读标签与输入、广播兼容独立行、深浅语义色、焦点与禁用态。
- [x] 更新 `tests/check_portal_structure.py` 的旧“仅明亮主题”契约为双主题契约，保持原体积预算。
- [x] 运行 `node tests/test_portal_theme_browser.cjs` 与 `python3 tests/check_portal_render.py`，检查双主题四宽截图、保存失败/重试、桥接、跑步、手机首屏。
- [x] 运行 `python3 scripts/tools/build_phone_demo.py`、`python3 scripts/tools/sync_user_docs.py`、`python3 scripts/tools/export_operation_demo.py`，并运行手机对齐浏览器验收。
- [x] 更新设计状态、用户说明、CHANGELOG 与 PROGRESS；记录内联资源增量和验证边界。

实施中补充：修正 `scripts/inline_static.py` 的类名误替换与后代伪类空格丢失，浏览器颜色和分行断言覆盖；新增专项接入 `tests/web-flash/package.json` 浏览器门禁。`scripts/tools/export_configuration_demo.py` 导出生产代码的隔离离线交互版。

本轮已获实施授权，直接按步骤执行；用户随后追加授权提交、推送并部署正式 GitHub Pages。固件烧录与版本发布仍单独授权。已有 BLE 规则及 AgentBuddy 文件保持基线。
