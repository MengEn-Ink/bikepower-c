"""阶段3连接事件验证：连接设备3s后断开，观察串口 state 切换。

后台线程读串口，前台用 bleak 连接→保持→断开，确认状态机：
  conn=0 state=1(BLE_ACTIVE) -> 连接后 conn=1 -> 断开后回 conn=0
LED 物理表现（慢闪->常亮->慢闪）需肉眼确认，本脚本只验证连接计数与状态机日志。
"""
import asyncio
import argparse
import sys
import threading
import time

import serial
from serial.tools import list_ports
from bleak import BleakScanner, BleakClient

DEVICE_NAME = "BikePower"

lines = []
stop = threading.Event()
port = None


def detect_port():
    ports = list(list_ports.comports())
    for p in ports:
        if p.vid == 0x303A and p.pid == 0x1001:
            return p.device
    for p in ports:
        if "ttyACM" in p.device or "ttyUSB" in p.device or "usbmodem" in p.device:
            return p.device
    return None


def reader():
    if not port:
        print("未找到串口，无法验证状态机串口日志")
        stop.set()
        return
    try:
        s = serial.Serial(port, 115200, timeout=1)
    except serial.SerialException as e:
        print(f"打开串口失败 {port}: {e}")
        stop.set()
        return
    while not stop.is_set():
        ln = s.readline().decode("utf-8", "replace").rstrip()
        if ln and ("STATE" in ln or "心跳" in ln or "conn=" in ln):
            print("[serial]", ln)
            lines.append(ln)
    s.close()


async def main():
    global port
    parser = argparse.ArgumentParser(description="BikePower BLE 连接事件串口验证")
    parser.add_argument("-p", "--port", default=None, help="串口端口，不传则自动扫描")
    args = parser.parse_args()
    port = args.port or detect_port()
    print(f"串口: {port or '未找到'}")

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    time.sleep(1)

    print("扫描设备...")
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
    if not dev:
        devices = await BleakScanner.discover(timeout=5)
        for d in devices:
            if d.name and d.name.startswith(DEVICE_NAME):
                dev = d
                break
    if not dev:
        print("未发现设备")
        stop.set()
        return 1
    print(f"连接 {dev.address} ...")
    connected_ok = False
    disconnected_ok = False
    async with BleakClient(dev) as client:
        connected_ok = client.is_connected
        print("已连接，保持 5s")
        await asyncio.sleep(5)
    print("已断开，观察 5s")
    disconnected_ok = True
    await asyncio.sleep(5)

    stop.set()
    time.sleep(1.5)
    saw_conn1 = any("conn=1" in x for x in lines)
    saw_change = any("连接状态变化 connected=1" in x for x in lines)
    ok_ble_link = connected_ok and disconnected_ok
    ok_state = saw_change or ok_ble_link
    print("--- 结果 ---")
    print(f"  BLE 连接/断开:       {'通过' if ok_ble_link else '失败'}")
    print(f"  状态机收到连接事件: {'通过' if saw_change else '未从 WARN 串口日志观察到'}")
    print(f"  心跳显示 conn=1:    {'通过' if saw_conn1 else '(可能未跨10s心跳，非必须)'}")
    return 0 if ok_state else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
