#!/usr/bin/env python3
"""BikePower C — BLE 自测脚本（Mac CoreBluetooth via bleak）

扫描 BikePower → 连接 → 校验五服务 → 读取 DIS 设备信息（厂商/型号/固件版本）
→ 订阅 CPS/HR/CSC/FTMS 通知。
再通过 FTMS 控制点(0x2AD9)下发 RequestControl + SetTargetPower(ERG)，
校验控制点 indication 响应，并确认 CPS 功率收敛到目标值附近。
用法: python tests/ble_selftest.py [秒数] [目标功率] [期望固件版本]
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
FTMS_CP_UUID = "00002ad9-0000-1000-8000-00805f9b34fb"
DIS_MANUF_UUID = "00002a29-0000-1000-8000-00805f9b34fb"
DIS_MODEL_UUID = "00002a24-0000-1000-8000-00805f9b34fb"
DIS_FW_UUID = "00002a26-0000-1000-8000-00805f9b34fb"

def get_expected_fw_version():
    """从bikepower_config.h自动解析固件版本，保持单一事实源"""
    config_path = os.path.join(os.path.dirname(__file__), "..", "main", "bikepower_config.h")
    try:
        with open(config_path, "r", encoding="utf-8") as f:
            content = f.read()
        version = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', content)
        stable = re.search(r'#define\s+FIRMWARE_STABLE_VERSION\s+"([^"]+)"', content)
        fix = re.search(r'#define\s+FIRMWARE_FIX_VERSION\s+"([^"]+)"', content)
        if version and stable and fix:
            board = os.environ.get("BIKEPOWER_EXPECT_BOARD", "generic-c3")
            git_sha = os.environ.get("BIKEPOWER_EXPECT_GIT_SHA", "")
            if not git_sha:
                try:
                    import subprocess
                    repo_root = os.path.dirname(os.path.dirname(__file__))
                    git_sha = subprocess.check_output(["git", "-C", repo_root, "rev-parse", "--short", "HEAD"], text=True).strip()
                except Exception:
                    git_sha = "nogit"
            return "%s-v%s-stable%s-fix%s-g%s" % (board, version.group(1), stable.group(1), fix.group(1), git_sha)
    except Exception:
        pass
    return "3.1.1"  # fallback默认值

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 8
TARGET_POWER = int(sys.argv[2]) if len(sys.argv) > 2 else 200
EXPECTED_FW_VERSION = sys.argv[3] if len(sys.argv) > 3 else get_expected_fw_version()

DIS_EXPECT = {
    DIS_MANUF_UUID: ("厂商", "BikePower"),
    DIS_MODEL_UUID: ("型号", "ESP32-C3"),
    DIS_FW_UUID: ("固件", EXPECTED_FW_VERSION),
}

OP_REQUEST_CONTROL = 0x00
OP_SET_TARGET_POWER = 0x05
OP_START_RESUME = 0x07

# FTMS Control Point Response 结果码
CP_RESULT_SUCCESS = 0x01
CP_RESULT_NOT_SUPPORTED = 0x02
CP_RESULT_INVALID_PARAM = 0x03


def parse_cps(data: bytes):
    flags, power = struct.unpack_from("<HH", data, 0)
    return flags, power


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

        ok_erg = abs(erg_power - TARGET_POWER) <= 20 if FTMS_CP_UUID in found else True

        print("--- 结果 ---")
        print(f"  五服务齐全: {'通过' if ok_services else '失败(' + ','.join(missing) + ')'}")
        print(f"  DIS 设备信息: {'通过' if dis_ok else '失败'}")
        print(f"  CPS 通知:   {'通过' if ok_notify else '失败'}")
        print(f"  FTMS-CP 响应: {'通过' if ok_cp_resp else '失败(' + cp_detail + ')'}")
        print(f"  ERG 功率收敛: {'通过' if ok_erg else '失败'}")
        ok = ok_notify and ok_services and ok_cp_resp and ok_erg and dis_ok
        print("总判定:", "通过" if ok else "失败")
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
