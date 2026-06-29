#!/usr/bin/env python3
"""
TC-3d WiFi 门户静态结构检查（无需设备）

从 main/wifi_portal.c 抽取所有 C 字符串字面量拼接的 HTML，
静态校验以下四类内容，防止烧录后才发现路由缺失、表单字段漂移或前端回归：

1. 路由表完整性：必须包含首页、配置保存、OTA、WiFi、模块页等核心路由。
2. HTML 表单字段：ssid / password 等关键 <input name="..."> 必须存在。
3. HTML 基础结构：必须包含 <html>、<head>、<body>、<meta name="viewport">，
   且 <form> 必须有 action 属性。
4. 回归点：保存强反馈、OTA 升级进度端点、OTA changelog 安全渲染、/more 个人信息前端校验。

设计：仅做静态文本匹配，不解析 DOM，不依赖 bs4 / lxml，便于 CI 裸跑。

用法:
    python3 tests/check_portal_structure.py
    python3 tests/check_portal_structure.py --file /tmp/wifi_portal_bad.c
退出码: 0 全绿, 1 有失败
"""
import re
import sys
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PORTAL_C = ROOT / "main" / "wifi_portal.c"

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
    "/config",      # 保存配置
    "/close-wifi",  # 保存并关闭 WiFi
    "/more",        # 更多配置
    "/ui/module",   # 动态模块
    "/ota",         # OTA 升级页
    "/ota/check",   # OTA 检查
    "/ota/status",  # OTA 检查状态
    "/ota/upgrade", # OTA 升级触发
    "/ota/upgrade/status",  # OTA 升级进度
    "/wifi/scan",   # 扫描 AP
    "/wifi/scan/status",    # 扫描状态
    "/wifi/save",   # 保存家庭 WiFi
    "/wifi/connect/status", # 连接状态
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

SAVE_FEEDBACK_MARKERS = [
    "已保存到设备",
    "已通过蓝牙实时生效",
    "saved=1",
]

OTA_UPGRADE_STATUS_MARKERS = [
    "/ota/upgrade/status",
    "h_ota_upgrade_status",
    "升级进度轮询",
    "\"state\":\"%s\"",
    "\"read\":%d",
    "\"total\":%d",
]

OTA_XSS_MARKERS = [
    "function esc(s)",
    "esc(d.changelog)",
    "json_escape(s_ota_check_changelog",
    "changelog\":\"%s\"",
]

MORE_PROFILE_VALIDATION_MARKERS = [
    "pfChk",
    "profile-weight",
    "profile-ftp",
    "profile-thr-hr",
    "体重",
    "FTP",
    "阈值心率",
    "需在 ",
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


def check_markers(src: str, html: str, markers: list, title: str):
    print(f"{TAG} {title}")
    for marker in markers:
        check(marker in src or marker in html,
              f"回归标记 {marker!r} 缺失")


def main():
    parser = argparse.ArgumentParser(description="WiFi 门户静态结构检查 (TC-3d)")
    parser.add_argument("--file", default=str(DEFAULT_PORTAL_C),
                        help="待检查的 C 源文件（默认 main/wifi_portal.c）")
    args = parser.parse_args()

    portal_c = Path(args.file)
    if not portal_c.is_absolute():
        portal_c = ROOT / portal_c

    if not portal_c.is_file():
        print(f"{TAG} FAIL: 文件不存在 {portal_c}", file=sys.stderr)
        return 1

    src = portal_c.read_text(encoding="utf-8")
    html = extract_c_strings(src)

    print(f"{TAG} 检查 {portal_c.name}: 拼接 HTML 长度={len(html)} 字节")

    # 1. 路由表完整性
    print(f"\n{TAG} [1/7] 路由表完整性")
    for route in REQUIRED_ROUTES:
        # 路由通常作为字符串字面量出现，如 "/save"
        # 部分路由可能带 query，匹配路径前缀即可
        pat = '"' + route + '"'
        check(pat in src or route in html,
              f"路由 {route} 缺失")

    # 2. HTML 表单字段
    print(f"{TAG} [2/7] HTML 表单字段")
    for marker in REQUIRED_FORM_MARKERS:
        check(marker in src or marker in html,
              f"表单标记 {marker!r} 缺失")

    # 3. HTML 基础结构
    print(f"{TAG} [3/7] HTML 基础结构")
    for tag in REQUIRED_HTML_TAGS:
        check(tag in html,
              f"HTML 标签 {tag!r} 缺失")

    # <form> 必须有 action
    form_matches = re.findall(r'<form[^>]*>', html, re.IGNORECASE)
    check(len(form_matches) >= 1, "至少存在一个 <form>")
    for i, fm in enumerate(form_matches, 1):
        has_action = "action" in fm.lower()
        check(has_action, f"<form>#{i} 缺少 action 属性: {fm!r}")

    check_markers(src, html, SAVE_FEEDBACK_MARKERS,
                  f"[4/7] 首页保存成功强反馈")
    check_markers(src, html, OTA_UPGRADE_STATUS_MARKERS,
                  f"[5/7] OTA 升级进度端点")
    check_markers(src, html, OTA_XSS_MARKERS,
                  f"[6/7] OTA JSON/XSS 防回退")
    check_markers(src, html, MORE_PROFILE_VALIDATION_MARKERS,
                  f"[7/7] /more 个人信息前端校验")

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
