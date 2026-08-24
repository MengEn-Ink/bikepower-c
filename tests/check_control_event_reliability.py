#!/usr/bin/env python3
"""FTMS 控制事件队列满时的顺序契约检查。"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = (ROOT / "main" / "app_main.c").read_text(encoding="utf-8")

checks = [
    ("s_pending_erg_event" in SOURCE, "必须用单一 pending ERG 事件邮箱"),
    ("s_has_pending_erg_event" in SOURCE, "必须记录 pending ERG 事件有效位"),
    ("s_pending_erg_target" not in SOURCE, "不得分别缓存 ERG target"),
    ("s_pending_erg_clear" not in SOURCE, "不得分别缓存 ERG clear"),
]
target_callback = SOURCE.split("static bool on_erg_target", 1)[1].split("static void on_erg_clear", 1)[0]
clear_callback = SOURCE.split("static void on_erg_clear", 1)[1].split("static bool on_run_control", 1)[0]
checks.extend([
    ("xQueueSend" not in target_callback, "ERG target 不得与队列混用导致新旧命令反转"),
    ("xQueueSend" not in clear_callback, "ERG clear 不得与队列混用导致新旧命令反转"),
])

failures = [message for condition, message in checks if not condition]
for failure in failures:
    print(f"[control-event-check] FAIL {failure}")
if failures:
    raise SystemExit(1)

print("[control-event-check] PASS: ERG 队列满时保持最后命令语义")
