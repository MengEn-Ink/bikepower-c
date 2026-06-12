"""阶段3连接事件验证：连接设备3s后断开，观察串口 state 切换。

后台线程读串口，前台用 bleak 连接→保持→断开，确认状态机：
  conn=0 state=1(BLE_ACTIVE) -> 连接后 conn=1 -> 断开后回 conn=0
LED 物理表现（慢闪->常亮->慢闪）需肉眼确认，本脚本只验证连接计数与状态机日志。
"""
import asyncio
import sys
import threading
import time

import serial
from bleak import BleakScanner, BleakClient

PORT = "/dev/cu.usbmodem101"
DEVICE_NAME = "BikePower"

lines = []
stop = threading.Event()


def reader():
    s = serial.Serial(PORT, 115200, timeout=1)
    while not stop.is_set():
        ln = s.readline().decode("utf-8", "replace").rstrip()
        if ln and ("STATE" in ln or "心跳" in ln or "conn=" in ln):
            print("[serial]", ln)
            lines.append(ln)
    s.close()


async def main():
    t = threading.Thread(target=reader, daemon=True)
    t.start()
    time.sleep(1)

    print("扫描设备...")
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
    if not dev:
        print("未发现设备")
        stop.set()
        return 1
    print(f"连接 {dev.address} ...")
    async with BleakClient(dev) as client:
        print("已连接，保持 5s")
        await asyncio.sleep(5)
    print("已断开，观察 5s")
    await asyncio.sleep(5)

    stop.set()
    time.sleep(1.5)
    saw_conn1 = any("conn=1" in x for x in lines)
    saw_change = any("连接状态变化 connected=1" in x for x in lines)
    print("--- 结果 ---")
    print(f"  状态机收到连接事件: {'通过' if saw_change else '失败'}")
    print(f"  心跳显示 conn=1:    {'通过' if saw_conn1 else '(可能未跨10s心跳，非必须)'}")
    return 0 if saw_change else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
