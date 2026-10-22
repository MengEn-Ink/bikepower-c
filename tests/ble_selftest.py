#!/usr/bin/env python3
"""BikePower C — BLE 自测脚本（Mac CoreBluetooth via bleak）

扫描 BikePower → 连接 → 校验五服务 → 读取 DIS 设备信息（厂商/型号/固件版本）
→ 订阅 CPS/HR/CSC/FTMS 通知。
再通过 FTMS 控制点(0x2AD9)下发 RequestControl + SetTargetPower(ERG)，
校验控制点 indication 响应，并确认 CPS 功率收敛到目标值附近。
用法:
  python tests/ble_selftest.py [秒数] [目标功率] [期望固件版本]
  python tests/ble_selftest.py --profile running [秒数] [期望固件版本]
  期望固件版本省略时自动从 main/bikepower_config.h 的 FIRMWARE_VERSION 解析。
"""
import asyncio
import os
import re
import struct
import sys

from bleak import BleakScanner, BleakClient
from ble_protocol import parse_cps, parse_rsc, parse_treadmill

DEVICE_NAME = "BikePower"
CPS_UUID = "00002a63-0000-1000-8000-00805f9b34fb"
HR_UUID = "00002a37-0000-1000-8000-00805f9b34fb"
CSC_UUID = "00002a5b-0000-1000-8000-00805f9b34fb"
FTMS_BIKE_UUID = "00002ad2-0000-1000-8000-00805f9b34fb"
FTMS_TREADMILL_UUID = "00002acd-0000-1000-8000-00805f9b34fb"
FTMS_FEATURE_UUID = "00002acc-0000-1000-8000-00805f9b34fb"
FTMS_CP_UUID = "00002ad9-0000-1000-8000-00805f9b34fb"
RSC_UUID = "00002a53-0000-1000-8000-00805f9b34fb"
RSC_FEATURE_UUID = "00002a54-0000-1000-8000-00805f9b34fb"
SENSOR_LOCATION_UUID = "00002a5d-0000-1000-8000-00805f9b34fb"
DIS_MANUF_UUID = "00002a29-0000-1000-8000-00805f9b34fb"
DIS_MODEL_UUID = "00002a24-0000-1000-8000-00805f9b34fb"
DIS_FW_UUID = "00002a26-0000-1000-8000-00805f9b34fb"

def get_expected_board_name(repo_root):
    env_board = os.environ.get("BIKEPOWER_EXPECT_BOARD", "")
    if env_board:
        aliases = {
            "generic": "generic-c3",
            "generic-c3": "generic-c3",
            "m5stamp": "m5stamp-c3",
            "m5stamp-c3": "m5stamp-c3",
        }
        return aliases.get(env_board, env_board)
    sdkconfig_path = os.path.join(repo_root, "sdkconfig")
    config_path = os.path.join(repo_root, "main", "bikepower_config.h")
    try:
        with open(config_path, "r", encoding="utf-8") as f:
            content = f.read()
        generic = re.search(r'#define\s+BOARD_NAME_GENERIC\s+"([^"]+)"', content)
        m5stamp = re.search(r'#define\s+BOARD_NAME_M5STAMP\s+"([^"]+)"', content)
        with open(sdkconfig_path, "r", encoding="utf-8") as f:
            sdkconfig = f.read()
        if "CONFIG_BOARD_M5STAMP_C3=y" in sdkconfig and m5stamp:
            return m5stamp.group(1)
        if "CONFIG_BOARD_GENERIC_C3=y" in sdkconfig and generic:
            return generic.group(1)
    except Exception:
        pass
    return "generic-c3"

def get_expected_fw_version():
    """从bikepower_config.h自动解析固件版本，保持单一事实源"""
    repo_root = os.path.dirname(os.path.dirname(__file__))
    config_path = os.path.join(repo_root, "main", "bikepower_config.h")
    try:
        with open(config_path, "r", encoding="utf-8") as f:
            content = f.read()
        version = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', content)
        stable = re.search(r'#define\s+FIRMWARE_STABLE_VERSION\s+"([^"]+)"', content)
        fix = re.search(r'#define\s+FIRMWARE_FIX_VERSION\s+"([^"]+)"', content)
        if version and stable and fix:
            board = get_expected_board_name(repo_root)
            git_sha = os.environ.get("BIKEPOWER_EXPECT_GIT_SHA", "")
            if not git_sha:
                try:
                    import subprocess
                    git_sha = subprocess.check_output(["git", "-C", repo_root, "rev-parse", "--short", "HEAD"], text=True).strip()
                except Exception:
                    git_sha = "nogit"
            return "%s-v%s-stable%s-fix%s-g%s" % (board, version.group(1), stable.group(1), fix.group(1), git_sha)
    except Exception:
        pass
    return "3.1.1"  # fallback默认值

