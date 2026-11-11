#!/usr/bin/env python3
"""导出使用生产页面与隔离接口的可交互离线配置页。"""
import json
from pathlib import Path
from build_phone_demo import render

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "design-demos/bikepower-ui/configuration-preview/configuration.html"


if __name__ == "__main__":
    if not TARGET.parent.is_dir():
        raise SystemExit("请先确认配置页预览输出目录")
    page = json.loads(render().split("window.BikePowerPhoneSource=", 1)[1].rstrip(";\n"))
    page = page.replace("<title>BikePower · 设备配置</title>", "<title>BikePower · 离线配置模拟</title>")
    TARGET.write_text(page, encoding="utf-8")
    print("已导出离线配置页：所有数据与保存操作仅在本地模拟")
