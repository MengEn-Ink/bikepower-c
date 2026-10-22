#!/usr/bin/env python3
"""BLE 重连真机门禁：两次独立扫描、连接与显式断开。

后台可选读取串口，前台严格执行：
  scan #1 -> connect #1 -> disconnect #1 -> scan #2 -> connect #2 -> disconnect #2
第二轮不得复用第一次扫描结果，确保设备断开后确实恢复广播并可再次连接。
"""
import argparse
import asyncio
import sys
import threading
import time

import serial
from serial.tools import list_ports
from bleak import BleakClient, BleakScanner

DEVICE_NAME = "BikePower"

lines = []
stop = threading.Event()
port = None


def detect_port():
    ports = list(list_ports.comports())
    for item in ports:
        if item.vid == 0x303A and item.pid == 0x1001:
            return item.device
    for item in ports:
        device = item.device or ""
        desc = (item.description or "").lower()
        if "ttyACM" in device or "ttyUSB" in device or "usbmodem" in device:
            return item.device
        if device.upper().startswith("COM") and (
            "espressif" in desc or "jtag/serial" in desc or "usb serial" in desc
        ):
            return item.device
    return None


def reader():
    if not port:
        print("未找到串口，跳过状态机日志观察")
        return
    try:
        stream = serial.Serial(port, 115200, timeout=1)
    except serial.SerialException as exc:
        print(f"打开串口失败 {port}: {exc}")
        return
    try:
        while not stop.is_set():
            line = stream.readline().decode("utf-8", "replace").rstrip()
            if line and ("STATE" in line or "心跳" in line or "conn=" in line or "连接状态变化" in line):
                print("[serial]", line)
                lines.append(line)
    finally:
        stream.close()


async def scan_device(scanner=BleakScanner, name=DEVICE_NAME, timeout=10.0):
    """执行一次全新的设备扫描，不缓存上一轮结果。"""
    device = await scanner.find_device_by_name(name, timeout=timeout)
    if device:
        return device
    devices = await scanner.discover(timeout=min(timeout, 5.0))
    for item in devices:
        if item.name and item.name.startswith(name):
            return item
    return None


async def connect_cycle(device, client_factory=BleakClient, hold_seconds=5.0, sleep_fn=asyncio.sleep):
    """连接一次并显式断开；只有连接和断开状态都可观察到才成功。"""
    client = client_factory(device)
    connected = False
    try:
        await client.connect()
        connected = bool(client.is_connected)
        if not connected:
            raise RuntimeError("连接调用完成但 is_connected=false")
        await sleep_fn(hold_seconds)
    finally:
        if client.is_connected:
            await client.disconnect()
    disconnected = not bool(client.is_connected)
    if not disconnected:
        raise RuntimeError("显式断开后 is_connected 仍为 true")
    return {"connected": connected, "disconnected": disconnected}


async def run_reconnect_harness(
    scanner=BleakScanner,
    client_factory=BleakClient,
    name=DEVICE_NAME,
    scan_timeout=10.0,
    hold_seconds=5.0,
    readvertise_wait=3.0,
    sleep_fn=asyncio.sleep,
):
    """执行两轮独立 scan/connect/disconnect，返回可供 fake harness 断言的结果。"""
    results = []
    addresses = []
    expected_address = None
    for cycle in (1, 2):
        print(f"第 {cycle} 次扫描设备...")
        device = await scan_device(scanner=scanner, name=name, timeout=scan_timeout)
        if not device:
            raise RuntimeError(f"第 {cycle} 次扫描未发现设备")
        address = str(device.address)
        if expected_address is None:
            expected_address = address
        elif address != expected_address:
            raise RuntimeError(
                f"第 {cycle} 次扫描命中了不同设备: "
                f"{address} != {expected_address}"
            )
        addresses.append(address)
        print(f"第 {cycle} 次连接 {device.address} ...")
        result = await connect_cycle(
            device,
            client_factory=client_factory,
            hold_seconds=hold_seconds,
            sleep_fn=sleep_fn,
        )
        results.append(result)
        print(f"第 {cycle} 次已断开")
        if cycle == 1:
            print(f"等待设备恢复广播 {readvertise_wait:.1f}s ...")
            await sleep_fn(readvertise_wait)
    return {"cycles": results, "addresses": addresses}


async def main():
    global port
    parser = argparse.ArgumentParser(description="BikePower BLE 双次重连与串口状态验证")
    parser.add_argument("-p", "--port", default=None, help="串口端口，不传则自动扫描")
    parser.add_argument("--scan-timeout", type=float, default=10.0)
    parser.add_argument("--hold-sec", type=float, default=5.0)
    parser.add_argument("--readvertise-wait", type=float, default=3.0)
    args = parser.parse_args()
    port = args.port or detect_port()
    print(f"串口: {port or '未找到（仅验证 BLE 链路）'}")

    stop.clear()
    lines.clear()
    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    await asyncio.sleep(1)

    try:
        result = await run_reconnect_harness(
            scan_timeout=args.scan_timeout,
            hold_seconds=args.hold_sec,
            readvertise_wait=args.readvertise_wait,
        )
    except Exception as exc:
        print(f"重连门禁失败: {type(exc).__name__}: {exc}")
        return 1
    finally:
        stop.set()
        thread.join(timeout=1.5)

    saw_connect = any("连接状态变化 connected=1" in item or "conn=1" in item for item in lines)
    saw_disconnect = any("连接状态变化 connected=0" in item or "conn=0" in item for item in lines)
    links_ok = len(result["cycles"]) == 2 and all(
        item["connected"] and item["disconnected"] for item in result["cycles"]
    )
    print("--- 结果 ---")
    print(f"  独立扫描次数:        {len(result['addresses'])}（期望 2）")
    print(f"  两次连接/断开:       {'通过' if links_ok else '失败'}")
    print(f"  串口观察连接事件:    {'通过' if saw_connect else '未观察到（无串口时不阻断）'}")
    print(f"  串口观察断开事件:    {'通过' if saw_disconnect else '未观察到（无串口时不阻断）'}")
    return 0 if links_ok else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
