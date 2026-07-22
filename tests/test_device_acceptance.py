#!/usr/bin/env python3
"""Functional tests for the device acceptance evidence tool."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "scripts" / "tools"
sys.path.insert(0, str(TOOLS))

from device_acceptance import (  # noqa: E402
    P0_ITEM_IDS,
    AcceptanceError,
    load_record,
    new_record,
    set_item,
    summarize_record,
    validate_record,
    write_record,
)

TAG = "[device-acceptance-test]"
VERSION = "9.8.7"
GIT_SHA = "abc1234"
NOW = "2026-07-22T12:00:00+08:00"


def expect_error(message, fn):
    try:
        fn()
    except AcceptanceError as exc:
        if message not in str(exc):
            raise AssertionError(f"expected {message!r}, got {exc!r}") from exc
        print(f"{TAG} PASS rejected: {message}")
        return
    raise AssertionError(f"expected AcceptanceError containing {message!r}")


def complete_p0(record):
    for item_id in P0_ITEM_IDS:
        set_item(record, item_id, "PASS", f"{item_id} verified", now=NOW)


def test_record_model():
    record = new_record(VERSION, GIT_SHA, "generic-c3", "tester", now=NOW)
    assert record["schema_version"] == 1
    assert record["firmware_version"] == VERSION
    assert record["git_sha"] == GIT_SHA
    assert record["board"] == "generic-c3"
    assert record["created_at"] == NOW
    assert record["updated_at"] == NOW
    assert record["items"]["serial_boot"]["status"] == "NOT_RUN"

    validate_record(
        record,
        expected_version=VERSION,
        expected_git_sha=GIT_SHA,
        strict=False,
    )
    expect_error(
        "P0 验收未通过",
        lambda: validate_record(
            record,
            expected_version=VERSION,
            expected_git_sha=GIT_SHA,
            strict=True,
        ),
    )

    set_item(record, "serial_boot", "PASS", "12 秒启动日志无异常", now=NOW)
    serial_item = record["items"]["serial_boot"]
    assert serial_item["status"] == "PASS"
    assert serial_item["checked_at"] == NOW
    assert serial_item["evidence"] == "12 秒启动日志无异常"
    assert serial_item["priority"] == "P0"
    assert serial_item["kind"] == "auto"

    complete_p0(record)
    validate_record(
        record,
        expected_version=VERSION,
        expected_git_sha=GIT_SHA,
        strict=True,
    )
    summary = summarize_record(record)
    assert summary["p0"] == {"pass": len(P0_ITEM_IDS), "fail": 0, "not_run": 0}
    print(f"{TAG} PASS record model")


def test_rejections():
    record = new_record(VERSION, GIT_SHA, "generic-c3", "tester", now=NOW)
    expect_error(
        "固件版本不匹配",
        lambda: validate_record(
            record,
            expected_version="1.0.0",
            expected_git_sha=GIT_SHA,
        ),
    )
    expect_error(
        "Git ID 不匹配",
        lambda: validate_record(
            record,
            expected_version=VERSION,
            expected_git_sha="deadbee",
        ),
    )
    expect_error(
        "未知验收项",
        lambda: set_item(record, "missing_item", "PASS", "bad", now=NOW),
    )
    expect_error(
        "非法状态",
        lambda: set_item(record, "serial_boot", "SKIP", "bad", now=NOW),
    )
    expect_error(
        "不支持的板型",
        lambda: new_record(VERSION, GIT_SHA, "esp32-s3", "tester", now=NOW),
    )
    print(f"{TAG} PASS invalid records rejected")


def test_atomic_file_round_trip():
    record = new_record(VERSION, GIT_SHA, "m5stamp-c3", "tester", now=NOW)
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "nested" / "device-acceptance.json"
        write_record(path, record)
        loaded = load_record(path)
        assert loaded == record
        assert json.loads(path.read_text(encoding="utf-8")) == record
        assert not list(path.parent.glob("*.tmp"))
    print(f"{TAG} PASS atomic file round trip")


def test_cli():
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "device-acceptance.json"
        script = TOOLS / "device_acceptance.py"
        init = subprocess.run(
            [
                sys.executable,
                str(script),
                "init",
                "--file",
                str(path),
                "--board",
                "generic-c3",
                "--operator",
                "tester",
                "--firmware-version",
                VERSION,
                "--git-sha",
                GIT_SHA,
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=20,
        )
        assert init.returncode == 0, init.stdout + init.stderr

        update = subprocess.run(
            [
                sys.executable,
                str(script),
                "set",
                "--file",
                str(path),
                "--item",
                "led_patterns",
                "--status",
                "PASS",
                "--evidence",
                "all patterns observed",
                "--firmware-version",
                VERSION,
                "--git-sha",
                GIT_SHA,
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=20,
        )
        assert update.returncode == 0, update.stdout + update.stderr

        validation = subprocess.run(
            [
                sys.executable,
                str(script),
                "validate",
                "--file",
                str(path),
                "--firmware-version",
                VERSION,
                "--git-sha",
                GIT_SHA,
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=20,
        )
        assert validation.returncode == 0, validation.stdout + validation.stderr
        assert "P0" in validation.stdout

        strict = subprocess.run(
            [
                sys.executable,
                str(script),
                "validate",
                "--strict",
                "--file",
                str(path),
                "--firmware-version",
                VERSION,
                "--git-sha",
                GIT_SHA,
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=20,
        )
        assert strict.returncode != 0
        assert "P0 验收未通过" in (strict.stdout + strict.stderr)
    print(f"{TAG} PASS CLI")


def main():
    test_record_model()
    test_rejections()
    test_atomic_file_round_trip()
    test_cli()
    print(f"{TAG} PASS: device acceptance evidence tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
