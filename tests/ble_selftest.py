#!/usr/bin/env python3
"""BikePower C — BLE 自测脚本（Mac CoreBluetooth via bleak）

扫描 BikePower → 连接 → 校验五服务 → 读取 DIS 设备信息（厂商/型号/固件版本）
→ 订阅 CPS/HR/CSC/FTMS 通知。
再通过 FTMS 控制点(0x2AD9)下发 RequestControl + SetTargetPower(ERG)，
校验控制点 indication 响应，并确认 CPS 功率收敛到目标值附近。
用法: python tests/ble_selftest.py [秒数] [目标功率]
"""
import asyncio
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

DIS_EXPECT = {
    DIS_MANUF_UUID: ("厂商", "BikePower"),
    DIS_MODEL_UUID: ("型号", "ESP32-C3"),
    DIS_FW_UUID: ("固件", "3.0.0"),
}

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 8
TARGET_POWER = int(sys.argv[2]) if len(sys.argv) > 2 else 200

OP_REQUEST_CONTROL = 0x00
OP_SET_TARGET_POWER = 0x05
OP_START_RESUME = 0x07


def parse_cps(data: bytes):
    flags, power = struct.unpack_from("<HH", data, 0)
    return flags, power


async def main():
    print(f"扫描 {DEVICE_NAME} ...")
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
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
            print(f"下发 SetTargetPower(0x05, {TARGET_POWER})")
            await client.write_gatt_char(
                FTMS_CP_UUID,
                bytes([OP_SET_TARGET_POWER]) + struct.pack("<h", TARGET_POWER),
                response=True,
            )

        print(f"采集 ERG 收敛 {DURATION}s ...")
        await asyncio.sleep(DURATION)
        erg_power = last_power[0]
        print(f"通知计数: {counts}")
        print(f"ERG 后功率 ~{erg_power}W (目标 {TARGET_POWER}W)")

        ok_notify = counts["cps"] > 0
        ok_services = not missing
        ok_cp_resp = any(r and r[0] == 0x80 for r in cp_responses) if FTMS_CP_UUID in found else True
        ok_erg = abs(erg_power - TARGET_POWER) <= 20 if FTMS_CP_UUID in found else True

        print("--- 结果 ---")
        print(f"  五服务齐全: {'通过' if ok_services else '失败(' + ','.join(missing) + ')'}")
        print(f"  DIS 设备信息: {'通过' if dis_ok else '失败'}")
        print(f"  CPS 通知:   {'通过' if ok_notify else '失败'}")
        print(f"  FTMS-CP 响应0x80: {'通过' if ok_cp_resp else '失败'}")
        print(f"  ERG 功率收敛: {'通过' if ok_erg else '失败'}")
        ok = ok_notify and ok_services and ok_cp_resp and ok_erg and dis_ok
        print("总判定:", "通过" if ok else "失败")
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
