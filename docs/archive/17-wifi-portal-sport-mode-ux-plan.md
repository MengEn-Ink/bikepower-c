# WiFi 配置端运动模式交互实施计划

> 归档于 2026-11-05：生产页面实现与自动化步骤已执行，保留原计划用于追溯。当前交互规范见 [设计说明](../dev/26-wifi-portal-sport-mode-ux-design.md)，实物待验收项见 [PROGRESS](../../PROGRESS.md)。

> **For agentic workers:** 按任务顺序执行；每个生产代码改动前必须先看到对应浏览器断言因缺少行为而失败。

**Goal:** 草稿运动模式与设备已应用模式不一致时隐藏旧实时输出，并统一跑步模式文案、诊断和保存交互。

**Architecture:** 继续使用现有 `state.draft` 与 `state.applied` 双状态。前端通过单一 `sportModePending()` 判定控制工作区布局、待应用提示及保存按钮；实时数据仍只读取 `state.applied`，不构造伪预览。

**Tech Stack:** 原生 HTML/CSS/JavaScript、Python Playwright、生产内联压缩脚本。

---

## 文件结构

- 修改 `tests/portal_bridge_browser.py`：覆盖模式待应用、保存成功、失败重试、反向切换和跑步术语。
- 修改 `main/web/index.html`：增加待切换提示和可动态更新的诊断标签。
- 修改 `main/web/app.css`：增加单栏模式与待切换提示样式。
- 修改 `main/web/app.js`：统一模式待应用判定、布局、按钮和跑步实时语义。
- 修改 `CHANGELOG.md`、`PROGRESS.md`：记录行为与验证结论。

### Task 1：建立模式切换失败用例

**Files:**
- Modify: `tests/portal_bridge_browser.py`

- [x] **Step 1：写入未保存切换断言**

在点击跑步后、保存前增加：

```python
page.locator("[data-sport=running]").click()
expect(page.locator(".live-pane")).to_be_hidden()
expect(page.locator(".workspace")).to_have_class(re.compile(r"\bis-mode-pending\b"))
expect(page.locator("#modeSwitchNotice")).to_be_visible()
expect(page.locator("#modeSwitchNotice")).to_contain_text("跑步模式尚未应用")
expect(page.locator("#saveBtn")).to_have_text("保存并切换到跑步")
expect(page.locator("[data-sport=running]")).to_have_attribute("aria-pressed", "true")
expect(page.locator("[data-sport=cycling]")).to_have_attribute("aria-pressed", "false")
```

- [x] **Step 2：写入保存失败与恢复断言**

通过已有 `/api/mock/fault` 注入保存失败，断言实时区继续隐藏、草稿保留、按钮可重试；清除故障并保存后断言：

```python
expect(page.locator(".live-pane")).to_be_visible()
expect(page.locator("#liveTitle")).to_have_text("跑步广播")
expect(page.locator("#liveStatus")).to_have_text("跑步广播中")
expect(page.locator("#runtimeAlertTitle")).to_have_text("跑步模拟正在运行")
expect(page.locator("#eventStateLabel")).to_have_text("步频状态")
expect(page.locator("#appServices")).to_have_text("RSC / HRS / FTMS")
```

- [x] **Step 3：写入反向切换断言**

已应用跑步时点击骑行，断言旧跑步实时区隐藏、按钮为“保存并切换到骑行”；切回跑步草稿时实时区立即恢复。

- [x] **Step 4：运行测试确认 RED**

Run:

```bash
python3 tests/portal_bridge_browser.py
```

Expected: FAIL，首个失败为 `.live-pane` 未隐藏或 `#modeSwitchNotice` 不存在，而不是夹具或语法错误。

### Task 2：实现待应用布局和状态提示

**Files:**
- Modify: `main/web/index.html`
- Modify: `main/web/app.css`
- Modify: `main/web/app.js`

- [x] **Step 1：增加语义化 DOM**

在 `.editor-head` 后加入：

```html
<div class="mode-switch-notice" id="modeSwitchNotice" role="status" hidden>
  <b id="modeSwitchTitle">跑步模式尚未应用</b>
  <p id="modeSwitchText">保存后设备将重建跑步广播，当前 BLE 连接可能短暂断开。</p>
</div>
```

给跑步摘要、诊断标签和服务值增加 `runModeSummary`、`eventStateLabel`、`appServices` ID。

- [x] **Step 2：增加单栏布局**

```css
.workspace.is-mode-pending { grid-template-columns:minmax(0,1fr); }
.workspace.is-mode-pending .editor-pane { width:min(760px,100%); margin:auto; }
.mode-switch-notice {
  margin:0 0 10px; padding:10px 12px; border:1px solid #ead29e;
  border-radius:7px; background:var(--warn-soft);
}
.mode-switch-notice b { color:var(--warn); font-size:13px; }
.mode-switch-notice p { margin:3px 0 0; color:var(--muted); font-size:12px; }
```

窄屏沿用单栏，不增加新的断点或固定尺寸。

- [x] **Step 3：统一待应用判定**

在 `app.js` 增加：

```javascript
function sportModePending() {
  return !!state.draft && !!state.applied && state.draft.sport !== state.applied.sport;
}
```

在 `renderSport()` 中：