PROFILE = "cycling"
RUN_CONTROL = False
args = sys.argv[1:]
if args[:2] == ["--profile", "running"]:
    PROFILE = "running"
    args = args[2:]
elif args[:2] == ["--profile", "cycling"]:
    args = args[2:]
if "--control" in args:
    RUN_CONTROL = True
    args = [a for a in args if a != "--control"]

DURATION = int(args[0]) if len(args) > 0 else 8
TARGET_POWER = int(args[1]) if PROFILE == "cycling" and len(args) > 1 else 200
EXPECTED_FW_VERSION = (
    args[2] if PROFILE == "cycling" and len(args) > 2
    else args[1] if PROFILE == "running" and len(args) > 1
    else get_expected_fw_version()
)

DIS_EXPECT = {
    DIS_MANUF_UUID: ("厂商", "BikePower"),
    DIS_MODEL_UUID: ("型号", "ESP32-C3"),
    DIS_FW_UUID: ("固件", EXPECTED_FW_VERSION),
}

OP_REQUEST_CONTROL = 0x00
OP_RESET = 0x01
OP_SET_TARGET_SPEED = 0x02
OP_SET_TARGET_POWER = 0x05
OP_START_RESUME = 0x07
OP_STOP_PAUSE = 0x08

# FTMS Control Point Response 结果码
CP_RESULT_SUCCESS = 0x01
CP_RESULT_NOT_SUPPORTED = 0x02
CP_RESULT_INVALID_PARAM = 0x03
CP_RESULT_OPERATION_FAILED = 0x04


async def write_ftms_cp_and_wait(client, responses, op_code, payload=b"", timeout=3.0):
    """写入 FTMS 控制点并等待同一操作码的 indication，避免固定 sleep 假绿。"""
    start = len(responses)
    await client.write_gatt_char(
        FTMS_CP_UUID,
        bytes([op_code]) + payload,
        response=True,
    )
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while loop.time() < deadline:
        for response in responses[start:]:
            if len(response) >= 3 and response[0] == 0x80 and response[1] == op_code:
                return response[2]
        await asyncio.sleep(0.05)
    raise TimeoutError(f"FTMS op=0x{op_code:02x} 在 {timeout:.1f}s 内无响应")


async def collect_running_window(rsc_samples, treadmill_samples, seconds, settle_seconds=0.0):
    """丢弃状态迁移样本后，返回一个独立通知观察窗口。"""
    if settle_seconds > 0:
        await asyncio.sleep(settle_seconds)
    rsc_start = len(rsc_samples)
    treadmill_start = len(treadmill_samples)
    await asyncio.sleep(seconds)
    return rsc_samples[rsc_start:], treadmill_samples[treadmill_start:]


def values_strictly_grow(samples, field):
    return len(samples) >= 2 and all(
        current[field] > previous[field]
        for previous, current in zip(samples, samples[1:])
    )


def values_are_frozen(samples, field):
    return len(samples) >= 2 and all(item[field] == samples[0][field] for item in samples[1:])


async def check_ftms_feature(client, found, expected_machine_mask, expected_target_mask):
    if FTMS_FEATURE_UUID not in found:
        print("  FTMS Feature: 无特征")
        return False
    try:
        data = bytes(await client.read_gatt_char(FTMS_FEATURE_UUID))
    except Exception as e:
        print(f"  FTMS Feature: 读取失败 {type(e).__name__}")
        return False
    if len(data) < 8:
        print(f"  FTMS Feature: 长度异常 {len(data)}")
        return False
    machine = struct.unpack_from("<I", data, 0)[0]
    target = struct.unpack_from("<I", data, 4)[0]
    ok_machine = (machine & expected_machine_mask) == expected_machine_mask
    ok_target = (target & expected_target_mask) == expected_target_mask
    print(
        "  FTMS Feature: machine=0x%08x target=0x%08x %s"
        % (machine, target, "通过" if ok_machine and ok_target else "失败")
    )
    return ok_machine and ok_target


