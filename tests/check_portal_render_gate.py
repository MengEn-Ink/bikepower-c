#!/usr/bin/env python3
"""检查生产页面浏览器门禁接线与故障路径覆盖。"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    renderer = (ROOT / "tests/check_portal_render.py").read_text()
    browser = (ROOT / "tests/portal_bridge_browser.py").read_text()
    quality = (ROOT / "scripts/tools/quality_gate.py").read_text()
    web_checks = (ROOT / "tests/web-flash/package.json").read_text()
    inputs = (ROOT / "tests/test_portal_inputs_browser.cjs").read_text()
    required = [("portal_bridge_browser.py" in renderer, "渲染执行完整交互"),
                ("--screenshot-dir" in renderer and "page.screenshot" in browser, "截图输出"),
                ("chromium.launch" in browser and "tests/mock_server.py" in browser, "真实浏览器与接口替身"),
                ("fail_save" in browser and '"stale"' in browser, "保存失败与过期路径"),
                ("320, 390, 1440" in browser, "手机与桌面宽度"),
                ("tests/check_portal_render.py" in quality, "质量门禁聚合"),
                ("test_portal_theme_browser.cjs" in web_checks, "双主题持久化浏览器门禁"),
                ("test_portal_intensity_browser.cjs" in web_checks, "骑行强度浏览器门禁"),
                ("test_portal_inputs_browser.cjs" in web_checks, "组合输入浏览器门禁"),
                ("test_portal_transport_browser.cjs" in web_checks and "test_captive_redirect.py" in quality,
                 "固定热点地址和网络恢复门禁"),
                ("test_portal_recovery_browser.cjs" in web_checks, "草稿恢复与隐藏错误浏览器门禁"),
                ("group.room-group.used" in inputs and "group.widths" in inputs
                 and "切换整体居中" in inputs, "按钮组占满、等宽与居中几何断言"),
                (all(x in inputs for x in ("checkFloat('hrFloat'", "checkFloat('runPaceFloat'",
                                          "checkFloat('runCadenceFloat'", "配网结束禁止编辑浮动滑杆")),
                 "新增浮动同步与会话结束门禁"),
                ("共用同一个组合输入栏" in inputs and "滑杆占满整个组合栏" in inputs
                 and "checkFloatRow('paceSeconds'" in inputs, "五项统一同栏与配速分秒门禁"),
                ("核心配置应在手机首屏可见" in inputs and "渐进展开不得自动保存" in inputs
                 and "打开已有配置不得重置或自动提交" in inputs,
                 "首次核心配置、展开隔离与已有配置恢复门禁")]
    for ok, label in required:
        print("[portal-render-gate-check]", "PASS" if ok else "FAIL", label)
    return any(not ok for ok, _ in required)


if __name__ == "__main__":
    raise SystemExit(main())
