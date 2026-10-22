#!/usr/bin/env python3
"""FTMS Control Point 操作码、回调接受与响应顺序静态契约。"""

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "main" / "bikepower_config.h"
BLE_C = ROOT / "main" / "ble_bikepower.c"
BLE_H = ROOT / "main" / "ble_bikepower.h"
APP_MAIN = ROOT / "main" / "app_main.c"
TAG = "[ftms-control-contract]"


def extract_define(source, name):
    match = re.search(rf"^\s*#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+|\d+)", source, re.M)
    return int(match.group(1), 0) if match else None


def extract_function(source, name):
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", source, re.S)
    if not match:
        return ""
    start = match.start()
    brace = source.find("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    return ""


def extract_case(function_source, label):
    match = re.search(
        rf"case\s+{re.escape(label)}\s*:(.*?)(?=\n\s*(?:case\s+|default\s*:))",
        function_source,
        re.S,
    )
    return match.group(1) if match else ""


def main():
    failures = []
    sources = {}
    for path in (CONFIG, BLE_C, BLE_H, APP_MAIN):
        if path.is_file():
            sources[path] = path.read_text(encoding="utf-8")
        else:
            sources[path] = ""
            failures.append(f"缺少 {path.relative_to(ROOT)}")

    def check(condition, message):
        if condition:
            print(f"{TAG} PASS {message}")
        else:
            failures.append(message)
            print(f"{TAG} FAIL {message}")

    config = sources[CONFIG]
    ble_c = sources[BLE_C]
    ble_h = sources[BLE_H]
    app_main = sources[APP_MAIN]

    expected_constants = {
        "FTMS_OP_REQUEST_CONTROL": 0x00,
        "FTMS_OP_RESET": 0x01,
        "FTMS_OP_SET_TARGET_SPEED": 0x02,
        "FTMS_OP_SET_TARGET_POWER": 0x05,
        "FTMS_OP_START_RESUME": 0x07,
        "FTMS_OP_STOP_PAUSE": 0x08,
        "FTMS_OP_RESPONSE_CODE": 0x80,
        "FTMS_RESULT_SUCCESS": 0x01,
        "FTMS_RESULT_NOT_SUPPORTED": 0x02,
        "FTMS_RESULT_INVALID_PARAM": 0x03,
        "FTMS_RESULT_OPERATION_FAILED": 0x04,
        "FTMS_STATUS_RESET": 0x01,
        "FTMS_STATUS_STOPPED_PAUSED_BY_USER": 0x02,
        "FTMS_STATUS_STARTED_RESUMED_BY_USER": 0x03,
    }
    for name, expected in expected_constants.items():
        actual = extract_define(config, name)
        check(actual == expected, f"{name}=0x{expected:02X} (实际={actual!r})")

    check(
        "typedef bool (*ble_run_control_cb_t)(ble_run_control_type_t type, int value)" in ble_h,
        "running 控制回调用 bool 表示事件是否被可靠接受",
    )

    on_run_control = extract_function(app_main, "on_run_control")
    check(bool(on_run_control), "可提取 app_main on_run_control 实现")
    for token in (
        "BLE_RUN_CTRL_SPEED",
        "BLE_RUN_CTRL_START",
        "BLE_RUN_CTRL_STOP",
        "BLE_RUN_CTRL_RESET",
        "APP_EVT_RUN_TARGET_SPEED",
        "APP_EVT_RUN_START",
        "APP_EVT_RUN_STOP",
        "APP_EVT_RUN_RESET",
    ):
        check(token in on_run_control, f"on_run_control 覆盖 {token}")
    check(
        "xQueueSend(s_app_queue, &e, 0) == pdTRUE" in on_run_control,
        "running 控制仅在非阻塞入队成功时返回 true",
    )

    cp = extract_function(ble_c, "ftms_cp_cb")
    response = extract_function(ble_c, "ftms_send_response")
    check(bool(cp), "可提取 ftms_cp_cb 实现")
    check(
        all(token in response for token in (
            "FTMS_OP_RESPONSE_CODE",
            "request_op",
            "result",
            "indicate_one",
        )),
        "控制点响应使用 0x80/request/result 三字节 indication",
    )

    status_sender = extract_function(ble_c, "ftms_send_status")
    check(
        "g_ftms_status_buf[1]" in ble_c
        and "sizeof(g_ftms_status_buf)" in status_sender
        and "notify_one" in status_sender,
        "Reset/Stopped/Started Fitness Machine Status 使用单字节通知",
    )

    speed_case = extract_case(cp, "FTMS_OP_SET_TARGET_SPEED")
    check(bool(speed_case), "存在 Set Target Speed 分支")
    check(
        "RUN_SPEED_X100_MIN" in speed_case
        and "RUN_SPEED_X100_MAX" in speed_case
        and "FTMS_RESULT_INVALID_PARAM" in speed_case,
        "Set Target Speed 值域非法返回 Invalid Parameter",
    )
    check(
        "BLE_RUN_CTRL_SPEED" in speed_case
        and "FTMS_RESULT_OPERATION_FAILED" in speed_case
        and "FTMS_RESULT_SUCCESS" in speed_case,
        "Set Target Speed 区分回调成功与入队失败",
    )

    for opcode, control, status in (
        ("FTMS_OP_START_RESUME", "BLE_RUN_CTRL_START", "FTMS_STATUS_STARTED_RESUMED_BY_USER"),
        ("FTMS_OP_STOP_PAUSE", "BLE_RUN_CTRL_STOP", "FTMS_STATUS_STOPPED_PAUSED_BY_USER"),
        ("FTMS_OP_RESET", "BLE_RUN_CTRL_RESET", "FTMS_STATUS_RESET"),
    ):
        branch = extract_case(cp, opcode)
        check(bool(branch), f"存在 {opcode} 分支")
        callback_pos = branch.find(control)
        status_pos = branch.find(f"ftms_send_status({status})")
        success_pos = branch.rfind("FTMS_RESULT_SUCCESS")
        check(
            callback_pos >= 0
            and "FTMS_RESULT_OPERATION_FAILED" in branch
            and status_pos > callback_pos
            and success_pos > status_pos,
            f"{opcode} 回调未接受则 Operation Failed，仅成功后更新 status 并回 Success",
        )

    stop_case = extract_case(cp, "FTMS_OP_STOP_PAUSE")
    reset_case = extract_case(cp, "FTMS_OP_RESET")
    check(
        "g_erg_clear_cb" in stop_case and "g_erg_clear_cb" in reset_case,
        "cycling Stop/Reset 仍保留 ERG clear 语义",
    )

    cp_defs = re.findall(
        r"FTMS_CONTROL_POINT_CHAR_UUID.*?\.flags\s*=\s*([^,\n]+(?:\|[^,\n]+)*)",
        ble_c,
        re.S,
    )
    check(len(cp_defs) >= 2, "cycling/running GATT 表都声明 FTMS Control Point")
    check(
        len(cp_defs) >= 2
        and all("BLE_GATT_CHR_F_INDICATE" in flags for flags in cp_defs[:2]),
        "cycling/running Control Point 响应均使用 indicate",
    )

    if failures:
        print(f"{TAG} FAIL: {len(failures)} 项契约不满足", file=sys.stderr)
        for failure in failures:
            print(f"{TAG}   - {failure}", file=sys.stderr)
        return 1
    print(f"{TAG} PASS: FTMS Control Point 契约全部满足")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
