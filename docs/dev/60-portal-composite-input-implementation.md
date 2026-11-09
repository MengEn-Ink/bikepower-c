# 60 - 组合输入与交互修复实施计划

目标：将功率/踏频的目标与浮动合并，使用图标切换主题，并修复已确认的输入反馈、弹窗焦点和空配速说明。

架构：保留现有四个数字输入 ID、草稿和事务；原生浮动滑杆通过 `input` 事件同步已有数字输入；主题 SVG 由主题属性控制可见，JS 更新按钮名称和提示。

技术：原生 HTML/CSS/JS、现有 mock、Playwright 与离线生成器。

## 步骤

- [x] 在 `tests/test_portal_inputs_browser.cjs` 增加浏览器专项并确认红测：主题按钮包含 SVG；同一组合栏内具有目标与浮动输入；拖动/键盘/触屏同步；非法值保留并标错；保存失败不丢草稿；扫描末尾 Tab 回到首项；空配速无 NaN。
- [x] 修改 `main/web/index.html`：两组 `.paired-input` 分别容纳 `simPower/powerFloat`、`simCadence/cadenceFloat`，浮动滑杆为 `powerFloatRange/cadenceFloatRange`；数字输入有独立名称、单位与描述，错误帮助为 `powerInputHelp/cadenceInputHelp`。
- [x] 修改 `main/web/app.js`：`applyTheme()` 只更新名称/title，不清空 SVG；`renderFixedInputs()` 同步范围值和 `aria-invalid`，保存/结束禁用；范围事件写入原浮动输入并派发 `input`；机器人收起固定浮动；配速说明先校验有限值；焦点列表过滤隐藏控件。
- [x] 修改 `main/web/app.css`：组合栏共用边框，目标与浮动分隔，触点至少 44 px；手机纵向、桌面并列；等价合并样式保持原预算，不添加动画库。
- [x] 运行 `node tests/test_portal_inputs_browser.cjs`、`node tests/test_portal_theme_browser.cjs`、`node tests/test_portal_intensity_browser.cjs`，检查双主题四宽及 Chromium 合成触屏/键盘证据。
- [x] 运行 `python3 tests/check_portal_render.py --color-scheme light` 与 `--color-scheme dark`，复核原保存/桥接/跑步流程；运行结构、JS 与门禁接线检查。
- [x] 同步四个演示生成器，运行 `node tests/test_phone_operation_demos_browser.cjs` 和生成器 `--check`，查看手机/桌面截图，交付可离线操作页面。
- [x] 更新 README、手册、索引、测试说明、CHANGELOG 与 PROGRESS；写入 `tests/reports/portal-inputs-20261109/report.md`，区分已修复问题、后续建议、浏览器证据与真机 NOT_RUN。

实现约束示例：

```javascript
range.addEventListener("input", function () {
  number.value = this.value;
  number.dispatchEvent(new Event("input", {bubbles:true}));
});
```

以上同步只改变草稿，不调用保存接口。无效数字仍由原校验阻止保存，范围控件不得替换它。原生滑杆保留 `step=1`、min/max 与有单位的 `aria-valuetext`。

本轮在当前未提交页面上继续实施；已有 BLE/工程规则与 AgentBuddy 文件保持，不创建提交、推送、tag 或 Release。

## 验证结果

确定性红测先失败于主题 SVG 数量 `0 !== 2`；实现后专项通过。鼠标拖动测试滚动滑杆至视口中间再拖动，避免固定保存栏影响自动化坐标；截图等待保存提示结束，并将组合栏滚动到中央。离线页由同一生产源码和隔离接口导出，模拟保存无外部请求。

资源最终为 HTML 21,491、CSS 24,553、JS 59,741 字节，内联总 105,784 字节；比上一轮增加 3,287 字节。原预算未放宽，分项余量很小，后续扩展需先重整静态资源。

Impeccable 静态扫描仅有无数据破折号提示；渲染扫描返回 5 项 nested-cards warning，经截图/DOM核对分别是分段选择器和组合输入边框，不是嵌套内容卡片，详见报告。保留原始结果，不把工具返回码 2 写成扫描全绿。真实设备和移动辅助技术验收未运行。
