#!/usr/bin/env python3
"""TC-3d 核心门户结构、路由、体积和设备资源边界。"""
import importlib.util
import re
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class Markup(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = []
        self.external = []

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if values.get("id"):
            self.ids.append(values["id"])
        if tag in ("img", "script", "link") and (values.get("src") or values.get("href")):
            self.external.append(values)


def main():
    html, css, js = [(ROOT / "main/web" / name).read_text() for name in ("index.html", "app.css", "app.js")]
    portal = (ROOT / "main/wifi_portal.c").read_text()
    parser = Markup()
    parser.feed(html)
    errors = []
    def check(ok, message):
        if not ok:
            errors.append(message)
    check(len(parser.ids) == len(set(parser.ids)), "HTML id 唯一")
    check(not parser.external, "设备页不加载外部资源")
    check(all(x in html for x in ("<html", "<head", "<body", "viewport", "<!-- INLINE_CSS -->", "<!-- INLINE_JS -->")), "内联入口结构")
    check(all(x in parser.ids for x in (
        "simulationEditor", "bridgeIntro", "bridgeEditor", "runningEditor",
        "sharedHeart", "scanResults", "configuredSource", "actualSource",
        "bridgeDeviceList", "bridgeDeviceTemplate", "nextBridgeAttempt",
        "retryBridgeBtn", "sharedCalibrationHint", "fallbackSimulationSummary",
        "replaceBridgeConfirm")), "核心渐进界面")
    check("data-runtime" not in html and "restoreDemoBtn" not in html, "禁止原型模拟操作进入设备页")
    check("html[data-theme=dark]" in css and "themeToggle" in parser.ids
          and "bikepower.portal.theme.v1" in js and "prefers-color-scheme" in js,
          "双主题与浏览器持久化")
    check("prefers-reduced-motion" in css and "text-align: center" in css, "动画与居中按钮")
    check("data-sim-mode=\"custom\"" not in html, "骑行自定义收敛于机器人档")
    for api in ("state", "runtime", "config", "close-wifi", "bridge/scan", "bridge/control", "renew", "time"):
        check(f'"/api/{api}"' in portal, f"设备路由 {api}")
    check("/api/device-templates" not in portal and "/api/ble-scan" not in portal, "移除旧模板与分析路由")
    check("revision:state.revision" in js and "accepted.transaction" in js, "保存使用 revision 与结果事务")
    check("state.draft.heart.enabled = false" in js and "bridgeReady()" in js, "测试后关闭心率与保存门槛")
    check("bridge_devices" in js and "bridge_runtime" in js,
          "三设备配置与实际来源运行状态")
    check("textContent" in js and ".innerHTML" not in js, "设备信息使用文本输出")
    spec = importlib.util.spec_from_file_location("inline_static", ROOT / "scripts/inline_static.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    parts = [module.minify_html(html), module.minify_css(css), module.minify_js(js)]
    for value, limit, name in zip(parts, (21, 24, 65), ("HTML", "CSS", "JS")):
        size = len(value.encode())
        check(size <= limit * 1024, f"{name} {size} bytes 超过 {limit} KiB")
    size = len(module.inline_html(*parts).encode())
    check(size <= 110 * 1024, f"内联总量 {size} 超过 110 KiB")
    for error in errors:
        print("[TC-3d] FAIL", error)
    print(f"[TC-3d] {'FAIL' if errors else 'PASS'}: 内联 {size} bytes，{len(parser.ids)} 个唯一控件")
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
