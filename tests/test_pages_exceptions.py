#!/usr/bin/env python3
"""通过 Node/Chromium 执行实际 Pages 配置异常与重试回归。"""
from pathlib import Path
import shutil
import subprocess

if __name__ == "__main__":
    node = shutil.which("node")
    if not node:
        raise RuntimeError("未找到 node")
    subprocess.run([node, str(Path(__file__).with_name("test_pages_exception_browser.cjs"))], check=True)
