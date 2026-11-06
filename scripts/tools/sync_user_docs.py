#!/usr/bin/env python3
"""从 Pages 手机手册生成离线版；--check 校验是否漂移。"""
import argparse
import re
from pathlib import Path
from build_phone_demo import render as render_phone

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "docs/user/pages-mobile-guide.html"


def render():
    source = (ROOT / "tools/web/ble-monitor.html").read_text(encoding="utf-8")
    theme = re.search(r'<script id="theme-boot">([\s\S]*?)</script>', source).group(1)
    css = re.search(r"<style>([\s\S]*?)</style>", source).group(1)
    guide = '<div class="mobile-guide"' + source.split(
        '<div class="mobile-guide"', 1
    )[1].split('<div class="wrap">', 1)[0]
    guide = guide.replace("移动端 · 只读", "离线使用手册")
    guide = guide.replace("正在读取 Release", "请到在线工具台查看")
    guide = guide.replace(
        "./manual-images/luatos-core-esp32c3-official-reference.png",
        "https://wiki.luatos.com/chips/esp32c3/board.html",
    )
    css += (ROOT / "tools/web/operation-demos.css").read_text(encoding="utf-8")
    css += (ROOT / "tools/web/phone-operation-demos.css").read_text(encoding="utf-8")
    css += (ROOT / "tools/web/pages-experience.css").read_text(encoding="utf-8")
    css += "\n.mobile-guide{display:block!important;max-width:560px;margin:auto}.mg-hero h1{font-size:40px}.mg-hero p{font-size:15px}.mobile-guide [id]{scroll-margin-top:75px}\n"
    script = render_phone()
    script += (ROOT / "tools/web/phone-operation-demos.js").read_text(encoding="utf-8")
    script += (ROOT / "tools/web/operation-demos.js").read_text(encoding="utf-8")
    script += (ROOT / "tools/web/pages-experience.js").read_text(encoding="utf-8")
    script = script.replace(
        "./manual-images/luatos-core-esp32c3-official-reference.png",
        "https://wiki.luatos.com/chips/esp32c3/board.html",
    )
    script += """
function toggleTheme(){
  const root=document.documentElement;
  const next=root.dataset.theme==="light"?"dark":"light";
  root.dataset.theme=next;
  root.classList.toggle("dark",next==="dark");
  try{localStorage.setItem("bikepower.theme.v1",next)}catch(_){}
}
async function mgCopyLink(){
  const result=document.getElementById("mg-copied");
  try{await navigator.clipboard.writeText("https://mengen-ink.github.io/bikepower-c/");result.textContent="已复制在线工具台链接"}
  catch(_){result.textContent="请长按上方网址复制"}
}
function mgShareLink(){mgCopyLink()}
"""
    return (
        '<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        '<meta name="color-scheme" content="light dark">'
        '<title>BikePower 手机使用手册 · 离线版</title><script>' + theme
        + "</script><style>" + css + "</style></head><body>" + guide
        + "<script>" + script + "</script></body></html>"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = render()
    if args.check:
        if not TARGET.is_file() or TARGET.read_text(encoding="utf-8") != expected:
            print("离线手册已漂移：运行 python3 scripts/tools/sync_user_docs.py")
            return 1
        print("离线手册与 Pages 来源一致")
    else:
        TARGET.write_text(expected, encoding="utf-8")
        print("已同步 docs/user/pages-mobile-guide.html")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
