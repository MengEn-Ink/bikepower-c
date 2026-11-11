#!/usr/bin/env python3
"""生产门户触屏布局回归：输入字号、模式切换、聚焦及动态视口。"""
import json,os,socket,subprocess,sys,time,urllib.request
from pathlib import Path
from playwright.sync_api import sync_playwright,expect
ROOT=Path(__file__).resolve().parents[1]
def check(page):
    data=page.evaluate('''() => ({width:innerWidth,scroll:document.documentElement.scrollWidth,
      scale:visualViewport.scale,textAdjust:getComputedStyle(document.documentElement).webkitTextSizeAdjust,
      small:[...document.querySelectorAll('input:not([type=checkbox]):not([type=range]),select,textarea')]
        .filter(e=>e.getClientRects().length&&parseFloat(getComputedStyle(e).fontSize)<16).map(e=>e.id),
      clipped:[...document.querySelectorAll('.editor-pane input,.editor-pane select,.action-bar button')]
        .filter(e=>e.getClientRects().length).filter(e=>{const r=e.getBoundingClientRect();return r.left<-.5||r.right>innerWidth+.5}).map(e=>e.id)})''')
    assert data['scroll']<=data['width'],data
    assert not data['small'] and not data['clipped'],data
    assert data['scale']==1,data
    return data

def main():
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    proc=subprocess.Popen([sys.executable,str(ROOT/'tests/mock_server.py'),'--port',str(port)],stdout=subprocess.DEVNULL)
    url=f'http://127.0.0.1:{port}'
    try:
        for _ in range(60):
            try:urllib.request.urlopen(url+'/api/state',timeout=.3).close();break
            except OSError:time.sleep(.1)
        with sync_playwright() as p:
            browser=p.chromium.launch(executable_path=os.environ.get('SYSTEM_CHROME'))
            results=[]
            for width in (320,360,390,430,844):
                context=browser.new_context(viewport={'width':width,'height':844},is_mobile=True,has_touch=True,device_scale_factor=3)
                page=context.new_page();errors=[];page.on('pageerror',lambda e:errors.append(str(e)))
                page.goto(url);expect(page.locator('#simPower')).to_be_visible();check(page)
                for selector in ('#simPower','#simCadence','#powerFloat','#cadenceFloat'):
                    page.locator(selector).tap();check(page)
                page.locator('.device-settings').evaluate('(el)=>el.open=true')
                page.locator('#deviceNameInput').tap();check(page)
                page.set_viewport_size({'width':width,'height':420});check(page)
                page.set_viewport_size({'width':width,'height':844})
                page.locator('[data-sport=running]').tap();check(page)
                page.locator('[data-sport=cycling]').tap();check(page)
                page.locator('[data-source=bridge]').tap();check(page)
                page.locator('[data-source=simulation]').tap();check(page)
                assert page.locator('[name=viewport]').get_attribute('content').find('user-scalable=no')<0
                assert not errors,errors
                results.append({'width':width,'result':'PASS','check':check(page)})
                context.close()
            browser.close();print(json.dumps(results,ensure_ascii=False,indent=2))
    finally:proc.terminate();proc.wait(timeout=5)
if __name__=='__main__':main()
