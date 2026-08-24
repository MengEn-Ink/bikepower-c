#!/usr/bin/env python3
"""BLE 重连真机脚本结构门禁。"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = (ROOT / "tests" / "stage3_conn_test.py").read_text(encoding="utf-8")

checks = [
    ("async def find_device()" in SOURCE, "重连脚本必须封装可重复扫描"),
    (SOURCE.count("await find_device()") >= 2, "重连脚本必须执行两次独立扫描"),
    (SOURCE.count("async with BleakClient(") >= 2, "重连脚本必须执行两次独立连接"),
    ("first_connected" in SOURCE and "second_connected" in SOURCE,
     "重连结论必须同时依赖两次连接成功"),
]

failures = [message for condition, message in checks if not condition]
for failure in failures:
    print(f"[ble-reconnect-check] FAIL {failure}")
if failures:
    raise SystemExit(1)

print("[ble-reconnect-check] PASS: BLE 重连脚本覆盖二次扫描与二次连接")
