#!/usr/bin/env python3
"""操作覆盖矩阵一致性检查。

确保关键用户操作都有门禁记录：每个操作 ID 必须在文档中出现，
并且必须绑定自动化命令或人工确认项，避免新增功能后测试矩阵漂移。
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
DOC = ROOT / "docs" / "dev" / "05-operation-test-gate.md"

REQUIRED_IDS = [
    "OP-MODE-001",
    "OP-MODE-002",
    "OP-MODE-003",
    "OP-SAVE-001",
    "OP-SAVE-002",
    "OP-SAVE-003",
    "OP-SAVE-004",
    "OP-BTN-001",
    "OP-BTN-002",
    "OP-BTN-003",
    "OP-WEB-001",
    "OP-WEB-002",
    "OP-WEB-003",
    "OP-WEB-004",
    "OP-BCAST-001",
    "OP-BCAST-002",
    "OP-BCAST-003",
    "OP-BLE-001",
    "OP-BLE-002",
    "OP-BLE-003",
    "OP-RUN-001",
    "OP-RUN-002",
    "OP-WIFI-001",
    "OP-STAB-001",
    "OP-STAB-002",
    "OP-REL-001",
    "OP-MANUAL-001",
]

REQUIRED_COMMANDS = [
    "make -C test run",
    "python3 tests/test_portal_config_save.py",
    "python3 tests/test_core_flow_endurance.py",
    "python3 tests/ble_selftest.py",
    "python3 tests/test_ble_protocol_vectors.py",
    "python3 tests/test_ble_config_e2e.py",
    "python3 tests/ble_config_e2e.py",
    "python3 tests/test_ble_reconnect_harness.py",
    "python3 tests/test_ble_running_harness.py",
    "python3 tests/test_device_profile_harness.py",
    "python3 tests/check_ftms_control_contract.py",
    "python3 tests/check_wifi_done_contract.py",
    "python3 tests/ble_broadcast_monitor.py",
    "python3 tests/check_ble_monitor.py",
    "python3 tests/test_prepare_web_flash_site.py",
    "python3 tests/stage5_portal_test.py",
    "python3 tests/check_config_consistency.py",
    "python3 tests/check_operation_coverage.py",
]


def fail(msg):
    print(f"[operation-coverage] FAIL {msg}")
    return 1


def main():
    if not DOC.exists():
        return fail(f"缺少操作覆盖矩阵文档: {DOC.relative_to(ROOT)}")

    text = DOC.read_text(encoding="utf-8")
    failures = []
    for op_id in REQUIRED_IDS:
        if op_id not in text:
            failures.append(f"缺少操作 ID {op_id}")
            continue
        pattern = re.compile(rf"\|\s*`?{re.escape(op_id)}`?\s*\|.*\|.*\|.*\|", re.MULTILINE)
        if not pattern.search(text):
            failures.append(f"{op_id} 未以矩阵表格行记录")

    for command in REQUIRED_COMMANDS:
        if command not in text:
            failures.append(f"缺少门禁命令 `{command}`")

    if "人工门禁" not in text:
        failures.append("缺少人工门禁章节")
    if "不能冒充自动通过" not in text:
        failures.append("缺少不可自动化项的结果记录规则")
    if "tests/operation_gate.py" not in text:
        failures.append("缺少统一门禁入口 tests/operation_gate.py")

    if failures:
        for item in failures:
            print(f"[operation-coverage] FAIL {item}")
        print(f"[operation-coverage] 结果: {len(failures)} 失败")
        return 1

    print(f"[operation-coverage] PASS: {len(REQUIRED_IDS)} 个关键操作均有门禁记录")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
