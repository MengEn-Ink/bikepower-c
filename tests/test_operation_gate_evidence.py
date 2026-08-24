#!/usr/bin/env python3
"""Functional tests for operation gate acceptance evidence recording."""
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts" / "tools"))
sys.path.insert(0, str(ROOT / "tests"))

from device_acceptance import load_record, new_record, set_item, write_record  # noqa: E402
from operation_gate import device_steps, prepare_evidence, record_gate_result  # noqa: E402

TAG = "[operation-gate-evidence-test]"
VERSION = "9.8.7"
GIT_SHA = "abc1234"


class StepRecorder:
    def __init__(self):
        self.steps = []
        self.skips = []

    def record_step(self, name, required, acceptance_item):
        self.steps.append((name, required, acceptance_item))

    def note_skip(self, name, reason):
        self.skips.append((name, reason))


def main():
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

    import operation_gate

    recorder = StepRecorder()
    original_run_step = operation_gate.run_step

    def fake_run_step(gate, name, command, timeout=120, required=True, acceptance_item=None):
        del command, timeout
        gate.record_step(name, required, acceptance_item)

    operation_gate.run_step = fake_run_step
    try:
        device_steps(recorder, port="/dev/fake", run_portal=True, device_profile="cycling")
    finally:
        operation_gate.run_step = original_run_step

    required_by_item = {
        acceptance_item: required
        for _name, required, acceptance_item in recorder.steps
        if acceptance_item
    }
    for item_id in (
        "serial_boot",
        "ble_cycling",
        "ble_reconnect",
        "portal_http",
        "broadcast_mask",
    ):
        assert required_by_item.get(item_id) is True, f"{item_id} 必须是 required P0"
    assert not any(item == "ble_running" for _name, _required, item in recorder.steps)

    recorder = StepRecorder()
    operation_gate.run_step = fake_run_step
    try:
        device_steps(recorder, port="/dev/fake", run_portal=False, device_profile="running")
    finally:
        operation_gate.run_step = original_run_step
    required_by_item = {
        acceptance_item: required
        for _name, required, acceptance_item in recorder.steps
        if acceptance_item
    }
    assert required_by_item.get("ble_running") is True, "ble_running 必须是 required P0"
    assert not any(item == "ble_cycling" for _name, _required, item in recorder.steps)
    print(f"{TAG} PASS device P0 steps are required")

    print(f"{TAG} PASS: operation gate evidence tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
