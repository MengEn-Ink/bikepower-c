#!/usr/bin/env python3
"""真实开发板 + macOS CPS 上游 + BLE 下游的桥接数据面验证，结束恢复原配置。"""
import argparse
import asyncio
import json
import struct
import time
import urllib.request
from pathlib import Path

from bleak import BleakClient

CPS = "00002a63-0000-1000-8000-00805f9b34fb"
CSC = "00002a5b-0000-1000-8000-00805f9b34fb"


def request(base, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(base + "/api/" + path, data=data,
                                 headers={"Content-Type": "application/json", "Connection": "close"})
    with urllib.request.urlopen(req, timeout=5) as response:
        return json.loads(response.read())


async def wait_for(predicate, seconds=20):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        await asyncio.sleep(0.25)
    raise AssertionError("等待实际状态超时")


async def save(base, config):
    revision = request(base, "state")["revision"]
    response = request(base, "config", dict(config, revision=revision))
    assert response["ok"], response
    transaction = response["transaction"]
    result = await wait_for(lambda: (r if r["transaction"] == transaction and not r["pending"] else None)
                            if (r := request(base, "runtime")) else None)
    assert result["result"] == 0, result
    return result


async def run(args):
    base = "http://" + args.ip
    control = Path(args.control)
    original_control = control.read_bytes() if control.exists() else None
    original = request(base, "state")["config"]
    assert original["cycling_source"] == 0, "夹具验收要求初始模拟来源，避免覆盖已有真实绑定"
    samples = []
    cadence_samples = []
    changed = False
    try:
        control.write_text(json.dumps({"power": 238}))
        request(base, "renew", {})
        request(base, "bridge/scan", {})
        scan = await wait_for(lambda: (r if not r["scanning"] else None)
                              if (r := request(base, "bridge/scan")) else None, 15)
        peer = next(d for d in scan["devices"] if d["name"].startswith("BikePower-CPS"))
        response = request(base, "bridge/control", {"action": "test", "candidate": peer["candidate"]})
        assert response["ok"], response
        bridge = await wait_for(lambda: (b if b["tested"] and b["valid"] else None)
                                if (b := request(base, "runtime")["bridge"]) else None, 30)
        assert bridge["raw_power"] == 238 and bridge["cadence"] == 60, bridge
        assert request(base, "runtime")["source"] == 0, "临时测试提前切换实际来源"
        print("[bridge-device] PASS 临时测试两样本及原输出隔离", flush=True)
        config = dict(original, cycling_source=1, bridge_bound=1,
                      bridge_address=peer["address"], bridge_addr_type=peer["addr_type"],
                      bridge_name=peer["name"], bridge_calibration=1, bridge_watts=12,
                      broadcast_mask=3, sport_profile=0)
        changed = True
        await save(base, config)
        await asyncio.sleep(3)
        async with BleakClient(args.device, timeout=20) as client:
            await client.start_notify(CPS, lambda _, data: samples.append(struct.unpack_from("<h", data, 2)[0]))
            await client.start_notify(CSC, lambda _, data: cadence_samples.append(bytes(data)))
            await wait_for(lambda: len(samples) >= 3 and len(cadence_samples) >= 2, 15)
            assert all(p == 250 for p in samples[-3:]), samples
            print("[bridge-device] PASS 双 BLE 连接 + WiFi，238W+12W→250W，CSC 同源通知", flush=True)
            control.write_text(json.dumps({"power": 0}))
            await wait_for(lambda: samples and samples[-1] == 0, 10)
            assert request(base, "runtime")["power"] == 0
            print("[bridge-device] PASS 真实通知停踩 0W 保护", flush=True)
            control.write_text(json.dumps({"power": 238, "pause": True}))
            await wait_for(lambda: not request(base, "runtime")["bridge"]["valid"], 6)
            assert request(base, "runtime")["power"] == 0
            await asyncio.sleep(9)
            control.write_text(json.dumps({"power": 238}))
            await wait_for(lambda: request(base, "runtime")["bridge"]["valid"], 30)
            await wait_for(lambda: samples[-1] == 250, 10)
            print("[bridge-device] PASS 过期归零、无通知断开及上游重连恢复", flush=True)
            await save(base, dict(config, bridge_calibration=2, bridge_percent_x10=1100))
            await wait_for(lambda: samples[-1] == 262, 10)
            memory = request(base, "mem_stats")
            assert memory["free"] > 81920, memory
            print(f"[bridge-device] PASS 110%→262W，共存空闲堆 {memory}", flush=True)
    finally:
        if changed:
            request(base, "renew", {})
            await save(base, original)
            print("[bridge-device] 已恢复原始配置", flush=True)
        request(base, "bridge/control", {"action": "stop"})
        if original_control is None:
            control.unlink(missing_ok=True)
        else:
            control.write_bytes(original_control)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", default="192.168.4.1")
    parser.add_argument("--device", required=True, help="macOS BLE 设备 UUID")
    parser.add_argument("--control", required=True)
    args = parser.parse_args()
    asyncio.run(run(args))


if __name__ == "__main__":
    main()
