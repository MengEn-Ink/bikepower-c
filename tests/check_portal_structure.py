#!/usr/bin/env python3
"""TC-3d WiFi 门户静态结构检查（无需设备，v3.2.0 重构版）

v3.2.0 起 HTML 不再嵌入 C 字符串，前端三件套独立存放：
  - main/web/index.html  - SPA 入口（含占位符）
  - main/web/app.css     - 样式
  - main/web/app.js      - SPA 逻辑

静态校验三类内容，防止烧录后才发现路由缺失或字段漂移：

1. HTML 基础结构：index.html 必须含 <html>/<head>/<body>/<meta viewport>，
   且必须保留 <!-- INLINE_CSS --> 与 <!-- INLINE_JS --> 占位符（编译时替换）。
2. 路由表一致性：wifi_portal.c 注册的路由必须包含前端调用的全部 /api/xxx。
3. 前端 API 调用完整性：app.js 必须调用 /api/state（初始化）和 /api/time（轮询）。

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
    if not PORTAL_C.is_file():
        print(f"{TAG} FAIL: 文件不存在 {PORTAL_C}", file=sys.stderr)
        return 1

    html = INDEX_HTML.read_text(encoding="utf-8")
    js = APP_JS.read_text(encoding="utf-8")
    csrc = PORTAL_C.read_text(encoding="utf-8")

    print(f"{TAG} 检查 index.html: {len(html)} 字节")
    print(f"{TAG} 检查 app.js: {len(js)} 字节")
    print(f"{TAG} 检查 wifi_portal.c: {len(csrc)} 字节")

    # 1. HTML 基础结构
    print(f"\n{TAG} [1/3] HTML 基础结构")
    for tag in REQUIRED_HTML_TAGS:
        check(tag in html, f"HTML 标记 {tag!r} 缺失")
    for ph in REQUIRED_PLACEHOLDERS:
        check(ph in html, f"占位符 {ph!r} 缺失")

    # 2. 后端路由表完整性
    print(f"{TAG} [2/3] wifi_portal.c 路由表完整性")
    for route in REQUIRED_BACKEND_ROUTES:
        # 路由在 C 源码中以 ".uri = "/api/xxx"" 形式出现
        pat = '.uri = "' + route + '"'
        check(pat in csrc, f"路由 {route} 未在 wifi_portal.c 注册")

    # 3. 前端 API 调用完整性
    print(f"{TAG} [3/3] app.js 前端 API 调用完整性")
    for api in REQUIRED_FRONTEND_APIS:
        # app.js 中可能以 "/api/xxx" 字符串调用，或拼接，统一查字符串
        check(('"' + api + '"') in js, f"app.js 未调用 {api}")

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
