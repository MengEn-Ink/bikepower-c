#!/usr/bin/env python3
"""BikePower C — BLE 自测脚本（Mac CoreBluetooth via bleak）
扫描 BikePower → 连接 → 订阅 CPS(0x2A63) 通知 → 解析功率/曲柄并打印。
用法: python tests/ble_selftest.py [秒数]
"""
import asyncio
import struct
import sys
from bleak import BleakScanner, BleakClient

DEVICE_NAME = "BikePower"
CPS_UUID = "00002a63-0000-1000-8000-00805f9b34fb"
HR_UUID  = "00002a37-0000-1000-8000-00805f9b34fb"
CSC_UUID = "00002a5b-0000-1000-8000-00805f9b34fb"
FTMS_BIKE_UUID = "00002ad2-0000-1000-8000-00805f9b34fb"

DURATION = int(sys.argv[1]) if len(sys.argv) > 1 else 8


def parse_cps(data: bytes):
    flags, power = struct.unpack_from("<HH", data, 0)
    out = f"CPS flags=0x{flags:04x} power={power}W"
    if flags & 0x20 and len(data) >= 8:
        rev, t1024 = struct.unpack_from("<HH", data, 4)
        out += f" rev={rev} t1024={t1024}"
    return out


async def main():
    print(f"扫描 {DEVICE_NAME} ...")
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
    if not dev:
        print("未发现设备")
        return 1
    print(f"发现 {dev.name} {dev.address}，连接中...")
    async with BleakClient(dev) as client:
        print("已连接")
        svcs = client.services
        found = {c.uuid for s in svcs for c in s.characteristics}
        for label, uuid in [("CPS", CPS_UUID), ("HR", HR_UUID), ("CSC", CSC_UUID), ("FTMS-Bike", FTMS_BIKE_UUID)]:
            print(f"  特征 {label}: {'有' if uuid in found else '无'}")

        counts = {"cps": 0, "hr": 0, "csc": 0, "ftms": 0}

        def cb_cps(_, data):
            counts["cps"] += 1
            print(parse_cps(bytes(data)))

        def cb_hr(_, data):
            counts["hr"] += 1
            b = bytes(data)
            print(f"HR flags=0x{b[0]:02x} hr={b[1]}bpm")

        def cb_csc(_, data):
            counts["csc"] += 1

        def cb_ftms(_, data):
            counts["ftms"] += 1

        await client.start_notify(CPS_UUID, cb_cps)
        if HR_UUID in found:
            await client.start_notify(HR_UUID, cb_hr)
        if CSC_UUID in found:
            await client.start_notify(CSC_UUID, cb_csc)
        if FTMS_BIKE_UUID in found:
            await client.start_notify(FTMS_BIKE_UUID, cb_ftms)

        print(f"订阅成功，采集 {DURATION}s ...")
        await asyncio.sleep(DURATION)
        await client.stop_notify(CPS_UUID)
        print(f"通知计数: {counts}")
        ok = counts["cps"] > 0
        print("结果:", "通过" if ok else "失败")
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
