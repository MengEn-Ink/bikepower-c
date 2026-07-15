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


def parse_cps(data: bytes):
    flags, power = struct.unpack_from("<HH", data, 0)
    return flags, power

def parse_rsc(data: bytes):
    b = bytes(data)
    if len(b) < 4:
        return None
    flags = b[0]
    speed = struct.unpack_from("<H", b, 1)[0]
    cadence = b[3]
    off = 4
    stride = None
    distance = None
    if flags & 0x01 and len(b) >= off + 2:
        stride = struct.unpack_from("<H", b, off)[0]
        off += 2
    if flags & 0x02 and len(b) >= off + 4:
        distance = struct.unpack_from("<I", b, off)[0]
    return {
        "flags": flags,
        "speed": speed,
        "speed_kmh": speed * 3.6 / 256.0,
        "cadence": cadence,
        "stride": stride,
        "distance": distance,
        "running": bool(flags & 0x04),
    }

def parse_treadmill(data: bytes):
    b = bytes(data)
    if len(b) < 4:
        return None
    flags = struct.unpack_from("<H", b, 0)[0]
    speed = struct.unpack_from("<H", b, 2)[0]
    off = 4
    distance = None
    hr = None
    elapsed = None
    if flags & 0x0004:
        if len(b) < off + 3:
            return None
        distance = b[off] | (b[off + 1] << 8) | (b[off + 2] << 16)
        off += 3
    if flags & 0x0080:
        if len(b) < off + 4:
            return None
        off += 4
    if flags & 0x0100:
        if len(b) < off + 1:
            return None
        hr = b[off]
        off += 1
    if flags & 0x0200:
        if len(b) < off + 2:
            return None
        elapsed = struct.unpack_from("<H", b, off)[0]
    return {
        "flags": flags,
        "speed_x100": speed,
        "speed_kmh": speed / 100.0,
        "distance": distance,
        "hr": hr,
        "elapsed": elapsed,
    }


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
    samples = []
    treadmill_samples = []
    cp_responses = []
    hr_count = [0]

    def cb_rsc(_, data):
        parsed = parse_rsc(bytes(data))
        if parsed:
            samples.append(parsed)

    def cb_hr(_, data):
        hr_count[0] += 1

    def cb_treadmill(_, data):
        parsed = parse_treadmill(bytes(data))
        if parsed:
            treadmill_samples.append(parsed)

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

    if RUN_CONTROL and FTMS_CP_UUID in found:
        print("下发 running RequestControl(0x00)")
        await client.write_gatt_char(FTMS_CP_UUID, bytes([OP_REQUEST_CONTROL]), response=True)
        await asyncio.sleep(0.4)
        print("下发 running StartResume(0x07)")
        await client.write_gatt_char(FTMS_CP_UUID, bytes([OP_START_RESUME]), response=True)
        await asyncio.sleep(0.4)
        print("下发 running SetTargetPower(0x05, 200) — 期望不支持")
        await client.write_gatt_char(
            FTMS_CP_UUID,
            bytes([OP_SET_TARGET_POWER]) + struct.pack("<h", 200),
            response=True,
        )
        await asyncio.sleep(0.4)
        print("下发 running SetTargetSpeed(0x02, 12.00km/h)")
        await client.write_gatt_char(
            FTMS_CP_UUID,
            bytes([OP_SET_TARGET_SPEED]) + struct.pack("<H", 1200),
            response=True,
        )
        await asyncio.sleep(0.4)
    expected_speed = 12.0 if RUN_CONTROL else 10.0
    print(f"采集 RSC/HRS/FTMS Treadmill 通知 {DURATION}s ...")
    await asyncio.sleep(DURATION)

    ok_notify = len(samples) > 0
    ok_speed = False
    ok_cadence = False
    ok_distance = False
    ok_flags = False
    ok_treadmill_notify = len(treadmill_samples) > 0
    ok_treadmill_speed = False
    ok_treadmill_distance = False
    ok_treadmill_hr = False
    ok_treadmill_elapsed = False
    if samples:
        first = samples[0]
        last = samples[-1]
        ok_speed = abs(last["speed_kmh"] - expected_speed) <= 0.25
        ok_cadence = 120 <= last["cadence"] <= 220
        ok_distance = (
            first["distance"] is not None
            and last["distance"] is not None
            and last["distance"] >= first["distance"]
        )
        ok_flags = bool(last["flags"] & 0x01) and bool(last["flags"] & 0x02) and last["running"]
        print(
            "RSC 最后样本: speed=%.2fkm/h cadence=%dspm stride=%s distance=%s flags=0x%02x"
            % (
                last["speed_kmh"],
                last["cadence"],
                str(last["stride"]),
                str(last["distance"]),
                last["flags"],
            )
        )
    if treadmill_samples:
        first_tm = treadmill_samples[0]
        last_tm = treadmill_samples[-1]
        ok_treadmill_speed = abs(last_tm["speed_kmh"] - expected_speed) <= 0.25
        ok_treadmill_distance = (
            first_tm["distance"] is not None
            and last_tm["distance"] is not None
            and last_tm["distance"] >= first_tm["distance"]
        )
        ok_treadmill_hr = last_tm["hr"] is not None and 60 <= last_tm["hr"] <= 200
        ok_treadmill_elapsed = (
            first_tm["elapsed"] is not None
            and last_tm["elapsed"] is not None
            and last_tm["elapsed"] >= first_tm["elapsed"]
        )
        print(
            "FTMS Treadmill 最后样本: speed=%.2fkm/h distance=%s hr=%s elapsed=%s flags=0x%04x"
            % (
                last_tm["speed_kmh"],
                str(last_tm["distance"]),
                str(last_tm["hr"]),
                str(last_tm["elapsed"]),
                last_tm["flags"],
            )
        )
    print(f"通知计数: RSC={len(samples)} HR={hr_count[0]} FTMS-Treadmill={len(treadmill_samples)}")

    ok_services = not missing
    ok_cp_control = True
    ok_reset = True
    if RUN_CONTROL:
        def cp_results(op_code):
            return [r[2] for r in cp_responses if len(r) >= 3 and r[0] == 0x80 and r[1] == op_code]
        ok_cp_control = (
            cp_results(OP_REQUEST_CONTROL)[-1:] == [CP_RESULT_SUCCESS]
            and cp_results(OP_START_RESUME)[-1:] == [CP_RESULT_SUCCESS]
            and cp_results(OP_SET_TARGET_POWER)[-1:] == [CP_RESULT_NOT_SUPPORTED]
            and cp_results(OP_SET_TARGET_SPEED)[-1:] == [CP_RESULT_SUCCESS]
        )
        if FTMS_CP_UUID in found:
            print("下发 running Reset(0x01)")
            before_reset_count = len(treadmill_samples)
            await client.write_gatt_char(FTMS_CP_UUID, bytes([OP_RESET]), response=True)
            await asyncio.sleep(2)
            reset_samples = treadmill_samples[before_reset_count:]
            reset_responses = cp_results(OP_RESET)
            ok_reset = bool(reset_responses and reset_responses[-1] == CP_RESULT_SUCCESS)
            if reset_samples:
                ok_reset = ok_reset and reset_samples[0]["distance"] is not None and reset_samples[0]["distance"] <= 2
    ok = (
        ok_services and dis_ok and ftms_feature_ok and ok_notify and ok_speed and ok_cadence and ok_distance and ok_flags
        and ok_treadmill_notify and ok_treadmill_speed and ok_treadmill_distance
        and ok_treadmill_hr and ok_treadmill_elapsed
        and ok_cp_control and ok_reset
    )
    print("--- 结果 ---")
    print(f"  跑步服务齐全: {'通过' if ok_services else '失败(' + ','.join(missing) + ')'}")
    print(f"  DIS 设备信息: {'通过' if dis_ok else '失败'}")
    print(f"  FTMS Feature: {'通过' if ftms_feature_ok else '失败'}")
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
    print(f"  FTMS 跑步控制点: {'通过' if ok_cp_control else '失败'}")
    print(f"  FTMS 跑步 Reset: {'通过' if ok_reset else '失败'}")
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

        def cb_cps(_, data):
            counts["cps"] += 1
            _, power = parse_cps(bytes(data))
            last_power[0] = power

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
        ok = ok_notify and ok_services and ftms_feature_ok and ok_cp_resp and ok_erg and ok_stop_resp and dis_ok
        print("总判定:", "通过" if ok else "失败")
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
