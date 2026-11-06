#!/usr/bin/env python3
"""将生产配置页与独立演示接口打包为离线手机屏幕。"""
import importlib.util
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def render():
    spec = importlib.util.spec_from_file_location("portal_demo_fixture", ROOT / "tests/mock_server.py")
    mock = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mock)
    fixture = mock.state()
    fixture["config"]["device_name"] = "BikePower"
    fixture["config"]["power"] = 200
    fixture["config"]["power_float"] = 0
    fixture["remaining_sec"] = 300
    page = (ROOT / "main/web/index.html").read_text()
    adapter = (ROOT / "tools/web/phone-demo-adapter.js").read_text()
    bootstrap = "window.PHONE_FIXTURE=" + json.dumps(
        {"state": fixture, "devices": mock.DEVICES, "identity": mock.IDENTITY},
        ensure_ascii=False,
    ) + ";"
    page = page.replace("<!-- INLINE_CSS -->", "<style>" + (ROOT / "main/web/app.css").read_text() + "</style>")
    page = page.replace("<!-- INLINE_JS -->", "<script>" + bootstrap + adapter + "</script><script>" +
                        (ROOT / "main/web/app.js").read_text() + "</script>")
    page = page.replace("<head>", '<head><meta http-equiv="Content-Security-Policy" '
                        'content="default-src \'none\'; script-src \'unsafe-inline\'; '
                        'style-src \'unsafe-inline\'; img-src data:; connect-src \'none\'; form-action \'none\'">')
    return "/* 生成文件：python3 scripts/tools/build_phone_demo.py */\nwindow.BikePowerPhoneSource=" + json.dumps(
        page, ensure_ascii=False
    ).replace("<", "\\u003c") + ";\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    target = ROOT / "tools/web/phone-portal-source.js"
    expected = render()
    if args.check:
        if not target.is_file() or target.read_text() != expected:
            raise SystemExit("手机演示来源已漂移：运行 python3 scripts/tools/build_phone_demo.py")
        print("手机演示来源与生产页面一致")
    else:
        target.write_text(expected)
        print("已同步真实配置页演示来源")
