#!/usr/bin/env python3
"""Offline tests for profile switching, selftest argv and restoration."""
import sys
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests"))

from device_profile_harness import run_profile_harness  # noqa: E402


class Completed:
    def __init__(self, returncode=0):
        self.returncode = returncode


def run_case(profile, selftest_rc=0, raise_selftest=False):
    calls = []

    def request_fn(base_url, path, payload=None):
        calls.append(("request", base_url, path, payload))
        if path == "/api/state":
            return {
                "sport_profile": "cycling",
                "broadcast_mask": 5,
                "run_speed_x100": 987,
                "run_pace_float_sec": 11,
                "run_cadence": 171,
                "run_cadence_float": 3,
                "run_mode": "pacer",
                "run_pacer_type": "tempo",
                "run_pace_low_sec": 300,
                "run_pace_high_sec": 360,
                "run_custom_name": "原始跑步配置",
            }
        return {"ok": True}

    def set_profile_fn(base_url, next_profile, mask, request_fn):
        del request_fn
        calls.append(("set", base_url, next_profile, mask))

    def restore_config_fn(base_url, payload, request_fn):
        del request_fn
        calls.append(("restore", base_url, payload))

    def run_fn(command, cwd, check):
        calls.append(("run", command, cwd, check))
        if raise_selftest:
            raise RuntimeError("fake selftest failure")
        return Completed(selftest_rc)

    rc = run_profile_harness(
        profile=profile,
        settle_sec=0,
        request_fn=request_fn,
        set_profile_fn=set_profile_fn,
        restore_config_fn=restore_config_fn,
        run_fn=run_fn,
        sleep_fn=lambda _seconds: None,
    )
    expected_rc = 1 if raise_selftest else selftest_rc
    assert rc == expected_rc
    set_calls = [item for item in calls if item[0] == "set"]
    assert set_calls[0][2:] == (profile, 7), "测试阶段必须强制开启全部广播项"
    restore_call = next(item for item in calls if item[0] == "restore")
    restore_payload = restore_call[2]
    assert restore_payload["sport_profile"] == "cycling"
    assert restore_payload["broadcast_mask"] == 5
    assert restore_payload["run_speed_x100"] == 987
    assert restore_payload["run_mode"] == "pacer"
    assert restore_payload["run_pacer_type"] == "tempo"
    assert restore_payload["run_custom_name"] == "原始跑步配置"
    command = next(item[1] for item in calls if item[0] == "run")
    assert command[command.index("--profile") + 1] == profile
    if profile == "running":
        assert "--control" in command, "running 真机门禁必须覆盖 Start/Stop/Reset"
    else:
        assert "--control" not in command


def main():
    run_case("cycling")
    run_case("running")
    with redirect_stdout(StringIO()):
        run_case("running", selftest_rc=3)
        run_case("running", raise_selftest=True)
    print("[device-profile-harness] PASS: full mask, argv and success/failure restoration")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
