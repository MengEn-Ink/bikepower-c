#!/usr/bin/env python3
"""生成共用页面来源的独立设备与手机操作演示。"""
import re
from pathlib import Path
from build_phone_demo import render as render_phone

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "design-demos/bikepower-ui/operation-demos"


def render():
    source = (ROOT / "tools/web/ble-monitor.html").read_text()
    css = re.search(r"<style>([\s\S]*?)</style>", source).group(1)
    css = css.replace("body>:not(.mobile-guide):not(script),", "")
    for name in ("pages-experience", "operation-demos", "phone-operation-demos"):
        css += (ROOT / f"tools/web/{name}.css").read_text()
    css += """
body{background:var(--bg);padding:32px 24px}
.demo-document{max-width:820px;margin:auto}
.demo-document>header{border-bottom:1px solid var(--line);padding-bottom:18px}
.demo-document h1{font-size:38px;letter-spacing:-.03em;line-height:1.25;color:var(--strong);margin:10px 0}
.demo-document>header p{font-size:14px;color:var(--muted);margin:10px 0 0}
.demo-document .theme-btn{float:right}
.demo-document footer{font-size:12px;color:var(--muted);border-top:1px solid var(--line);padding:18px 0}
.guide-collection{margin:32px 0}.guide-collection>h2{font-size:24px;color:var(--strong)}
.film body{padding:24px 40px}
.film .demo-document>header{display:none}
.film .guide-collection,.film footer,.film .theme-btn,.film .practice,.film .demo-controls,.film .demo-scenes{display:none}
.film .phone-stage{padding:18px 0 26px}
.film .phone-demo-head{margin:14px 0}
.film .operation-demos{margin:14px 0}
.film .phone-caption{margin:12px 0;font-size:19px!important}
.film .operation-caution{margin:8px 0;font-size:14px!important}
.film .operation-disclaimer{margin:10px 0}
.film .demo-document h1{font-size:28px}
.film .phone-scenes{display:none}
@media(max-width:600px){body{padding:22px 16px}.demo-document h1{font-size:30px}}
"""
    scripts = render_phone()
    for name in ("phone-operation-demos", "operation-demos", "pages-experience"):
        scripts += (ROOT / f"tools/web/{name}.js").read_text()
    controller = """
function toggleTheme(){
  document.documentElement.dataset.theme=document.documentElement.dataset.theme==="light"?"dark":"light";
}
document.addEventListener("DOMContentLoaded",()=>{
  const params=new URLSearchParams(location.search);
  document.documentElement.dataset.theme=params.get("theme")||"light";
  if(params.has("film"))document.documentElement.classList.add("film");
  if(params.has("phone"))document.querySelector('[data-operation-tab="phone"]').click();
});
"""
    guides = [("hotspot", "手机连接热点"), ("cycling", "设置骑行"), ("running", "设置跑步"),
              ("profile", "填写用户资料"), ("device", "修改名称"), ("step", "调整按钮步进"),
              ("lights", "看懂指示灯"), ("upstream", "接入功率计"), ("about", "核对版本"),
              ("exit", "保存退出"), ("connect", "连接 App"), ("troubleshoot", "连接排查"),
              ("overview", "使用顺序"), ("install", "电脑安装"), ("compare", "电脑比对")]
    return (
        '<!doctype html><html lang="zh-CN" data-theme="light"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1"><title>BikePower 操作演示</title>'
        '<style>' + css + '</style></head><body><main class="demo-document">'
        '<header><button class="theme-btn" type="button" onclick="toggleTheme()" aria-label="切换主题">浅 / 深</button>'
        '<h1>先看一次，再亲手操作。</h1><p>设备看按键与灯光，手机跟随真实页面。模拟演示，可以放心练习。</p></header>'
        '<div class="operation-demos" data-operation-demos></div>'
        '<section class="guide-collection"><h2>其他步骤，展开看演示</h2>' +
        ''.join('<details class="guide-faq"><summary>' + title +
                '</summary><div data-operation-guide="' + name + '"></div></details>' for name, title in guides) +
        '</section><footer>先松开，再确认。保存成功后再退出；连接 App 前释放网页连接。灯色以你的设置为准。</footer>'
        '<script>' + scripts + '</script><script>' + controller + '</script></main></body></html>'
    )


if __name__ == "__main__":
    if not OUT.is_dir():
        raise SystemExit("请先确认操作演示输出目录")
    (OUT / "index.html").write_text(render())
    print("已生成离线设备与手机交互演示")
