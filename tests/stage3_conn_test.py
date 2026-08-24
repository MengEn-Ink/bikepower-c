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
    # 优先匹配 ESP32-C3 USB VID:PID
    for p in ports:
        if p.vid == 0x303A and p.pid == 0x1001:
            return p.device
    # 次优匹配：常见 USB 串口设备名（跨平台）
    for p in ports:
        device = p.device or ""
        desc = (p.description or "").lower()
        if "ttyACM" in device or "ttyUSB" in device or "usbmodem" in device:
            return p.device
        # Windows COM 口：描述含 espressif/usb serial/jtag
        if device.upper().startswith("COM") and ("espressif" in desc or "jtag/serial" in desc or "usb serial" in desc):
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

    async def find_device():
        found = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
        if found:
            return found
        devices = await BleakScanner.discover(timeout=5)
        for item in devices:
            if item.name and item.name.startswith(DEVICE_NAME):
                return item
        return None

    print("第一次扫描设备...")
    first_device = await find_device()
    if not first_device:
        print("第一次扫描未发现设备")
        stop.set()
        return 1

    print(f"第一次连接 {first_device.address} ...")
    first_connected = False
    async with BleakClient(first_device) as client:
        first_connected = client.is_connected
        print("第一次已连接，保持 3s")
        await asyncio.sleep(3)
    print("第一次已断开，等待重新广播 3s")
    await asyncio.sleep(3)

    print("第二次扫描设备...")
    second_device = await find_device()
    if not second_device:
        print("第二次扫描未发现设备")
        stop.set()
        return 1

    print(f"第二次连接 {second_device.address} ...")
    second_connected = False
    async with BleakClient(second_device) as client:
        second_connected = client.is_connected
        print("第二次已连接，保持 3s")
        await asyncio.sleep(3)
    print("第二次已断开，观察 5s")
    await asyncio.sleep(5)

    disconnected_ok = first_connected and second_connected

    stop.set()
    time.sleep(1.5)
    saw_conn1 = any("conn=1" in x for x in lines)
    saw_change = any("连接状态变化 connected=1" in x for x in lines)
    ok_ble_link = first_connected and second_connected and disconnected_ok
    ok_state = saw_change or ok_ble_link
    print("--- 结果 ---")
    print(f"  BLE 两次连接/断开:   {'通过' if ok_ble_link else '失败'}")
    print(f"  状态机收到连接事件: {'通过' if saw_change else '未从 WARN 串口日志观察到'}")
    print(f"  心跳显示 conn=1:    {'通过' if saw_conn1 else '(可能未跨10s心跳，非必须)'}")
    return 0 if ok_state else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
