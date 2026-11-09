#!/usr/bin/env python3
"""使用生产页面和 mock 接口验证手机/桌面桥接与保存失败路径。"""
import json
import argparse
import os
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
    parser.add_argument("--color-scheme", choices=("light", "dark"), default="light")
    args = parser.parse_args()
    screenshots = Path(args.screenshot_dir) if args.screenshot_dir else None
    if screenshots:
        screenshots.mkdir(parents=True, exist_ok=True)
    rows = []
    with sync_playwright() as p:
        chrome_kw = {"executable_path": os.environ["SYSTEM_CHROME"]} if os.environ.get("SYSTEM_CHROME") else {}
        browser = p.chromium.launch(**chrome_kw)
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
                page = browser.new_page(viewport={"width": width, "height": 844}, color_scheme=args.color_scheme)
                errors = []
                page.on("pageerror", lambda e: errors.append(str(e)))
                page.goto(url)
                expect(page.locator("#liveSubtitle")).to_contain_text("App 连接")
                expect(page.locator("#schemaVersion")).to_have_text("schema v14")
                expect(page.locator("#heapValue")).to_contain_text("当前 117.2 KiB")
                expect(page.locator("#heapValue")).to_contain_text("最低 95.7 KiB")
                expect(page.locator("#sharedCalibrationHint")).to_have_text(
                    "这组校正适用于全部已保存功率计")
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
                expect(page.locator("#configuredSource")).to_have_text("真实功率计")
                expect(page.locator("#actualSource")).to_contain_text("P505")
                expect(page.locator("#bridgeDeviceList [data-device]")).to_have_count(1)
                saved = page.request.get(url + "/api/state").json()
                assert saved["identity"]["manufacturer"] == "Magene"
                assert saved["identity"]["model"] == "P505"
                expect(page.locator("#heroMetricValue")).to_have_text("250")
                expect(page.locator("#heartLiveValue")).to_have_text("—")
                page.locator("[data-calibration=percent]").click()
                expect(page.locator("#calibrationValue")).to_have_attribute("max", "200")
                expect(page.locator("#calibrationRange")).to_have_attribute("max", "200")
                page.locator("#calibrationValue").fill("200")
                expect(page.locator("#calibrationPreview")).to_have_text("238 W → 714 W")
                page.locator("#saveBtn").click()
                expect(page.locator("#heroMetricValue")).to_have_text("714")
                assert page.request.get(url + "/api/state").json()["config"]["bridge_percent_x10"] == 3000
                page.reload()
                expect(page.locator("#calibrationValue")).to_have_value("200")
                expect(page.locator("#actualCalibration")).to_have_text("+200%")
                page.locator("#calibrationValue").fill("200.1")
                expect(page.locator("#calibrationValue")).to_have_value("200")
                page.request.post(url + "/api/mock/fault", data={"fault": "zero"})
                expect(page.locator("#heroMetricValue")).to_have_text("0")
                page.request.post(url + "/api/mock/fault", data={})
                expect(page.locator("#heroMetricValue")).to_have_text("714")
                page.locator("[data-calibration=watts]").click()
                page.locator("#saveBtn").click()
                expect(page.locator("#heroMetricValue")).to_have_text("250")
                if screenshots:
                    page.screenshot(path=str(screenshots / f"core-bridge-{width}.png"), full_page=True)
                for scan_index, expected_count in ((1, 2), (2, 3)):
                    page.locator("#scanBtn").click()
                    choice = page.locator(
                        f'#scanResults [data-scan-index="{scan_index}"] button')
                    expect(choice).to_be_enabled()
                    choice.click()
                    expect(page.locator("#scanModal")).to_be_hidden(timeout=10000)
                    expect(page.locator("#bridgeDeviceList [data-device]")).to_have_count(
                        expected_count)
                    page.locator("#saveBtn").click()
                    expect(page.locator("#saveState")).to_contain_text("已应用")
                expect(page.locator("#bridgeDeviceList [data-device]")).to_have_count(3)
                page.locator("#scanBtn").click()
                fourth = page.locator('#scanResults [data-scan-index="3"] button')
                expect(fourth).to_be_enabled()
                fourth.click()
                expect(page.locator("#replaceBridgeConfirm")).to_be_visible(timeout=10000)
                page.locator("#replaceBridgeSlot").select_option("1")
                page.locator("#confirmBridgeReplaceBtn").click()
                expect(page.locator("#scanModal")).to_be_hidden()
                expect(page.locator("#bridgeDeviceList [data-device]")).to_have_count(3)
                page.locator("#saveBtn").click()
                expect(page.locator("#saveState")).to_contain_text("已应用")
                page.locator("#bridgeDeviceList [data-device]").nth(2).locator(
                    '[data-action="prefer"]').click()
                expect(page.locator("#bridgeDeviceList [data-device]").first).to_contain_text(
                    "Stages")
                page.locator("#saveBtn").click()
                expect(page.locator("#saveState")).to_contain_text("已应用")
                if screenshots:
                    page.screenshot(
                        path=str(screenshots / f"core-bridge-devices-{width}.png"),
                        full_page=True)
                page.once("dialog", lambda dialog: dialog.accept())
                page.locator("#bridgeDeviceList [data-device]").first.locator(
                    '[data-action="delete"]').click()
                expect(page.locator("#bridgeDeviceList [data-device]")).to_have_count(2)
                page.locator("#saveBtn").click()
                expect(page.locator("#saveState")).to_contain_text("已应用")
                page.request.post(url + "/api/mock/fault", data={"fault": "all_missing"})
                expect(page.locator("#actualSource")).to_contain_text("模拟降级")
                expect(page.locator("#nextBridgeAttempt")).to_contain_text("42 秒")
                expect(page.locator("#fallbackSimulationSummary")).to_contain_text("315 W")
                page.locator("#calibrationValue").fill("20")
                page.locator("#saveBtn").click()
                expect(page.locator("#saveState")).to_contain_text("已应用")
                expect(page.locator("#heroMetricValue")).to_have_text("315")
                if screenshots:
                    page.screenshot(
                        path=str(screenshots / f"core-bridge-fallback-{width}.png"),
                        full_page=True)
                page.locator("#retryBridgeBtn").click()
                expect(page.locator("#actualSource")).to_contain_text("快速恢复")
                page.request.post(url + "/api/mock/fault", data={"fault": "backup_live"})
                expect(page.locator("#actualSource")).to_contain_text("真实功率计")
                assert page.request.get(url + "/api/state").json()["identity"]["serial"] == "MOCK-4A29"
                page.request.post(url + "/api/mock/fault", data={"fault": "zero"})
                expect(page.locator("#heroMetricValue")).to_have_text("0")
                page.request.post(url + "/api/mock/fault", data={"fault": "stale"})
                expect(page.locator("#runtimeAlertTitle")).to_contain_text("模拟输出")
                page.request.post(url + "/api/mock/fault", data={})
                expect(page.locator("#heroMetricValue")).to_have_text("258")
                page.locator("#heartToggle").click()
                page.locator("#saveBtn").click()
                expect(page.locator("#heartLiveValue")).to_have_text("145")
                page.locator("[data-source=simulation]").click()
                page.locator("#saveBtn").click()
                expect(page.locator("#heroMetricValue")).to_have_text("315")
                page.locator("[data-sport=running]").click()
                expect(page.locator(".live-pane")).to_be_hidden()
                assert "is-mode-pending" in page.locator(".workspace").get_attribute("class")
                expect(page.locator("#modeSwitchNotice")).to_be_visible()
                expect(page.locator("#modeSwitchNotice")).to_contain_text("跑步模式尚未应用")
                expect(page.locator("#saveBtn")).to_have_text("保存并切换到跑步")
                expect(page.locator("#heartIndex")).to_have_text("02")
                expect(page.locator("[data-sport=running]")).to_have_attribute("aria-pressed", "true")
                expect(page.locator("[data-sport=cycling]")).to_have_attribute("aria-pressed", "false")
                if width == 1440:
                    assert page.locator(".editor-pane").bounding_box()["width"] <= 760
                if screenshots:
                    page.screenshot(path=str(screenshots / f"core-running-pending-{width}.png"), full_page=True)
                page.locator("#paceMinutes").fill("5")
                page.locator("#paceSeconds").fill("0")
                page.request.post(url + "/api/mock/fault", data={"fail_save": True})
                page.locator("#saveBtn").click()
                expect(page.locator("#saveState")).to_contain_text("未保存")
                expect(page.locator(".live-pane")).to_be_hidden()
                expect(page.locator("#paceMinutes")).to_have_value("5")
                expect(page.locator("#saveBtn")).to_be_enabled()
                page.locator("#saveBtn").click()
                expect(page.locator(".live-pane")).to_be_visible()
                expect(page.locator("#heroMetricValue")).to_have_text("12.00")
                page.locator("#runCadence").focus()
                expect(page.locator("#liveTitle")).to_have_text("跑步广播")
                expect(page.locator("#liveStatus")).to_have_text("跑步广播中")
                expect(page.locator("#runtimeAlertTitle")).to_have_text("跑步模拟正在运行")
                expect(page.locator("#eventStateLabel")).to_have_text("步频状态")
                expect(page.locator("#appServices")).to_have_text("RSC / HRS / FTMS")
                expect(page.locator("#upstreamStateLabel")).to_have_text("会话来源")
                expect(page.locator("#reconnectState")).to_have_text("本机模拟")
                expect(page.locator("#runModeSummary")).to_have_text("固定配速")
                if screenshots:
                    page.screenshot(path=str(screenshots / f"core-running-applied-{width}.png"), full_page=True)
                assert page.locator("#runCadence").evaluate("el => el === document.activeElement")
                page.locator("[data-run-mode=pacer]").click()
                expect(page.locator("#runModeSummary")).to_have_text("配速档")
                page.locator("[data-run-mode=fixed]").click()
                page.locator("[data-sport=cycling]").click()
                expect(page.locator(".live-pane")).to_be_hidden()
                expect(page.locator("#modeSwitchNotice")).to_contain_text("骑行模式尚未应用")
                expect(page.locator("#saveBtn")).to_have_text("保存并切换到骑行")
                page.locator("[data-sport=running]").click()
                expect(page.locator(".live-pane")).to_be_visible()
                expect(page.locator("#modeSwitchNotice")).to_be_hidden()
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