```javascript
var pending = sportModePending();
document.querySelector(".workspace").classList.toggle("is-mode-pending", pending);
document.querySelector(".live-pane").hidden = pending;
$("modeSwitchNotice").hidden = !pending;
$("modeSwitchTitle").textContent = state.draft.sport === "running"
  ? "跑步模式尚未应用" : "骑行模式尚未应用";
$("modeSwitchText").textContent = "保存后设备将重建"
  + (state.draft.sport === "running" ? "跑步" : "骑行")
  + "广播，当前 BLE 连接可能短暂断开。";
```

在 `updateDirty()` 中让 profile 切换优先于普通保存文案：

```javascript
if (sportModePending()) {
  $("saveBtn").textContent = "保存并切换到" + (state.draft.sport === "running" ? "跑步" : "骑行");
  $("saveHint").textContent = state.busy ? "正在切换运动模式…" : "保存后 BLE 服务将短暂重建";
}
```

- [x] **Step 4：运行浏览器测试确认 GREEN**

Run:

```bash
python3 tests/portal_bridge_browser.py
```

Expected: 模式隐藏、失败重试和反向切换断言通过。

### Task 3：收敛跑步实时语义与可访问性

**Files:**
- Modify: `main/web/app.js`

- [x] **Step 1：同步按钮选中语义**

扩展 `setActive()`：

```javascript
var active = button.dataset[key] === value;
button.classList.toggle("is-active", active);
button.setAttribute("aria-pressed", String(active));
```

- [x] **Step 2：更新跑步实时文案**

在 `renderLive()` 根据 `running` 设置：

```javascript
$("liveStatus").textContent = running ? "跑步广播中"
  : bridge ? (active ? labels[b.state] : "上游未连接") : "模拟运行";
$("runtimeAlertTitle").textContent = running ? "跑步模拟正在运行"
  : bridge ? (valid ? "桥接正在运行" : "真实数据不可用，功率归零") : "模拟输出正在运行";
$("runtimeAlertText").textContent = running ? "速度与步频由设备本机平滑生成。"
  : bridge ? (valid ? "功率与踏频来自同一台功率计。" : "请检查功率计；可重新连接或选择模拟后保存。")
  : "输出由设备本机平滑生成。";
$("eventStateLabel").textContent = running ? "步频状态" : "曲柄事件";
$("appServices").textContent = running ? "RSC / HRS / FTMS" : "CPS / CSC / FTMS";
```

- [x] **Step 3：更新跑步编辑摘要**

在 `renderRunning()` 设置：

```javascript
$("runModeSummary").textContent = run.mode === "fixed" ? "固定配速" : "配速档";
```

- [x] **Step 4：重跑浏览器测试**

Run:

```bash
python3 tests/portal_bridge_browser.py
python3 tests/portal_exception_browser.py
```

Expected: 两项 PASS，三种视口零 `pageerror`，异常保存仍保留草稿。

### Task 4：完成静态、资源和文档门禁

**Files:**
- Modify: `CHANGELOG.md`
- Modify: `PROGRESS.md`

- [x] **Step 1：运行静态及渲染门禁**

```bash
python3 tests/check_portal_js.py
python3 tests/check_portal_structure.py
python3 tests/check_portal_render.py
git diff --check
```

Expected: 全部 PASS。

- [x] **Step 2：测量压缩资源**

```bash
python3 scripts/inline_static.py --web-dir main/web --output /tmp/bikepower-sport-mode-web.h --report
```

Expected: `output_bytes < 112640`，相对基线 72,386 bytes 增量不超过 3,072 bytes。

- [x] **Step 3：运行 quick 操作门禁**

```bash
python3 tests/operation_gate.py --level quick --report build/sport-mode-ux-operation-quick.md
```

Expected: 零 FAIL；本轮不修改固件 C、协议或 NVS，因此不要求重新烧录真机。

- [x] **Step 4：同步文档**

在 `CHANGELOG.md` 未发布条目记录模式待应用隐藏、跑步语义和浏览器验证；在 `PROGRESS.md` 记录自动验证结果及未重新执行的真实热点/手机 Safari 边界。

- [x] **Step 5：提交边界**

本计划不自动创建 Git commit。仅在用户明确要求提交后，按“前端与测试”和“文档”分批暂存、提交并推送。

### Task 5：纠正诊断区事实来源

**Files:**
- Modify: `tests/portal_bridge_browser.py`
- Modify: `main/web/index.html`
- Modify: `main/web/app.js`

- [x] **Step 1：先验证硬编码诊断缺少动态元素**

浏览器断言 `schemaVersion`、`heapValue`、`upstreamStateLabel`，确认旧页面因元素不存在而失败。

- [x] **Step 2：复用现有事实来源**

配置版本读取 `data.config.schema_ver`；空闲堆读取现有 `/api/mem_stats`，首次加载后刷新并每 10 秒更新；请求失败显示“不可用”，不影响主配置加载。

- [x] **Step 3：收敛跑步诊断**

跑步模式显示 `会话来源 / 本机模拟`，骑行模式继续显示真实功率桥接状态。

- [x] **Step 4：验证资源和交互**

生产三视口浏览器回归、结构与 JS 门禁通过；最终压缩内联资源 74,932 bytes，较 72,386 bytes 基线增加 2,546 bytes；跑步配置段编号连续为 01/02。
