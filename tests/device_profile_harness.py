#!/usr/bin/env python3
"""Profile-aware device harness for BLE cycling/running validation.

单个固件运行时只能暴露 cycling 或 running GATT 表。本 harness 通过仍在运行的
配网 API 临时切换 profile，等待 BLE 重建后执行对应 selftest，并在 finally 中恢复
原 profile、broadcast mask 与跑步持久配置，避免门禁污染用户配置。
"""
import argparse
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALL_BROADCAST_FIELDS = 7
RUNNING_CONFIG_KEYS = (
    "run_speed_x100",
    "run_pace_float_sec",
    "run_cadence",
    "run_cadence_float",
    "run_mode",
    "run_pacer_type",
    "run_pace_low_sec",
    "run_pace_high_sec",
    "run_custom_name",
)


def request_json(base_url, path, payload=None, timeout=5):
    url = base_url.rstrip("/") + path
    headers = {"Connection": "close"}
    if payload is None:
        request = urllib.request.Request(url, headers=headers)
    else:
        body = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
        request = urllib.request.Request(url, data=body, method="POST", headers=headers)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def set_profile(base_url, profile, broadcast_mask, request_fn=request_json):
    result = request_fn(
        base_url,
        "/api/config",
        {"sport_profile": profile, "broadcast_mask": broadcast_mask},
    )
    if result.get("ok") is not True:
        raise RuntimeError(f"切换 profile={profile} 失败: {result}")


def restore_config(base_url, payload, request_fn=request_json):
    result = request_fn(base_url, "/api/config", payload)
    if result.get("ok") is not True:
        raise RuntimeError(f"恢复原始配置失败: {result}")


def selftest_command(profile, duration, target_power):
    command = [sys.executable, str(ROOT / "tests" / "ble_selftest.py"), "--profile", profile]
    if profile == "running":
        command.extend(["--control", str(duration)])
    else:
        command.extend([str(duration), str(target_power)])
    return command


def run_profile_harness(
    profile,
    base_url="http://192.168.4.1",
    settle_sec=4.0,
    duration=12,
    target_power=250,
    request_fn=request_json,
    set_profile_fn=set_profile,
    restore_config_fn=restore_config,
    run_fn=subprocess.run,
    sleep_fn=time.sleep,
):
    original = request_fn(base_url, "/api/state")
    original_profile = original.get("sport_profile")
    original_mask = original.get("broadcast_mask", ALL_BROADCAST_FIELDS) or ALL_BROADCAST_FIELDS
    if original_profile not in ("cycling", "running"):
        raise RuntimeError(f"/api/state 返回未知 sport_profile: {original_profile}")
    missing_keys = [key for key in RUNNING_CONFIG_KEYS if key not in original]
    if missing_keys:
        raise RuntimeError(f"/api/state 缺少跑步配置字段: {','.join(missing_keys)}")
    restore_payload = {
        "sport_profile": original_profile,
        "broadcast_mask": original_mask,
        **{key: original[key] for key in RUNNING_CONFIG_KEYS},
    }

    print(
        f"[device-profile] 原配置 profile={original_profile} mask={original_mask}; "
        f"测试 profile={profile}"
    )
    test_rc = 1
    primary_error = None
    restore_error = None
    try:
        set_profile_fn(base_url, profile, ALL_BROADCAST_FIELDS, request_fn=request_fn)
        sleep_fn(settle_sec)
        command = selftest_command(profile, duration, target_power)
        print("[device-profile] 执行:", " ".join(command))
        completed = run_fn(command, cwd=str(ROOT), check=False)
        test_rc = completed.returncode
    except Exception as exc:
        primary_error = exc
    finally:
        try:
            restore_config_fn(base_url, restore_payload, request_fn=request_fn)
            sleep_fn(settle_sec)
            print(
                f"[device-profile] 已恢复 profile={original_profile} mask={original_mask} "
                f"run_speed={restore_payload['run_speed_x100']} run_mode={restore_payload['run_mode']}"
            )
        except Exception as exc:
            restore_error = exc

    if primary_error is not None:
        print(f"[device-profile] FAIL: {type(primary_error).__name__}: {primary_error}")
        return 1
    if restore_error is not None:
        print(f"[device-profile] FAIL 恢复配置: {type(restore_error).__name__}: {restore_error}")
        return 1
    if test_rc != 0:
        print(f"[device-profile] FAIL selftest rc={test_rc}")
        return test_rc
    print(f"[device-profile] PASS profile={profile}")
    return 0


def main():
    parser = argparse.ArgumentParser(description="临时切换运动 profile 并执行 BLE 真机自测")
    parser.add_argument("--profile", choices=("cycling", "running"), required=True)
    parser.add_argument("--base-url", default="http://192.168.4.1")
    parser.add_argument("--settle-sec", type=float, default=4.0)
    parser.add_argument("--duration", type=int, default=12)
    parser.add_argument("--target-power", type=int, default=250)
    args = parser.parse_args()
    return run_profile_harness(
        profile=args.profile,
        base_url=args.base_url,
        settle_sec=args.settle_sec,
        duration=args.duration,
        target_power=args.target_power,
    )


if __name__ == "__main__":
    raise SystemExit(main())
