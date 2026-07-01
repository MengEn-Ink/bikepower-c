#!/usr/bin/env python3
"""TC-3d WiFi 门户静态结构检查（无需设备，v3.2.0 重构版）

v3.2.0 起 HTML 不再嵌入 C 字符串，前端三件套独立存放：
  - main/web/index.html  - SPA 入口（含占位符）
  - main/web/app.css     - 样式
  - main/web/app.js      - SPA 逻辑

静态校验四类内容，防止烧录后才发现路由缺失或字段漂移：

1. HTML 基础结构：index.html 必须含 <html>/<head>/<body>/<meta viewport>，
   且必须保留 <!-- INLINE_CSS --> 与 <!-- INLINE_JS --> 占位符（编译时替换）。
2. 路由表一致性：wifi_portal.c 注册的路由必须包含前端调用的全部 /api/xxx。
3. 前端 API 调用完整性：app.js 必须调用 /api/state（初始化）和 /api/time（轮询）。
4. 前端信息架构：底部只保留首页/设置两 Tab，用户资料、设备信息、关于并入设置页。
   物理按钮和 LED 自定义配置归入设备信息分区。

设计：仅做静态文本匹配，不解析 DOM，不依赖 bs4 / lxml，便于 CI 裸跑。

用法:
    python3 tests/check_portal_structure.py
退出码: 0 全绿, 1 有失败
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INDEX_HTML = ROOT / "main" / "web" / "index.html"
APP_JS = ROOT / "main" / "web" / "app.js"
APP_CSS = ROOT / "main" / "web" / "app.css"
PORTAL_C = ROOT / "main" / "wifi_portal.c"

TAG = "[TC-3d]"
g_pass = 0
g_fail = 0
failures = []


def check(cond: bool, msg: str):
    global g_pass, g_fail
    if cond:
        g_pass += 1
    else:
        g_fail += 1
        failures.append(msg)
        print(f"{TAG}   FAIL {msg}")


REQUIRED_HTML_TAGS = [
    "<html",
    "<head",
    "<body",
    'name="viewport"',  # 移动端适配
]

REQUIRED_PLACEHOLDERS = [
    "<!-- INLINE_CSS -->",
    "<!-- INLINE_JS -->",
]

# 前端 app.js 必须调用的核心 API（防止删错路由后前端黑屏）
# 注：/api/mem_stats 自 v3.2.1 起不再被前端调用（关于页移除内存统计展示）
REQUIRED_FRONTEND_APIS = [
    "/api/state",
    "/api/time",
    "/api/renew",
    "/api/config",
    "/api/close-wifi",
]

# wifi_portal.c 必须注册的核心路由（防止改坏路由表）
REQUIRED_BACKEND_ROUTES = REQUIRED_FRONTEND_APIS + ["/"]


def main():
    if not INDEX_HTML.is_file():
        print(f"{TAG} FAIL: 文件不存在 {INDEX_HTML}", file=sys.stderr)
        return 1
    if not APP_JS.is_file():
        print(f"{TAG} FAIL: 文件不存在 {APP_JS}", file=sys.stderr)
        return 1
    if not APP_CSS.is_file():
        print(f"{TAG} FAIL: 文件不存在 {APP_CSS}", file=sys.stderr)
        return 1
    if not PORTAL_C.is_file():
        print(f"{TAG} FAIL: 文件不存在 {PORTAL_C}", file=sys.stderr)
        return 1

    html = INDEX_HTML.read_text(encoding="utf-8")
    js = APP_JS.read_text(encoding="utf-8")
    css = APP_CSS.read_text(encoding="utf-8")
    csrc = PORTAL_C.read_text(encoding="utf-8")

    print(f"{TAG} 检查 index.html: {len(html)} 字节")
    print(f"{TAG} 检查 app.js: {len(js)} 字节")
    print(f"{TAG} 检查 app.css: {len(css)} 字节")
    print(f"{TAG} 检查 wifi_portal.c: {len(csrc)} 字节")

    # 1. HTML 基础结构
    print(f"\n{TAG} [1/4] HTML 基础结构")
    for tag in REQUIRED_HTML_TAGS:
        check(tag in html, f"HTML 标记 {tag!r} 缺失")
    for ph in REQUIRED_PLACEHOLDERS:
        check(ph in html, f"占位符 {ph!r} 缺失")

    # 2. 后端路由表完整性
    print(f"{TAG} [2/4] wifi_portal.c 路由表完整性")
    for route in REQUIRED_BACKEND_ROUTES:
        # 路由在 C 源码中以 ".uri = "/api/xxx"" 形式出现
        pat = '.uri = "' + route + '"'
        check(pat in csrc, f"路由 {route} 未在 wifi_portal.c 注册")

    # 3. 前端 API 调用完整性
    print(f"{TAG} [3/4] app.js 前端 API 调用完整性")
    for api in REQUIRED_FRONTEND_APIS:
        # app.js 中可能以 "/api/xxx" 字符串调用，或拼接，统一查字符串
        check(('"' + api + '"') in js, f"app.js 未调用 {api}")

    # 4. 前端信息架构
    print(f"{TAG} [4/4] 前端信息架构")
    nav_items = re.findall(r'<a\b[^>]*class="nav-item"[^>]*data-h="([^"]+)"', html)
    check(nav_items == ["/home", "/settings"], "底部导航必须只有 首页/设置 两个 Tab")
    check('id="view-settings"' in html, "缺少设置视图 view-settings")
    check('id="view-more"' not in html, "旧更多视图 view-more 必须并入设置")
    check('id="view-about"' not in html, "旧关于视图 view-about 必须并入设置")
    check('id="settings-profile"' in html, "设置页缺少用户资料分区")
    check('id="settings-device"' in html, "设置页缺少设备信息分区")
    check('id="settings-about"' in html, "设置页缺少关于分区")
    check('id="settings-device-name"' in html, "设备名称输入必须挪入设置页")
    check('id="about-dev"' not in html, "设备名称不应继续留在关于页")
    check('pager-track' not in html and 'pager-track' not in css, "关于内容不应继续使用横向分页")
    check('"/settings": renderSettings' in js, "app.js 路由缺少 /settings -> renderSettings")
    check('"/more":' not in js and '"/about":' not in js, "app.js 不应保留更多/关于作为一级路由")

    profile_pos = html.find('id="settings-profile"')
    device_pos = html.find('id="settings-device"')
    about_pos = html.find('id="settings-about"')
    button_pos = html.find("<h2>物理按钮</h2>")
    check(device_pos < button_pos < about_pos, "物理按钮卡片必须位于设备信息分区")
    check(not (profile_pos < button_pos < device_pos), "物理按钮不应留在用户资料分区")

    led_control_ids = [
        "settings-led-card", "settings-led-silent",
        "settings-led-wait-mode", "settings-led-wait-color",
        "settings-led-conn-mode", "settings-led-conn-color",
        "settings-led-wifi-mode", "settings-led-wifi-color",
        "settings-led-confirm-mode", "settings-led-confirm-color",
        "settings-led-error-mode", "settings-led-error-color",
    ]
    for ctl in led_control_ids:
        check(f'id="{ctl}"' in html, f"LED 自定义配置控件 {ctl} 缺失")
    for key in [
        "led_silent",
        "led_wait_mode", "led_wait_color",
        "led_conn_mode", "led_conn_color",
        "led_wifi_mode", "led_wifi_color",
        "led_confirm_mode", "led_confirm_color",
        "led_error_mode", "led_error_color",
    ]:
        check(key in js, f"app.js 缺少 LED 配置字段 {key}")

    # 总结
    print(f"\n{TAG} 结果: {g_pass} 通过, {g_fail} 失败")
    if g_fail == 0:
        print(f"{TAG} PASS: WiFi 门户静态结构全部通过")
        return 0
    else:
        print(f"{TAG} FAIL: 存在结构缺失，禁止编译/烧录", file=sys.stderr)
        for m in failures:
            print(f"{TAG}   - {m}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
