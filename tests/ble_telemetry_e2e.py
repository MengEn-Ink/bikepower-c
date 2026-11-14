#!/usr/bin/env python3
"""真实设备遥测：四组通知、CPU/堆/RSSI、退订、双次重连；不修改配置。"""
import argparse
import asyncio
import json
from pathlib import Path
from bleak import BleakClient, BleakScanner
from ble_telemetry import decode, decode_meta, uuid

async def wait_link_ready(client, timeout=5.0):
    """有界等待host低频链路缓存，正式样本仍严格验证MTU与通知。"""
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    while True:
        link = decode(2, bytes(await client.read_gatt_char(uuid(4))))
        if link["connected"] and link["mtu"] >= 23:
            return link
        remaining = deadline - loop.time()
        if remaining <= 0:
            raise TimeoutError("遥测连接缓存未在规定时间内就绪")
        await asyncio.sleep(min(0.1, remaining))

async def run(args):
    device = await BleakScanner.find_device_by_filter(
        lambda d, _: args.address == d.address if args.address else d.name and d.name.startswith(args.name),
        timeout=15)
    if not device:
        raise RuntimeError("未发现目标开发板；网页和骑行 App 必须先释放连接")
    report = {"source": "real-device-ble", "sessions": []}
    boot = None
    for attempt in range(2):
        async with BleakClient(device) as client:
            meta = decode_meta(bytes(await client.read_gatt_char(uuid(1))))
            if boot is not None and boot != meta["boot"]:
                raise AssertionError("普通重连不应重启设备")
            boot = meta["boot"]
            await wait_link_ready(client)
            pending, samples, errors = {}, [], []
            count = 0
            def handler(group):
                def receive(_, data):
                    nonlocal count
                    count += 1
                    try:
                        value = decode(group, bytes(data))
                        sequence = value["sequence"]
                        parts = pending.setdefault(sequence, {})
                        parts[group] = value
                        if len(parts) == 4:
                            sample = {}
                            for index in range(4):
                                sample.update(parts[index])
                            samples.append(sample)
                            del pending[sequence]
                        while len(pending) > 8:
                            del pending[next(iter(pending))]
                    except Exception as exc:
                        errors.append(str(exc))
                return receive
            subscribed = []
            try:
                for group in range(4):
                    await client.start_notify(uuid(group + 2), handler(group))
                    subscribed.append(uuid(group + 2))
                await asyncio.sleep(args.seconds)
            finally:
                for characteristic in subscribed:
                    await client.stop_notify(characteristic)
            baseline = count
            await asyncio.sleep(3)
            assert count == baseline, "退订后仍发送遥测通知"
            assert not errors, errors
            assert len(samples) >= 3, "完整快照不足"
            assert all(sample["free"] > 81920 for sample in samples), "空闲堆低于 80 KiB"
            assert any(sample["cpu_percent"] is not None for sample in samples), "CPU 统计未启用"
            assert any(sample["rssi"] is not None for sample in samples), "未取得有效板端 RSSI"
            assert samples[-1]["uptime_s"] > samples[0]["uptime_s"], "快照未推进"
            assert all(sample["mtu"] >= 23 for sample in samples)
            report["sessions"].append({"attempt": attempt + 1, "meta": meta,
                                      "samples": samples, "notifications": count})
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print("PASS: 真实遥测、CPU、RSSI、堆门限、退订和双次重连")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="BP_")
    parser.add_argument("--address")
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.seconds < 5:
        parser.error("--seconds 至少 5 秒")
    asyncio.run(run(args))

if __name__ == "__main__":
    main()