async def run_running_profile(client, found):
    services = [
        ("RSC", RSC_UUID),
        ("RSC-Feature", RSC_FEATURE_UUID),
        ("SensorLocation", SENSOR_LOCATION_UUID),
        ("HR", HR_UUID),
        ("FTMS-Treadmill", FTMS_TREADMILL_UUID),
        ("FTMS-CP", FTMS_CP_UUID),
    ]
    missing = []
    for label, uuid in services:
        present = uuid in found
        print(f"  特征 {label}: {'有' if present else '无'}")
        if not present:
            missing.append(label)

    dis_ok = await check_dis(client, found)
    ftms_feature_ok = await check_ftms_feature(
        client,
        found,
        expected_machine_mask=0x00000504,
        expected_target_mask=0x00000002,
    )
    rsc_feature_ok = False
    if RSC_FEATURE_UUID in found:
        try:
            rsc_feature = bytes(await client.read_gatt_char(RSC_FEATURE_UUID))
            rsc_feature_ok = rsc_feature == bytes.fromhex("0700")
            print(
                f"  RSC Feature: {rsc_feature.hex()} "
                f"{'通过' if rsc_feature_ok else '失败(期望0700)'}"
            )
        except Exception as exc:
            print(f"  RSC Feature: 读取失败 {type(exc).__name__}")
    samples = []
    treadmill_samples = []
    cp_responses = []
    parse_errors = []
    hr_count = [0]

    def cb_rsc(_, data):
        try:
            samples.append(parse_rsc(bytes(data)))
        except ValueError as exc:
            parse_errors.append(f"RSC: {exc}")

    def cb_hr(_, data):
        hr_count[0] += 1

    def cb_treadmill(_, data):
        try:
            treadmill_samples.append(parse_treadmill(bytes(data)))
        except ValueError as exc:
            parse_errors.append(f"FTMS Treadmill: {exc}")

    def cb_cp(_, data):
        b = bytes(data)
        cp_responses.append(b)
        print(f"  FTMS-CP 响应: {b.hex()}")

    if RSC_UUID in found:
        await client.start_notify(RSC_UUID, cb_rsc)
    if HR_UUID in found:
        await client.start_notify(HR_UUID, cb_hr)
    if FTMS_TREADMILL_UUID in found:
        await client.start_notify(FTMS_TREADMILL_UUID, cb_treadmill)
    if FTMS_CP_UUID in found:
        await client.start_notify(FTMS_CP_UUID, cb_cp)

    expected_speed = 12.0 if RUN_CONTROL else 10.0
    control_results = {}
    control_errors = []
    moving_rsc = []
    moving_treadmill = []
    paused_rsc = []
    paused_treadmill = []
    reset_rsc = []
    reset_treadmill = []
    resumed_rsc = []
    resumed_treadmill = []

    if RUN_CONTROL and FTMS_CP_UUID in found:
        try:
            commands = [
                ("RequestControl", OP_REQUEST_CONTROL, b"", CP_RESULT_SUCCESS),
                ("StartResume", OP_START_RESUME, b"", CP_RESULT_SUCCESS),
                (
                    "SetTargetPower",
                    OP_SET_TARGET_POWER,
                    struct.pack("<h", 200),
                    CP_RESULT_NOT_SUPPORTED,
                ),
                (
                    "SetTargetSpeed",
                    OP_SET_TARGET_SPEED,
                    struct.pack("<H", 1200),
                    CP_RESULT_SUCCESS,
                ),
            ]
            for label, opcode, payload, expected_result in commands:
                print(f"下发 running {label}(0x{opcode:02x})")
                result = await write_ftms_cp_and_wait(
                    client, cp_responses, opcode, payload
                )
                control_results[label] = result
                if result != expected_result:
                    control_errors.append(
                        f"{label}=0x{result:02x}, 期望0x{expected_result:02x}"
                    )

            print(f"采集 moving 阶段通知 {max(DURATION, 3)}s ...")
            moving_rsc, moving_treadmill = await collect_running_window(
                samples, treadmill_samples, max(DURATION, 3), settle_seconds=1.1
            )

            print("下发 running StopPause(0x08)")
            stop_result = await write_ftms_cp_and_wait(
                client, cp_responses, OP_STOP_PAUSE
            )
            control_results["StopPause"] = stop_result
            if stop_result != CP_RESULT_SUCCESS:
                control_errors.append(
                    f"StopPause=0x{stop_result:02x}, 期望0x{CP_RESULT_SUCCESS:02x}"
                )
            paused_rsc, paused_treadmill = await collect_running_window(
                samples, treadmill_samples, 2.2, settle_seconds=1.1
            )

            print("下发 running Reset(0x01)，暂停态应保持")
            reset_result = await write_ftms_cp_and_wait(
                client, cp_responses, OP_RESET
            )
            control_results["Reset"] = reset_result
            if reset_result != CP_RESULT_SUCCESS:
                control_errors.append(
                    f"Reset=0x{reset_result:02x}, 期望0x{CP_RESULT_SUCCESS:02x}"
                )
            reset_rsc, reset_treadmill = await collect_running_window(
                samples, treadmill_samples, 2.2, settle_seconds=1.1
            )

            print("下发 running StartResume(0x07)，累计应重新推进")
            resume_result = await write_ftms_cp_and_wait(
                client, cp_responses, OP_START_RESUME
            )
            control_results["Resume"] = resume_result
            if resume_result != CP_RESULT_SUCCESS:
                control_errors.append(
                    f"Resume=0x{resume_result:02x}, 期望0x{CP_RESULT_SUCCESS:02x}"
                )
            resumed_rsc, resumed_treadmill = await collect_running_window(
                samples, treadmill_samples, 3.2, settle_seconds=1.1
            )
        except Exception as exc:
            control_errors.append(f"{type(exc).__name__}: {exc}")
    elif RUN_CONTROL:
        control_errors.append("缺少 FTMS Control Point")
    else:
        print(f"采集 RSC/HRS/FTMS Treadmill 通知 {max(DURATION, 3)}s ...")
        moving_rsc, moving_treadmill = await collect_running_window(
            samples, treadmill_samples, max(DURATION, 3)
        )

    ok_notify = len(moving_rsc) >= 2
    ok_speed = ok_notify and abs(moving_rsc[-1]["speed_kmh"] - expected_speed) <= 0.25
    ok_cadence = ok_notify and 120 <= moving_rsc[-1]["cadence_spm"] <= 220
    ok_distance = values_strictly_grow(moving_rsc, "total_distance_m")
    ok_flags = ok_notify and all(item["flags"] == 0x07 and item["running"] for item in moving_rsc)
    ok_treadmill_notify = len(moving_treadmill) >= 2
    ok_treadmill_speed = (
        ok_treadmill_notify
        and abs(moving_treadmill[-1]["speed_x100_kmh"] / 100.0 - expected_speed) <= 0.25
    )
    ok_treadmill_distance = values_strictly_grow(moving_treadmill, "total_distance_m")
    ok_treadmill_hr = (
        ok_treadmill_notify
        and 60 <= moving_treadmill[-1]["heart_rate_bpm"] <= 200
    )
    ok_treadmill_elapsed = values_strictly_grow(moving_treadmill, "elapsed_time_s")

    ok_pause = True
    ok_reset = True
    ok_resume = True
    if RUN_CONTROL:
        ok_pause = (
            len(paused_rsc) >= 2
            and len(paused_treadmill) >= 2
            and all(
                not item["running"]
                and item["flags"] == 0x03
                and item["speed_1_256_mps"] == 0
                and item["cadence_spm"] == 0
                and item["stride_cm"] == 0
                for item in paused_rsc
            )
            and values_are_frozen(paused_rsc, "total_distance_dm")
            and all(item["speed_x100_kmh"] == 0 for item in paused_treadmill)
            and values_are_frozen(paused_treadmill, "total_distance_m")
            and values_are_frozen(paused_treadmill, "elapsed_time_s")
        )
        ok_reset = (
            len(reset_rsc) >= 2
            and len(reset_treadmill) >= 2
            and all(
                not item["running"]
                and item["speed_1_256_mps"] == 0
                and item["cadence_spm"] == 0
                and item["stride_cm"] == 0
                and item["total_distance_dm"] == 0
                for item in reset_rsc
            )
            and all(
                item["speed_x100_kmh"] == 0
                and item["total_distance_m"] == 0
                and item["elapsed_time_s"] == 0
                for item in reset_treadmill
            )
        )
        ok_resume = (
            len(resumed_rsc) >= 2
            and len(resumed_treadmill) >= 2
            and all(item["running"] and item["flags"] == 0x07 for item in resumed_rsc)
            and abs(resumed_rsc[-1]["speed_kmh"] - expected_speed) <= 0.25
            and values_strictly_grow(resumed_rsc, "total_distance_dm")
            and resumed_treadmill[-1]["speed_x100_kmh"] > 0
            and values_strictly_grow(resumed_treadmill, "total_distance_m")
            and values_strictly_grow(resumed_treadmill, "elapsed_time_s")
        )

    ok_services = not missing
    ok_cp_control = not RUN_CONTROL or not control_errors
    ok = (
        ok_services and dis_ok and ftms_feature_ok and rsc_feature_ok
        and ok_notify and ok_speed and ok_cadence and ok_distance and ok_flags
        and ok_treadmill_notify and ok_treadmill_speed and ok_treadmill_distance
        and ok_treadmill_hr and ok_treadmill_elapsed
        and ok_cp_control and ok_pause and ok_reset and ok_resume and not parse_errors
    )
    print("--- 结果 ---")
    print(f"  跑步服务齐全: {'通过' if ok_services else '失败(' + ','.join(missing) + ')'}")
    print(f"  DIS 设备信息: {'通过' if dis_ok else '失败'}")
    print(f"  FTMS Feature: {'通过' if ftms_feature_ok else '失败'}")
    print(f"  RSC Feature: {'通过' if rsc_feature_ok else '失败'}")
    print(f"  RSC 通知: {'通过' if ok_notify else '失败'}")
    print(f"  RSC 速度: {'通过' if ok_speed else '失败'}")
    print(f"  RSC 步频: {'通过' if ok_cadence else '失败'}")
    print(f"  RSC 距离: {'通过' if ok_distance else '失败'}")
    print(f"  RSC flags: {'通过' if ok_flags else '失败'}")
    print(f"  FTMS Treadmill 通知: {'通过' if ok_treadmill_notify else '失败'}")
    print(f"  FTMS Treadmill 速度: {'通过' if ok_treadmill_speed else '失败'}")
    print(f"  FTMS Treadmill 距离: {'通过' if ok_treadmill_distance else '失败'}")
    print(f"  FTMS Treadmill 心率: {'通过' if ok_treadmill_hr else '失败'}")
    print(f"  FTMS Treadmill 用时: {'通过' if ok_treadmill_elapsed else '失败'}")
    print(
        f"  FTMS 跑步控制点: {'通过' if ok_cp_control else '失败(' + '; '.join(control_errors[:3]) + ')'}"
    )
    if RUN_CONTROL:
        print(f"  Stop 后零值/累计冻结: {'通过' if ok_pause else '失败'}")
        print(f"  暂停态 Reset 清零且保持暂停: {'通过' if ok_reset else '失败'}")
        print(f"  Resume 后速度/累计恢复: {'通过' if ok_resume else '失败'}")
        print(
            "  阶段样本数: moving=%d/%d paused=%d/%d reset=%d/%d resumed=%d/%d"
            % (
                len(moving_rsc), len(moving_treadmill),
                len(paused_rsc), len(paused_treadmill),
                len(reset_rsc), len(reset_treadmill),
                len(resumed_rsc), len(resumed_treadmill),
            )
        )
    print(f"  BLE payload 解析: {'通过' if not parse_errors else '失败(' + '; '.join(parse_errors[:3]) + ')'}")
    print("总判定:", "通过" if ok else "失败")
    return 0 if ok else 1


