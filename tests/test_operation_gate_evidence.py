#!/usr/bin/env python3
"""Functional tests for operation gate acceptance evidence recording."""
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts" / "tools"))
sys.path.insert(0, str(ROOT / "tests"))

from device_acceptance import load_record, new_record, set_item, write_record  # noqa: E402
from operation_gate import Gate, prepare_evidence, record_gate_result  # noqa: E402

TAG = "[operation-gate-evidence-test]"
VERSION = "9.8.7"
GIT_SHA = "abc1234"


def main():
    with tempfile.TemporaryDirectory() as tmp:
        gate = Gate("device", Path(tmp) / "report.md")
        gate.record("ble_running", "fake running", 1, "failed", required=True)
        gate.record("ble_reconnect", "fake reconnect", 1, "failed", required=True)
        assert len(gate.failures) == 2, "P0 running/reconnect 失败必须阻断 device gate"
        print(f"{TAG} PASS P0 running/reconnect failures are required")

    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "device-acceptance.json"
        record = new_record(VERSION, GIT_SHA, "generic-c3", "tester")
        set_item(record, "led_patterns", "PASS", "manual LED evidence")
        write_record(path, record)

        prepared = prepare_evidence(
            path,
            firmware_version=VERSION,
            git_sha=GIT_SHA,
            board="",
            operator="",
        )
        assert prepared == path

        record_gate_result(
            path,
            "serial_boot",
            rc=0,
            output="boot ok\nheap ok",
            command="serial_capture.py",
        )
        record_gate_result(
            path,
            "ble_cycling",
            rc=1,
            output="connect timeout",
            command="ble_selftest.py",
            required=False,
        )

        updated = load_record(path)
        assert updated["items"]["serial_boot"]["status"] == "PASS"
        assert updated["items"]["ble_config_rw"]["status"] == "NOT_RUN"
        assert "serial_capture.py" in updated["items"]["serial_boot"]["evidence"]
        assert updated["items"]["ble_cycling"]["status"] == "FAIL"
        assert "connect timeout" in updated["items"]["ble_cycling"]["evidence"]
        assert updated["items"]["portal_http"]["status"] == "NOT_RUN"
        assert updated["items"]["led_patterns"]["status"] == "PASS"
        assert updated["items"]["led_patterns"]["evidence"] == "manual LED evidence"
        print(f"{TAG} PASS result mapping and manual evidence preservation")

    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "device-acceptance.json"
        prepared = prepare_evidence(
            path,
            firmware_version=VERSION,
            git_sha=GIT_SHA,
            board="m5stamp-c3",
            operator="tester",
        )
        assert prepared == path
        created = load_record(path)
        assert created["board"] == "m5stamp-c3"
        assert created["operator"] == "tester"
        print(f"{TAG} PASS creates evidence when board is explicit")

    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "device-acceptance.json"
        prepared = prepare_evidence(
            path,
            firmware_version=VERSION,
            git_sha=GIT_SHA,
            board="",
            operator="",
        )
        assert prepared is None
        assert not path.exists()
        print(f"{TAG} PASS missing board leaves evidence disabled")

    print(f"{TAG} PASS: operation gate evidence tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
