#!/usr/bin/env python3
"""5 分钟设备耐久自动化验证。

反复执行「扫描 → 连接 → 订阅四路通知 → 断开」，统计连接失败、
通知中断与数据断流，用作发版前的短时设备耐久门禁。

用法:
  python tests/device_soak_5min.py                 # 默认 300 秒 / 15 秒一轮
  python tests/device_soak_5min.py --duration 300 --interval 15
"""
import argparse
import asyncio
import time

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "BikePower"
CPS_UUID = "00002a63-0000-1000-8000-00805f9b34fb"
HR_UUID = "00002a37-0000-1000-8000-00805f9b34fb"
CSC_UUID = "00002a5b-0000-1000-8000-00805f9b34fb"
FTMS_BIKE_UUID = "00002ad2-0000-1000-8000-00805f9b34fb"


def parse_args():
    parser = argparse.ArgumentParser(description="BikePower C 5 分钟设备耐久")
    parser.add_argument("--duration", type=float, default=300.0, help="总时长秒数")
    parser.add_argument("--interval", type=float, default=15.0, help="单轮连接保持秒数")
    parser.add_argument("--name", default=DEVICE_NAME, help="设备名")
    parser.add_argument("--scan-timeout", type=float, default=15.0)
    return parser.parse_args()


async def run_cycle(name, scan_timeout, hold):
    device = await BleakScanner.find_device_by_name(name, timeout=scan_timeout)
    if not device:
        return {"scan_ok": False, "notify": 0, "error": "扫描未发现设备"}
    counts = {"cps": 0, "hr": 0, "csc": 0, "ftms": 0, "other": 0}
    try:
        async with BleakClient(device) as client:
            def make_cb(key):
                def _cb(_sender, _data):
                    counts[key] += 1
                return _cb

            await client.start_notify(CPS_UUID, make_cb("cps"))
            for uuid, key in ((HR_UUID, "hr"), (CSC_UUID, "csc"), (FTMS_BIKE_UUID, "ftms")):
                try:
                    await client.start_notify(uuid, make_cb(key))
                except Exception:
                    counts["other"] += 1
            await asyncio.sleep(hold)
            await client.stop_notify(CPS_UUID)
    except Exception as exc:  # noqa: BLE001
        return {"scan_ok": True, "notify": counts["cps"], "error": f"连接异常: {exc}"}
    return {"scan_ok": True, "notify": counts["cps"], "error": ""}


async def main():
    args = parse_args()
    started = time.monotonic()
    cycles = 0
    scan_failures = 0
    connect_failures = 0
    empty_notify_cycles = 0
    total_notify = 0
    errors = []
    planned = max(1, int(args.duration // args.interval))

    while time.monotonic() - started < args.duration:
        result = await run_cycle(args.name, args.scan_timeout, args.interval)
        if result["error"] or not result["scan_ok"]:
            await asyncio.sleep(2)
            result = await run_cycle(args.name, args.scan_timeout, args.interval)
        cycles += 1
        total_notify += result["notify"]
        if not result["scan_ok"]:
            scan_failures += 1
        if result["error"]:
            connect_failures += 1
            errors.append(result["error"])
        if result["scan_ok"] and not result["error"] and result["notify"] == 0:
            empty_notify_cycles += 1
        elapsed = time.monotonic() - started
        print(
            f"  轮次 {cycles}: 通知={result['notify']} 累计={total_notify} "
            f"耗时={elapsed:.1f}s {result['error']}".rstrip()
        )

    elapsed = time.monotonic() - started
    ok = (
        elapsed >= args.duration * 0.95
        and cycles >= 2
        and scan_failures == 0
        and connect_failures == 0
        and empty_notify_cycles == 0
    )
    print("--- 结果 ---")
    print(f"  计划轮次:      {planned}")
    print(f"  实际轮次:      {cycles}")
    print(f"  总时长:        {elapsed:.1f}s")
    print(f"  扫描失败:      {scan_failures}")
    print(f"  连接失败:      {connect_failures}")
    print(f"  通知空轮次:    {empty_notify_cycles}")
    print(f"  累计 CPS 通知: {total_notify}")
    if errors:
        print("  错误样例:      " + "; ".join(errors[:3]))
    print(f"总判定: {'通过' if ok else '失败'}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
