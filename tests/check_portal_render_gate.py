#!/usr/bin/env python3
"""检查生产页面浏览器门禁接线与故障路径覆盖。"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    renderer = (ROOT / "tests/check_portal_render.py").read_text()
    browser = (ROOT / "tests/portal_bridge_browser.py").read_text()
    quality = (ROOT / "scripts/tools/quality_gate.py").read_text()
    required = [("portal_bridge_browser.py" in renderer, "渲染执行完整交互"),
                ("--screenshot-dir" in renderer and "page.screenshot" in browser, "截图输出"),
                ("chromium.launch" in browser and "tests/mock_server.py" in browser, "真实浏览器与接口替身"),
                ("fail_save" in browser and '"stale"' in browser, "保存失败与过期路径"),
                ("320, 390, 1440" in browser, "手机与桌面宽度"),
                ("tests/check_portal_render.py" in quality, "质量门禁聚合")]
    for ok, label in required:
        print("[portal-render-gate-check]", "PASS" if ok else "FAIL", label)
    return any(not ok for ok, _ in required)


if __name__ == "__main__":
    raise SystemExit(main())
