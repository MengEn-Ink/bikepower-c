#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""只读空中广播：打印目标设备的 service UUID / service data / 名称 / RSSI。不改配置、不连接。"""
import argparse
import asyncio
import json
import time

from bleak import BleakScanner

TARGET_NAME = "BikePower"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--address", default="10:20:BA:CC:3A:EE")
    ap.add_argument("--seconds", type=float, default=15.0)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    found = {}

    def cb(dev, adv):
        if dev.address.upper() != args.address.upper():
            return
        found[dev.address.upper()] = {
            "name": adv.local_name,
            "rssi": adv.rssi,
            "service_uuids": sorted(u for u in adv.service_uuids),
            "service_data": {str(k): v.hex() for k, v in adv.service_data.items()},
            "manufacturer_data": {str(k): v.hex() for k, v in adv.manufacturer_data.items()},
            "tx_power": adv.tx_power,
        }

    async def run():
        scanner = BleakScanner(detection_callback=cb)
        await scanner.start()
        await asyncio.sleep(args.seconds)
        await scanner.stop()

    asyncio.run(run())
    out = {"target": args.address.upper(), "found": found}
    print(json.dumps(out, ensure_ascii=False, indent=2))
    from pathlib import Path
    Path(args.out).write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
