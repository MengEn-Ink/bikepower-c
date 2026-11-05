#!/usr/bin/env python3
"""执行生产 BLE 标准回调、诊断规则和页面交接回归。"""
from pathlib import Path
import shutil
import subprocess
import sys

if __name__ == "__main__":
    root = Path(__file__).resolve().parent
    node = shutil.which("node")
    if not node:
        raise RuntimeError("未找到 node")
    for args in [
        [sys.executable, str(root / "test_ble_cycling_services.py")],
        [node, "--test", str(root / "test_ble_compatibility.cjs")],
        [node, str(root / "test_ble_compatibility_browser.cjs")],
    ]:
        subprocess.run(args, check=True)
