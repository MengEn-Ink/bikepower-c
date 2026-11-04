#!/usr/bin/env python3
"""使用生产页面和 mock 接口验证手机/桌面桥接与保存失败路径。"""
import json
import argparse
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--screenshot-dir", default="")
    args = parser.parse_args()
    screenshots = Path(args.screenshot_dir) if args.screenshot_dir else None
    if screenshots:
        screenshots.mkdir(parents=True, exist_ok=True)
    rows = []
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for width in (320, 390, 1440):
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            proc = subprocess.Popen([sys.executable, str(ROOT / "tests/mock_server.py"), "--port", str(port)],
                                    stdout=subprocess.DEVNULL)
            url = f"http://127.0.0.1:{port}"
            try:
                for _ in range(50):
                    try:
                        urllib.request.urlopen(url + "/api/state", timeout=0.2).close()
                        break
                    except OSError:
                        time.sleep(0.05)
                page = browser.new_page(viewport={"width": width, "height": 844})
                errors = []
                page.on("pageerror", lambda e: errors.append(str(e)))
                page.goto(url)
                expect(page.locator("#liveSubtitle")).to_contain_text("App 连接")
                expect(page.locator("#saveBtn")).to_be_disabled()
                assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
                assert page.locator("button:visible").evaluate_all("els => els.every(e => getComputedStyle(e).textAlign === 'center')")
                if screenshots:
                    page.screenshot(path=str(screenshots / f"core-default-{width}.png"), full_page=True)
                page.locator("#simPower").fill("310")
                expect(page.locator("#heroMetricValue")).to_have_text("220")
                page.locator("#saveBtn").click()
                expect(page.locator("#heroMetricValue")).to_have_text("310")
                page.locator("#simPower").fill("315")
                page.request.post(url + "/api/mock/fault", data={"fail_save": True})
                page.locator("#saveBtn").click()
                expect(page.locator("#saveState")).to_contain_text("未保存")
                expect(page.locator("#simPower")).to_have_value("315")
                expect(page.locator("#heroMetricValue")).to_have_text("310")
                page.locator("#saveBtn").click()
                expect(page.locator("#heroMetricValue")).to_have_text("315")
                page.locator("[data-source=bridge]").click()
                expect(page.locator("#bridgeEditor")).to_be_hidden()
                expect(page.locator("#saveBtn")).to_be_disabled()
                page.locator("#bridgeStartScanBtn").click()
                page.locator("#scanResults button").first.click()
                expect(page.locator("#scanModal")).to_be_hidden(timeout=10000)
                expect(page.locator("#bridgeEditor")).to_be_visible()
                assert page.locator("#heartToggle").get_attribute("aria-checked") == "false"
                expect(page.locator("#heroMetricValue")).to_have_text("315")
                page.locator("[data-calibration=watts]").click()
                page.locator("#calibrationValue").fill("12")
                expect(page.locator("#calibrationPreview")).to_have_text("238 W → 250 W")
                page.locator("#copyIdentity").check()
                page.locator("#identityNameSource").select_option("custom")
                page.locator("#customIdentityName").fill("我的功率计")
                page.locator("#saveBtn").click()
                expect(page.locator("#headerDeviceName")).to_have_text("我的功率计")
                saved = page.request.get(url + "/api/state").json()
                assert saved["identity"]["manufacturer"] == "Magene"
                assert saved["identity"]["model"] == "P505"
                expect(page.locator("#heroMetricValue")).to_have_text("250")
                expect(page.locator("#heartLiveValue")).to_have_text("—")
                if screenshots:
                    page.screenshot(path=str(screenshots / f"core-bridge-{width}.png"), full_page=True)
                page.request.post(url + "/api/mock/fault", data={"fault": "zero"})
                expect(page.locator("#heroMetricValue")).to_have_text("0")
                page.request.post(url + "/api/mock/fault", data={"fault": "stale"})
                expect(page.locator("#runtimeAlertTitle")).to_contain_text("归零")
                page.request.post(url + "/api/mock/fault", data={})
                expect(page.locator("#heroMetricValue")).to_have_text("250")
                page.locator("#heartToggle").click()
                page.locator("#saveBtn").click()
                expect(page.locator("#heartLiveValue")).to_have_text("145")
                page.locator("[data-source=simulation]").click()
                page.locator("#saveBtn").click()
                expect(page.locator("#heroMetricValue")).to_have_text("315")
                page.locator("[data-sport=running]").click()
                page.locator("#paceMinutes").fill("5")
                page.locator("#paceSeconds").fill("0")
                page.locator("#saveBtn").click()
                expect(page.locator("#heroMetricValue")).to_have_text("12.00")
                page.locator("#runCadence").focus()
                expect(page.locator("#liveTitle")).to_have_text("跑步广播")
                assert page.locator("#runCadence").evaluate("el => el === document.activeElement")
                page.emulate_media(reduced_motion="reduce")
                assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
                assert not errors, errors
                page.locator("#renewBtn").click()
                expect(page.locator("#toast")).to_contain_text("续期")
                page.locator("#closeWifiBtn").click()
                page.locator("#confirmCloseBtn").click()
                expect(page.locator("#wifiClosed")).to_be_visible()
                rows.append({"width": width, "save_failure": "PASS", "bridge": "PASS", "running": "PASS"})
                page.close()
            finally:
                proc.terminate()
                proc.wait(timeout=5)
        browser.close()
    print(json.dumps(rows, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
