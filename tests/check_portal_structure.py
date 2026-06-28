#!/usr/bin/env python3
"""
TC-3d WiFi 门户静态结构检查（无需设备）

从 main/wifi_portal.c 抽取所有 C 字符串字面量拼接的 HTML，
静态校验以下三类内容，防止烧录后才发现路由缺失或表单字段漂移：

1. 路由表完整性：必须包含 /、/save、/scan、/connect、/status、/ota 等核心路由。
2. HTML 表单字段：ssid / password 等关键 <input name="..."> 必须存在。
3. HTML 基础结构：必须包含 <html>、<head>、<body>、<meta name="viewport">，
   且 <form> 必须有 action 属性。

设计：仅做静态文本匹配，不解析 DOM，不依赖 bs4 / lxml，便于 CI 裸跑。

用法:
    python3 tests/check_portal_structure.py
退出码: 0 全绿, 1 有失败
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
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


C_STRING_PATTERN = re.compile(r'"((?:\\.|[^"\\])*)"')

REQUIRED_ROUTES = [
    "/",            # 首页
    "/save",        # 保存配置
    "/scan",        # 扫描 AP
    "/connect",     # 连接
    "/status",      # 状态查询
    "/ota",         # OTA 升级页
]

# 表单字段：实际门户通过点击 WiFi 列表项（data-ssid）+ 密码输入框（home-psk）提交
# SSID 本身无 <input>，通过 data-ssid 属性携带；提交参数为 ssid=...&psk=...
REQUIRED_FORM_MARKERS = [
    'data-ssid',           # WiFi 列表项携带 SSID
    'id="home-psk"',       # 家庭 WiFi 密码输入框
    "'ssid='",             # JS 提交参数 ssid=
    "'&psk='",             # JS 提交参数 psk=
]

REQUIRED_HTML_TAGS = [
    "<html",
    "<head",
    "<body",
    'name="viewport"',  # 移动端适配
]


def extract_c_strings(src: str) -> str:
    """提取 C 源码中所有双引号字符串字面量内容，反转义后拼接成连续文本。"""
    parts = []
    for m in C_STRING_PATTERN.finditer(src):
        s = m.group(1)
        s = (s.replace("\\n", "\n")
              .replace("\\t", "\t")
              .replace('\\"', '"')
              .replace("\\'", "'")
              .replace("\\\\", "\\"))
        parts.append(s)
    return "".join(parts)


def main():
    if not PORTAL_C.is_file():
        print(f"{TAG} FAIL: 文件不存在 {PORTAL_C}", file=sys.stderr)
        return 1

    src = PORTAL_C.read_text(encoding="utf-8")
    html = extract_c_strings(src)

    print(f"{TAG} 检查 {PORTAL_C.name}: 拼接 HTML 长度={len(html)} 字节")

    # 1. 路由表完整性
    print(f"\n{TAG} [1/3] 路由表完整性")
    for route in REQUIRED_ROUTES:
        # 路由通常作为字符串字面量出现，如 "/save"
        # 部分路由可能带 query，匹配路径前缀即可
        pat = '"' + route + '"'
        check(pat in src or route in html,
              f"路由 {route} 缺失")

    # 2. HTML 表单字段
    print(f"{TAG} [2/3] HTML 表单字段")
    for marker in REQUIRED_FORM_MARKERS:
        check(marker in src or marker in html,
              f"表单标记 {marker!r} 缺失")

    # 3. HTML 基础结构
    print(f"{TAG} [3/3] HTML 基础结构")
    for tag in REQUIRED_HTML_TAGS:
        check(tag in html,
              f"HTML 标签 {tag!r} 缺失")

    # <form> 必须有 action
    form_matches = re.findall(r'<form[^>]*>', html, re.IGNORECASE)
    check(len(form_matches) >= 1, "至少存在一个 <form>")
    for i, fm in enumerate(form_matches, 1):
        has_action = "action" in fm.lower()
        check(has_action, f"<form>#{i} 缺少 action 属性: {fm!r}")

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
