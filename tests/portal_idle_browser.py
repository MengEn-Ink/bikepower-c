#!/usr/bin/env python3
"""真实Chrome执行生产配置页，虚拟时钟覆盖300秒闲置与真实用户事件。"""
import copy
import importlib.util
import json
import os
import sys
from pathlib import Path
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from inline_static import inline_html, minify_html, minify_css, minify_js
spec = importlib.util.spec_from_file_location("portal_mock", ROOT / "tests/mock_server.py")
mock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mock)


def main():
    html = inline_html(minify_html((ROOT / "main/web/index.html").read_text(encoding="utf-8")),
                       minify_css((ROOT / "main/web/app.css").read_text(encoding="utf-8")),
                       minify_js((ROOT / "main/web/app.js").read_text(encoding="utf-8")))
    with sync_playwright() as p:
        browser = p.chromium.launch(executable_path=os.environ.get("SYSTEM_CHROME") or None)
        try:
            for width in (320,390,1440):
                page = browser.new_page(viewport={"width":width,"height":844},has_touch=width<500)
                page.clock.install()
                errors=[]
                page.on("pageerror",lambda e:errors.append(str(e)))
                model={"now":0,"deadline":300,"closing":False,"renews":0,"posts":0,"fault":False,
                       "offline":False,"hold_runtime":False,"held":None}
                fixture=mock.state()
                def route(r):
                    path=r.request.url.split("/api/")[-1]
                    if "/api/" not in r.request.url:
                        return r.fulfill(body=html,content_type="text/html")
                    if model["offline"]:
                        return r.fulfill(status=503,body="offline")
                    if model["now"]>=model["deadline"] and not model["closing"]:
                        model["closing"]=True
                        model["deadline"]=model["now"]+5
                    remaining=max(0,model["deadline"]-model["now"])
                    if path=="renew":
                        model["renews"]+=1
                        if model["fault"]:
                            return r.fulfill(status=503,body="offline")
                        if model["closing"]:
                            return r.fulfill(json={"ok":False,"closing":True,"error":"配网已结束"})
                        model["deadline"]=model["now"]+300
                        return r.fulfill(body="300",content_type="text/plain")
                    if path=="state":
                        data=copy.deepcopy(fixture)
                        data.update(remaining_sec=remaining,closing=model["closing"],idle_timeout_sec=300)
                        return r.fulfill(json=data)
                    if path=="runtime":
                        data=mock.runtime()
                        data.update(remaining_sec=remaining,closing=model["closing"])
                        if model["hold_runtime"]:
                            model["held"]=(r,data)
                            return
                        return r.fulfill(json=data)
                    if path=="mem_stats":
                        return r.fulfill(json={"free":120000,"min_free":98000})
                    if path=="close-wifi":
                        model["posts"]+=1
                        model["closing"]=True
                        model["deadline"]=model["now"]+5
                        return r.fulfill(json={"ok":True,"transaction":mock.STATE["transaction"]})
                    if path=="config":
                        model["posts"]+=1
                    return r.fulfill(json={"ok":True,"scanning":False,"devices":[]})
                page.route("**/*",route)
                def open_page():
                    page.goto("http://portal.local/")
                    expect(page.locator("#heroMetricValue")).to_have_text("220")
                def advance(now,seconds):
                    model["now"]=now
                    page.clock.fast_forward(seconds*1000)
                    page.wait_for_timeout(80)
                open_page()
                advance(269,269)
                expect(page.locator("#sessionWarning")).to_be_hidden()
                assert model["renews"]==0,"轮询不得续期"
                advance(270,1)
                expect(page.locator("#sessionWarning")).to_be_visible()
                expect(page.locator("#warningSeconds")).to_have_text("30")
                page.locator("#warningRenewBtn").click()
                expect(page.locator("#wifiTimer")).to_have_text("05:00")
                assert model["renews"]==1,"继续配置只能续期一次"
                expect(page.locator("#sessionWarning")).to_be_hidden()
                advance(280,10)
                page.locator("#simPower").fill("310")
                expect(page.locator("#wifiTimer")).to_have_text("05:00")
                assert model["posts"]==0,"编辑不得自动保存"
                prior=model["renews"]
                for _ in range(10):
                    page.mouse.wheel(0,10)
                page.clock.fast_forward(1100)
                page.wait_for_timeout(80)
                assert 1<=model["renews"]-prior<=2,"连续滚动应合并请求"
                prior=model["renews"]
                page.evaluate("document.dispatchEvent(new Event('input',{bubbles:true})); window.scrollTo(0,0)")
                page.mouse.move(100,100)
                page.clock.fast_forward(1100)
                assert model["renews"]==prior,"合成事件与鼠标移动不得续期"
                model["fault"]=True
                advance(300,18)
                page.locator("#simPower").fill("311")
                expect(page.locator("#sessionRenewError")).to_be_visible()
                expect(page.locator("#wifiTimer")).not_to_have_text("05:00")
                model["fault"]=False
                model["deadline"]=model["now"]+30
                page.clock.fast_forward(1000)
                expect(page.locator("#sessionWarning")).to_be_visible()
                expect(page.locator("#warningDirty")).to_be_visible()
                advance(model["deadline"],30)
                expect(page.locator("#wifiClosed")).to_be_visible()
                expect(page.locator("#wifiClosed")).to_have_attribute("data-confirmed","true")
                expect(page.locator("#wifiClosedText")).to_contain_text("设备已接受关闭")
                assert model["posts"]==0,"超时不得自动提交草稿"
                prior=model["renews"]
                page.locator("#wifiClosedTitle").click()
                page.clock.fast_forward(10000)
                assert model["renews"]==prior,"结束后不得再续期"
                model.update(now=0,deadline=300,closing=False,offline=False)
                open_page()
                model["hold_runtime"]=True
                advance(200,1)
                assert model["held"],"应捕获在途旧轮询"
                model["now"]=201
                page.locator("#simPower").fill("315")
                expect(page.locator("#wifiTimer")).to_have_text("05:00")
                old_route,old_data=model["held"]
                model["hold_runtime"]=False
                old_route.fulfill(json=old_data)
                expect(page.locator("#wifiTimer")).to_have_text("05:00")
                prior=model["renews"]
                page.evaluate("Object.defineProperty(document,'hidden',{configurable:true,value:true}); document.dispatchEvent(new Event('visibilitychange'))")
                page.clock.fast_forward(5000)
                page.evaluate("Object.defineProperty(document,'hidden',{configurable:true,value:false}); document.dispatchEvent(new Event('visibilitychange'))")
                page.wait_for_timeout(80)
                assert model["renews"]==prior,"仅恢复可见不续期"
                page.clock.fast_forward(1100)
                page.keyboard.press("PageDown")
                page.wait_for_timeout(80)
                assert model["renews"]>prior,"键盘阅读应续期"
                prior=model["renews"]
                if width<500:
                    page.clock.fast_forward(1100)
                    page.touchscreen.tap(150,250)
                    page.wait_for_timeout(80)
                    assert model["renews"]>prior,"真实触摸应续期"
                model.update(now=0,deadline=300,closing=False,offline=False)
                open_page()
                advance(295,1)
                model["offline"]=True
                advance(300,300)
                expect(page.locator("#wifiClosed")).to_be_visible()
                expect(page.locator("#wifiClosed")).to_have_attribute("data-confirmed","false")
                expect(page.locator("#sessionRetryBtn")).to_be_visible()
                expect(page.locator("#wifiClosedState")).to_have_text("设备状态待确认")
                model["offline"]=False
                page.locator("#sessionRetryBtn").click()
                expect(page.locator("#wifiClosed")).to_have_attribute("data-confirmed","true")
                assert not errors,errors
                print(json.dumps({"width":width,"idle_300s":"PASS","warning_30s":"PASS","activity_and_scroll_merge":"PASS","no_auto_save":"PASS","renew_failure":"PASS","terminal":"PASS","late_poll":"PASS","visibility_keyboard_touch":"PASS","offline_terminal_retry":"PASS"},ensure_ascii=False))
                page.close()
        finally:
            browser.close()

if __name__=="__main__":
    main()
