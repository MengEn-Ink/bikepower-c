#!/usr/bin/env python3
"""压缩生产门户 DOM 与 HTTP 故障注入；服务端为本地 mock。"""
import json
import os
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[1]


def main():
    with sync_playwright() as p:
        chrome_kw = {"executable_path": os.environ["SYSTEM_CHROME"]} if os.environ.get("SYSTEM_CHROME") else {}
        browser = p.chromium.launch(**chrome_kw)
        try:
            for width in (320, 390, 1440):
                with socket.socket() as sock:
                    sock.bind(("127.0.0.1", 0))
                    port = sock.getsockname()[1]
                proc = subprocess.Popen([sys.executable, str(ROOT / "tests/mock_server.py"),
                                         "--bind", "127.0.0.1", "--port", str(port)],
                                        stdout=subprocess.DEVNULL)
                try:
                    url = f"http://127.0.0.1:{port}"
                    for _ in range(50):
                        try:
                            urllib.request.urlopen(url + "/api/state", timeout=.2).close()
                            break
                        except OSError:
                            time.sleep(.05)
                    page = browser.new_page(viewport={"width": width, "height": 844})
                    errors = []
                    page.on("pageerror", lambda e: errors.append(str(e)))
                    faults = {}
                    posts = []

                    def route(request):
                        path = request.request.url.split("/api/")[-1]
                        fault = faults.get(path)
                        if path == "config" and request.request.method == "POST":
                            posts.append(request.request.post_data_json)
                        if fault == "503":
                            request.fulfill(status=503, body="unavailable")
                        elif fault == "json":
                            request.fulfill(content_type="application/json", body="{broken")
                        elif fault == "reject":
                            request.fulfill(json={"ok": False, "error": "设备忙，请稍后重试"})
                        elif fault == "lost":
                            request.fetch()
                            faults.pop(path)
                            request.abort()
                        elif fault == "pending":
                            data = request.fetch().json()
                            data["pending"] = True
                            data["transaction"] += 1
                            request.fulfill(json=data)
                        else:
                            request.continue_()

                    page.route("**/api/**", route)
                    def instrument_portal(request):
                        response = request.fetch()
                        hook = """
window.testCloseRace = async function () {
  var snapshot = await api("state"), original = api, release;
  api = function () { return new Promise(function (resolve) { release = resolve; }); };
  endSession(false,0);
  var pending = checkSession();
  api = original;
  endSession(true,1);
  release(snapshot);
  await pending;
  return {closed:state.closed,confirmed:state.closeConfirmed,
    hidden:$("wifiClosed").hidden,inert:document.querySelector(".page").inert};
};
"""
                        html = response.text()
                        assert html.count("}());") == 1
                        request.fulfill(response=response, body=html.replace("}());", hook + "}());"))

                    page.route(url + "/", instrument_portal)
                    for fault in ("503", "json"):
                        faults["state"] = fault
                        page.goto(url)
                        expect(page.locator("#saveState")).to_contain_text("请刷新")
                        expect(page.locator("#saveBtn")).to_be_disabled()
                    faults.clear()
                    page.reload()
                    expect(page.locator("#heroMetricValue")).to_have_text("220")
                    page.locator("#simPower").fill("310")
                    faults["runtime"] = "503"
                    expect(page.locator("#heroMetricValue")).to_have_text("—")
                    expect(page.locator("#simPower")).to_have_value("310")
                    faults.clear()
                    expect(page.locator("#heroMetricValue")).to_have_text("220")
                    external = page.request.get(url + "/api/state").json()
                    body = dict(external["config"], revision=external["revision"], power=240)
                    assert page.request.post(url + "/api/config", data=body).json()["ok"]
                    expect(page.locator("#heroMetricValue")).to_have_text("240")
                    expect(page.locator("#simPower")).to_have_value("310")
                    expect(page.locator("#saveState")).to_contain_text("草稿")
                    faults["config"] = "503"
                    page.locator("#saveBtn").click()
                    expect(page.locator("#toast")).to_contain_text("503")
                    expect(page.locator("#saveBtn")).to_be_enabled()
                    expect(page.locator("#simPower")).to_have_value("310")
                    faults["config"] = "reject"
                    page.locator("#saveBtn").click()
                    expect(page.locator("#toast")).to_contain_text("设备忙")
                    expect(page.locator("#toast")).not_to_contain_text("核实")
                    expect(page.locator("#heroMetricValue")).to_have_text("240")
                    expect(page.locator("#simPower")).to_have_value("310")
                    expect(page.locator("#saveBtn")).to_be_enabled()
                    faults.clear()
                    page.locator("#saveBtn").click()
                    expect(page.locator("#heroMetricValue")).to_have_text("310")
                    page.locator("#simPower").fill("315")
                    faults["config"] = "lost"
                    count = len(posts)
                    page.locator("#saveBtn").click()
                    expect(page.locator("#toast")).to_contain_text("核实")
                    expect(page.locator("#heroMetricValue")).to_have_text("315")
                    expect(page.locator("#simPower")).to_have_value("315")
                    assert len(posts) == count + 1, "响应丢失不得自动重复提交"
                    page.locator("#simPower").fill("320")
                    count = len(posts)
                    page.locator("#saveBtn").evaluate("el => { el.click(); el.click(); el.click(); }")
                    expect(page.locator("#heroMetricValue")).to_have_text("320")
                    assert len(posts) == count + 1, "快速重复点击只能提交一次"
                    page.locator("#simPower").fill("325")
                    faults["runtime"] = "pending"
                    count = len(posts)
                    page.locator("#saveBtn").click()
                    expect(page.locator("#toast")).to_contain_text("核实", timeout=10000)
                    expect(page.locator("#saveState")).to_contain_text("核实")
                    expect(page.locator("#simPower")).to_have_value("325")
                    assert len(posts) == count + 1, "等待异事务超时不得重复提交"
                    faults.clear()
                    expect(page.locator("#heroMetricValue")).to_have_text("325")
                    page.locator("#sourceOptions summary").click()
                    page.locator("[data-source=bridge]").click()
                    faults["bridge/scan"] = "503"
                    page.locator("#bridgeStartScanBtn").click()
                    expect(page.locator("#scanStatus")).to_contain_text("503")
                    faults.clear()
                    page.keyboard.press("Escape")
                    expect(page.locator("#heroMetricValue")).to_have_text("325")
                    page.locator("[data-source=simulation]").click()
                    faults["close-wifi"] = "503"
                    page.locator("#closeWifiBtn").click()
                    page.locator("#confirmCloseBtn").click()
                    expect(page.locator("#toast")).to_contain_text("503")
                    expect(page.locator("#wifiClosed")).to_be_hidden()
                    expect(page.locator("#confirmCloseBtn")).to_be_enabled()
                    faults.clear()
                    race = page.evaluate("testCloseRace()")
                    assert race == {"closed": True, "confirmed": True, "hidden": False, "inert": True}, race
                    page.reload()
                    expect(page.locator("#heroMetricValue")).to_have_text("325")
                    page.locator("#closeWifiBtn").click()
                    page.locator("#confirmCloseBtn").click()
                    expect(page.locator("#wifiClosed")).to_be_visible()
                    assert not errors, errors
                    print(json.dumps({"width": width, "initial_read": "PASS", "poll_recovery": "PASS",
                                      "external_revision": "PASS", "save_retry_lost_ack_duplicate": "PASS",
                                      "scan_cancel_close_retry": "PASS"}, ensure_ascii=False))
                    page.close()
                finally:
                    proc.terminate()
                    proc.wait(timeout=5)
        finally:
            browser.close()


if __name__ == "__main__":
    main()
