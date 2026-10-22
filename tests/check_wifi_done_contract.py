#!/usr/bin/env python3
"""WiFi 配网完成通知可靠性静态契约门禁。"""

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
PORTAL_H = ROOT / "main" / "wifi_portal.h"
PORTAL_C = ROOT / "main" / "wifi_portal.c"
APP_MAIN = ROOT / "main" / "app_main.c"
APP_STATE = ROOT / "main" / "app_state.c"
RADIO_CTRL = ROOT / "main" / "radio_ctrl.c"
TAG = "[wifi-done-contract]"


def extract_function(source, name):
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
    if not match:
        return ""
    start = match.start()
    brace = source.find("{", match.start())
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    return ""


def extract_braced_after(source, marker):
    start = source.find(marker)
    if start < 0:
        return ""
    brace = source.find("{", start + len(marker))
    if brace < 0:
        return ""
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    return ""


def main():
    sources = {}
    failures = []
    for path in (PORTAL_H, PORTAL_C, APP_MAIN, APP_STATE, RADIO_CTRL):
        if not path.is_file():
            failures.append(f"缺少 {path.relative_to(ROOT)}")
            sources[path] = ""
        else:
            sources[path] = path.read_text(encoding="utf-8")

    def check(condition, message):
        if condition:
            print(f"{TAG} PASS {message}")
        else:
            failures.append(message)
            print(f"{TAG} FAIL {message}")

    portal_h = sources[PORTAL_H]
    portal_c = sources[PORTAL_C]
    app_main = sources[APP_MAIN]
    app_state = sources[APP_STATE]
    radio_ctrl = sources[RADIO_CTRL]

    check(
        "typedef bool (*wifi_portal_done_callback_t)(void)" in portal_h
        and "wifi_portal_set_done_callback(wifi_portal_done_callback_t cb)" in portal_h,
        "done callback 用 bool 表示应用层是否可靠接受",
    )

    portal_done = extract_function(portal_c, "portal_done")
    callback_pos = portal_done.find("s_done_cb()")
    closing_pos = portal_done.find("s_closing = true")
    done_pos = portal_done.find("s_done_called = true")
    check(bool(portal_done), "可提取 portal_done 实现")
    check(
        0 <= closing_pos < callback_pos < done_pos,
        "portal_done 先进入 closing，回调接受后才置 done",
    )
    check(
        "bool accepted" in portal_done and "if (accepted)" in portal_done,
        "portal_done 仅在 callback 返回 true 后完成幂等标记",
    )

    shutdown_check = extract_function(portal_c, "shutdown_check_cb")
    check(
        "if (s_done_called) return" in shutdown_check
        and "portal_done();" in shutdown_check,
        "定时检查在 done 未被接受时保留重试",
    )

    wifi_done = extract_function(app_main, "on_wifi_done")
    check(
        re.search(r"static\s+bool\s+on_wifi_done\s*\(void\)", app_main) is not None,
        "应用层 done callback 返回 bool",
    )
    check(
        "xQueueSend(s_app_queue, &e, 0)" in wifi_done,
        "esp_timer 回调只做非阻塞队列投递",
    )
    check(
        "s_pending_wifi_done = true" in wifi_done
        and "portDISABLE_INTERRUPTS()" in wifi_done
        and "return true" in wifi_done,
        "队列满时在临界区设置 pending latch 并确认接受",
    )

    loop_pos = app_main.find("while (1)")
    consume_pos = app_main.find("if (s_pending_wifi_done)", loop_pos)
    ble_continue_pos = app_main.find("if (!ble_bikepower_is_ready())", loop_pos)
    check(
        0 <= loop_pos < consume_pos < ble_continue_pos,
        "主循环在 BLE-not-ready continue 前消费 pending WiFi done",
    )
    consume_window = app_main[consume_pos:ble_continue_pos]
    check(
        "s_pending_wifi_done = false" in consume_window
        and "APP_EVT_WIFI_DONE" in consume_window
        and "app_state_handle_event" in consume_window,
        "pending latch 原子取清后交给状态机",
    )

    wifi_case_src = extract_braced_after(app_state, "case APP_EVT_WIFI_DONE:")
    check(bool(wifi_case_src), "可提取 APP_EVT_WIFI_DONE 状态处理")
    check(
        "bool keep_error = s_state == ST_ERROR_RECOVERY" in wifi_case_src,
        "WiFi 资源清理前记录错误恢复态",
    )
    check(
        "if (s_state == ST_WIFI_CONFIG)" not in wifi_case_src
        and "s_wifi_exit();" in wifi_case_src,
        "APP_EVT_WIFI_DONE 不受 ST_WIFI_CONFIG 守卫限制，无条件调幂等 exit",
    )
    check(
        "if (keep_error)" in wifi_case_src
        and "LED_SCENE_ERROR" in wifi_case_src
        and "s_state = ST_BLE_ACTIVE" in wifi_case_src,
        "清理后保留 ERROR 状态，非 ERROR 才恢复 BLE_ACTIVE",
    )

    exit_wifi = extract_function(radio_ctrl, "radio_ctrl_exit_wifi")
    check(
        all(
            token in exit_wifi
            for token in (
                "wifi_portal_stop()",
                "captive_portal_stop_dns()",
                "radio_ctrl_stop_wifi_ap()",
            )
        ),
        "radio_ctrl_exit_wifi 仍统一幂等清理 HTTP/DNS/SoftAP",
    )

    if failures:
        print(f"{TAG} FAIL: {len(failures)} 项契约不满足", file=sys.stderr)
        for failure in failures:
            print(f"{TAG}   - {failure}", file=sys.stderr)
        return 1
    print(f"{TAG} PASS: WiFi done 可靠性契约全部满足")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
