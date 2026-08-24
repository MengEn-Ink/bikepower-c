#!/usr/bin/env python3
"""WiFi 配网完成事件最终送达契约检查。"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP_MAIN = (ROOT / "main" / "app_main.c").read_text(encoding="utf-8")
PORTAL_C = (ROOT / "main" / "wifi_portal.c").read_text(encoding="utf-8")
PORTAL_H = (ROOT / "main" / "wifi_portal.h").read_text(encoding="utf-8")

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print(f"[wifi-done-check] FAIL {message}")


check("bool (*cb)(void)" in PORTAL_H, "done callback 必须返回 bool")
check("static bool on_wifi_done(void)" in APP_MAIN, "app_main done callback 必须返回 bool")
check("s_pending_wifi_done" in APP_MAIN, "app_main 必须提供 pending WiFi done latch")
check("if (s_pending_wifi_done)" in APP_MAIN, "主循环必须消费 pending WiFi done latch")
done_callback = APP_MAIN.split("static bool on_wifi_done(void)", 1)[1].split("void app_main(void)", 1)[0]
check("pdMS_TO_TICKS" not in done_callback, "esp_timer done callback 禁止阻塞等待队列")
check("xQueueSend(s_app_queue, &e, 0)" in done_callback, "done callback 只能零等待投递")
check("if (s_done_cb && s_done_cb())" in PORTAL_C, "portal 仅在回调接受后标记完成")
shutdown_body = PORTAL_C.split("static void shutdown_check_cb", 1)[1].split(
    "static void portal_done", 1
)[0]
check("if (s_done_called) return;" in shutdown_body, "定时器仅在完成已被接受后停止")
check("if (s_closing) return;" not in shutdown_body, "closing 状态不能阻止完成通知重试")
check("portal_done();" in shutdown_body, "定时器必须再次调用 portal_done")

if failures:
    raise SystemExit(1)

print("[wifi-done-check] PASS: WiFi 完成事件具备最终送达契约")
