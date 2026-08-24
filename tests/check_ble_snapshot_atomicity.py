#!/usr/bin/env python3
"""BLE 通知输入快照必须整组更新，避免 timer 读取混合帧。"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = (ROOT / "main" / "ble_bikepower.c").read_text(encoding="utf-8")


def body_between(start, end):
    return SOURCE.split(start, 1)[1].split(end, 1)[0]


checks = {
    "定义专用数据临界区": "static portMUX_TYPE g_data_mux" in SOURCE,
    "骑行快照整组更新": "portENTER_CRITICAL(&g_data_mux)" in body_between(
        "void ble_bikepower_set_data", "void ble_bikepower_set_broadcast_mask"
    ),
    "跑步快照整组更新": "portENTER_CRITICAL(&g_data_mux)" in body_between(
        "void ble_bikepower_set_running_data", "void ble_bikepower_set_erg_callback"
    ),
    "广播掩码原子更新": "portENTER_CRITICAL(&g_data_mux)" in body_between(
        "void ble_bikepower_set_broadcast_mask", "void ble_bikepower_set_profile"
    ),
    "骑行编码读取同一帧": "portENTER_CRITICAL(&g_data_mux)" in body_between(
        "pack_all(void)", "pack_running(void)"
    ),
    "跑步编码读取同一帧": "portENTER_CRITICAL(&g_data_mux)" in body_between(
        "pack_running(void)", "static void notify_one_nolock"
    ),
    "GATT 读取先复制 payload": "chr_read_measurement" in SOURCE,
}

failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(f"[ble-snapshot-atomicity] {'PASS' if ok else 'FAIL'} {name}")
if failed:
    raise SystemExit(1)
print("[ble-snapshot-atomicity] PASS: BLE 通知输入按整组快照更新")
