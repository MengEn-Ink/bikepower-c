#!/usr/bin/env python3
"""通过 BikePower Config Service 临时切换 GATT profile 并执行真机自测。

不启动 WiFi；结束时恢复测试前五组完整配置，适合无人值守 device gate。
"""

import argparse
import asyncio
import os
import subprocess
import sys

from ble_config_e2e import (
    ROOT,
    RESTART_GATT,
    RESULT_SUCCESS,
    ConfigE2EError,
    DeviceSession,
    expected_firmware_name,
    reconnect,
    require,
    validate_groups,
    verify_firmware,
    verify_snapshot,
)


ALL_BROADCAST_FIELDS = 0x07
SPORT_PROFILES = {"cycling": 0, "running": 1}


def make_profile_groups(original, profile):
    require(profile in SPORT_PROFILES, f"未知 profile: {profile}")
    changed = dict(original)
    core = bytearray(original["core"])
    core[13] = SPORT_PROFILES[profile]
    core[14] = ALL_BROADCAST_FIELDS
    changed["core"] = bytes(core)
    validate_groups(changed)
    return changed


def selftest_command(profile, duration, target_power):
    command = [sys.executable, "tests/ble_selftest.py", "--profile", profile]
    if profile == "running":
        command.extend(["--control", str(duration)])
    else:
        command.extend([str(duration), str(target_power)])
    return command


async def restore_original(session, original, settle_seconds):
    await session.connect()
    meta, current = await session.read_snapshot()
    if current != original:
        response = await session.commit_all(original, meta["revision"])
        require(response["result"] == RESULT_SUCCESS, f"恢复原 profile 配置失败: {response}")
        if response["restart_flags"] & RESTART_GATT:
            await reconnect(session, settle_seconds)
    await verify_snapshot(session, original, "profile harness 恢复后读取")


async def run_harness(args):
    session = DeviceSession(args.device_name, args.scan_timeout)
    original = None
    primary_error = None
    cleanup_error = None
    test_rc = 1
    try:
        await session.connect()
        await verify_firmware(session, expected_firmware_name(args.board))
        meta, original = await session.read_snapshot()
        target = make_profile_groups(original, args.profile)
        response = await session.commit_all(target, meta["revision"])
        require(response["result"] == RESULT_SUCCESS, f"切换 {args.profile} 失败: {response}")
        await session.disconnect()
        await asyncio.sleep(args.settle_seconds)

        command = selftest_command(args.profile, args.duration, args.target_power)
        env = dict(os.environ)
        if args.board:
            env["BIKEPOWER_EXPECT_BOARD"] = args.board
        print("[device-profile-ble] 执行:", " ".join(command))
        completed = await asyncio.to_thread(
            subprocess.run,
            command,
            cwd=str(ROOT),
            env=env,
            check=False,
        )
        test_rc = completed.returncode
        require(test_rc == 0, f"{args.profile} selftest rc={test_rc}")
    except Exception as exc:
        primary_error = exc
    finally:
        if original is not None:
            try:
                await restore_original(session, original, args.settle_seconds)
            except Exception as exc:
                cleanup_error = exc
        await session.disconnect()

    if primary_error is not None:
        raise ConfigE2EError(
            f"profile 自测失败: {type(primary_error).__name__}: {primary_error}"
            + (
                f"；且恢复失败: {type(cleanup_error).__name__}: {cleanup_error}"
                if cleanup_error
                else ""
            )
        )
    if cleanup_error is not None:
        raise ConfigE2EError(
            f"profile 自测通过但恢复失败: {type(cleanup_error).__name__}: {cleanup_error}"
        )
    print(f"[device-profile-ble] PASS profile={args.profile}，完整原配置已恢复")


def parse_args():
    parser = argparse.ArgumentParser(description="通过 BLE Config Service 临时切换 profile")
    parser.add_argument("--profile", choices=tuple(SPORT_PROFILES), required=True)
    parser.add_argument("--board", choices=("generic-c3", "m5stamp-c3"), default="")
    parser.add_argument("--device-name", default="BikePower")
    parser.add_argument("--scan-timeout", type=float, default=20.0)
    parser.add_argument("--settle-seconds", type=float, default=4.0)
    parser.add_argument("--duration", type=int, default=12)
    parser.add_argument("--target-power", type=int, default=250)
    return parser.parse_args()


def main():
    args = parse_args()
    try:
        asyncio.run(run_harness(args))
    except Exception as exc:
        print(f"[device-profile-ble] FAIL {type(exc).__name__}: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