async def check_dis(client, found):
    dis_ok = True
    print("读取 DIS 设备信息 ...")
    for uuid, (label, expect) in DIS_EXPECT.items():
        if uuid not in found:
            print(f"  DIS {label}: 无特征")
            dis_ok = False
            continue
        try:
            val = (await client.read_gatt_char(uuid)).decode("utf-8", "replace")
            match = val == expect
            print(f"  DIS {label}: '{val}' (期望 '{expect}') {'通过' if match else '失败'}")
            dis_ok = dis_ok and match
        except Exception as e:
            print(f"  DIS {label}: 读取失败 {type(e).__name__}")
            dis_ok = False
    return dis_ok


async def main():
    print(f"扫描 {DEVICE_NAME} ...")
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
    if not dev:
        # 模糊匹配：设备名可能带后缀（如 BikePower23）
        devices = await BleakScanner.discover(timeout=5)
        for d in devices:
            if d.name and d.name.startswith(DEVICE_NAME):
                dev = d
                break
    if not dev:
        print("未发现设备")
        return 1
    print(f"发现 {dev.name} {dev.address}，连接中...")
    async with BleakClient(dev) as client:
        print("已连接")
        found = {c.uuid for s in client.services for c in s.characteristics}
        if PROFILE == "running":
            return await run_running_profile(client, found)

        services = [
            ("CPS", CPS_UUID),
            ("HR", HR_UUID),
            ("CSC", CSC_UUID),
            ("FTMS-Bike", FTMS_BIKE_UUID),
            ("FTMS-CP", FTMS_CP_UUID),
        ]
        missing = []
        for label, uuid in services:
            present = uuid in found
            print(f"  特征 {label}: {'有' if present else '无'}")
            if not present:
                missing.append(label)

        dis_ok = await check_dis(client, found)
        ftms_feature_ok = await check_ftms_feature(
            client,
            found,
            expected_machine_mask=0x00004002,
            expected_target_mask=0x00000008,
        )

        counts = {"cps": 0, "hr": 0, "csc": 0, "ftms": 0}
        last_power = [0]
        cp_responses = []
        parse_errors = []

        def cb_cps(_, data):
            counts["cps"] += 1
            try:
                last_power[0] = parse_cps(bytes(data))["power_w"]
            except ValueError as exc:
                parse_errors.append(f"CPS: {exc}")

        def cb_hr(_, data):
            counts["hr"] += 1

        def cb_csc(_, data):
            counts["csc"] += 1

        def cb_ftms(_, data):
            counts["ftms"] += 1

        def cb_cp(_, data):
            b = bytes(data)
            cp_responses.append(b)
            print(f"  FTMS-CP 响应: {b.hex()}")

        await client.start_notify(CPS_UUID, cb_cps)
        if HR_UUID in found:
            await client.start_notify(HR_UUID, cb_hr)
        if CSC_UUID in found:
            await client.start_notify(CSC_UUID, cb_csc)
        if FTMS_BIKE_UUID in found:
            await client.start_notify(FTMS_BIKE_UUID, cb_ftms)
        if FTMS_CP_UUID in found:
            await client.start_notify(FTMS_CP_UUID, cb_cp)

        print("采集基线 3s ...")
        await asyncio.sleep(3)
        baseline = last_power[0]
        print(f"基线功率 ~{baseline}W，目标 ERG={TARGET_POWER}W")

        if FTMS_CP_UUID in found:
            print("下发 RequestControl(0x00)")
            await client.write_gatt_char(FTMS_CP_UUID, bytes([OP_REQUEST_CONTROL]), response=True)
            await asyncio.sleep(0.5)
            print("下发 StartResume(0x07)")
            await client.write_gatt_char(FTMS_CP_UUID, bytes([OP_START_RESUME]), response=True)
            await asyncio.sleep(0.5)
            # 先测非法值（负值），期望 Invalid Param
            print(f"下发 SetTargetPower(0x05, -1) — 期望拒绝")
            cp_before = len(cp_responses)
            await client.write_gatt_char(
                FTMS_CP_UUID,
                bytes([OP_SET_TARGET_POWER]) + struct.pack("<h", -1),
                response=True,
            )
            await asyncio.sleep(0.5)
            # 再测合法值，期望 Success
            print(f"下发 SetTargetPower(0x05, {TARGET_POWER}) — 期望成功")
            await client.write_gatt_char(
                FTMS_CP_UUID,
                bytes([OP_SET_TARGET_POWER]) + struct.pack("<h", TARGET_POWER),
                response=True,
            )
            # 记录合法值下发后的 cp 响应索引位置
            cp_after_valid = len(cp_responses)
            _ = cp_after_valid  # 后面用 cp_responses 末尾判断

        print(f"采集 ERG 收敛 {DURATION}s ...")
        await asyncio.sleep(DURATION)
        erg_power = last_power[0]
        print(f"通知计数: {counts}")
        print(f"ERG 后功率 ~{erg_power}W (目标 {TARGET_POWER}W)")

        ok_notify = counts["cps"] > 0
        ok_services = not missing

        # FTMS CP 响应校验：检查响应码结构 + SetTargetPower 成功/失败
        ok_cp_resp = True
        cp_detail = ""
        if FTMS_CP_UUID in found:
            # 响应格式: 0x80 + request_op + result_code
            def find_cp_resp(op_code):
                for r in cp_responses:
                    if len(r) >= 3 and r[0] == 0x80 and r[1] == op_code:
                        return r[2]
                return None
            # RequestControl 应该成功
            rc = find_cp_resp(OP_REQUEST_CONTROL)
            if rc != CP_RESULT_SUCCESS:
                ok_cp_resp = False
                cp_detail += f"RequestControl结果={rc} "
            # SetTargetPower(-1) 应该返回 Invalid Param
            rc = find_cp_resp(OP_SET_TARGET_POWER)
            # 可能有多个 SetTargetPower 响应，找最后一个（合法值那次）应该是 Success
            # 先简单判断：至少有一个 SetTargetPower 响应
            stp_responses = [r for r in cp_responses if len(r) >= 3 and r[0] == 0x80 and r[1] == OP_SET_TARGET_POWER]
            if len(stp_responses) < 2:
                ok_cp_resp = False
                cp_detail += f"SetTargetPower响应数={len(stp_responses)} "
            else:
                # 第一次（-1）应该 Invalid Param，第二次（合法值）应该 Success
                first_result = stp_responses[0][2]
                last_result = stp_responses[-1][2]
                if first_result != CP_RESULT_INVALID_PARAM:
                    ok_cp_resp = False
                    cp_detail += f"首次SetTargetPower结果={first_result}(期望InvalidParam) "
                if last_result != CP_RESULT_SUCCESS:
                    ok_cp_resp = False
                    cp_detail += f"末次SetTargetPower结果={last_result}(期望Success) "
            if cp_detail == "":
                cp_detail = "全部通过"

        stop_power = None
        if FTMS_CP_UUID in found:
            print("下发 Stop/Pause(0x08) — 期望退出 ERG")
            await client.write_gatt_char(FTMS_CP_UUID, bytes([OP_STOP_PAUSE]), response=True)
            await asyncio.sleep(3)
            stop_power = last_power[0]
            print(f"Stop/Pause 后功率 ~{stop_power}W")

        ok_erg = abs(erg_power - TARGET_POWER) <= 20 if FTMS_CP_UUID in found else True
        ok_stop_resp = True
        if FTMS_CP_UUID in found and stop_power is not None:
            stop_responses = [r for r in cp_responses if len(r) >= 3 and r[0] == 0x80 and r[1] == OP_STOP_PAUSE]
            ok_stop_resp = len(stop_responses) > 0 and stop_responses[-1][2] == CP_RESULT_SUCCESS

        print("--- 结果 ---")
        print(f"  五服务齐全: {'通过' if ok_services else '失败(' + ','.join(missing) + ')'}")
        print(f"  DIS 设备信息: {'通过' if dis_ok else '失败'}")
        print(f"  FTMS Feature: {'通过' if ftms_feature_ok else '失败'}")
        print(f"  CPS 通知:   {'通过' if ok_notify else '失败'}")
        print(f"  FTMS-CP 响应: {'通过' if ok_cp_resp else '失败(' + cp_detail + ')'}")
        print(f"  ERG 功率收敛: {'通过' if ok_erg else '失败'}")
        print(f"  Stop/Pause 响应: {'通过' if ok_stop_resp else '失败'}")
        print(f"  BLE payload 解析: {'通过' if not parse_errors else '失败(' + '; '.join(parse_errors[:3]) + ')'}")
        ok = ok_notify and ok_services and ftms_feature_ok and ok_cp_resp and ok_erg and ok_stop_resp and dis_ok and not parse_errors
        print("总判定:", "通过" if ok else "失败")
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
