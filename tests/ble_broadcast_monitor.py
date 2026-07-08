#!/usr/bin/env python3
"""BLE 广播项开关真机监控门禁。

设备需处于配网态，脚本通过 HTTP /api/config 切换 broadcast_mask，
同时连接 BLE 订阅通知，验证关闭/开启功率、踏频、心率是否真实反映到
CPS/HRS/CSC/FTMS 数据面。
"""
import argparse
import asyncio
import json
import struct
import sys
import time
import urllib.request

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "BikePower"

CPS_UUID = "00002a63-0000-1000-8000-00805f9b34fb"
HR_UUID = "00002a37-0000-1000-8000-00805f9b34fb"
CSC_UUID = "00002a5b-0000-1000-8000-00805f9b34fb"
FTMS_BIKE_UUID = "00002ad2-0000-1000-8000-00805f9b34fb"
RSC_UUID = "00002a53-0000-1000-8000-00805f9b34fb"

POWER_BIT = 1
CADENCE_BIT = 2
HR_BIT = 4
ALL_BITS = POWER_BIT | CADENCE_BIT | HR_BIT


def request_json(base_url, path, payload=None, timeout=5):
    url = base_url.rstrip("/") + path
    if payload is None:
        req = urllib.request.Request(url, headers={"Connection": "close"})
    else:
        body = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            url,
            data=body,
            method="POST",
            headers={"Connection": "close", "Content-Type": "application/json"},
        )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = resp.read()
    return json.loads(data.decode("utf-8"))


def parse_hr(data):
    if len(data) < 2:
        return None
    if data[0] & 0x01:
        return struct.unpack_from("<H", data, 1)[0] if len(data) >= 3 else None
    return data[1]


def parse_cps(data):
    if len(data) < 4:
        return None
    return struct.unpack_from("<h", data, 2)[0]


def parse_csc(data, prev):
    if len(data) < 5 or not (data[0] & 0x02):
        return None, prev
    revs = struct.unpack_from("<H", data, 1)[0]
    t1024 = struct.unpack_from("<H", data, 3)[0]
    if prev is None:
        return None, (revs, t1024)
    prev_revs, prev_t1024 = prev
    rev_delta = (revs - prev_revs) & 0xFFFF
    time_delta = (t1024 - prev_t1024) & 0xFFFF
    if rev_delta <= 0 or time_delta <= 0:
        return None, (revs, t1024)
    cadence = round((rev_delta / (time_delta / 1024.0)) * 60.0)
    return cadence, (revs, t1024)


def parse_rsc(data):
    if len(data) < 4:
        return None
    speed = struct.unpack_from("<H", data, 1)[0] * 3.6 / 256.0
    cadence = data[3]
    return {"speed_kmh": speed, "cadence": cadence}


def parse_ftms_indoor_bike(data):
    if len(data) < 4:
        return {"power": None, "cadence": None}
    flags = struct.unpack_from("<H", data, 0)[0]
    off = 2
    if not (flags & 0x0001) and len(data) >= off + 2:
        off += 2
    cadence = None
    if (flags & 0x0004) and len(data) >= off + 2:
        cadence = struct.unpack_from("<H", data, off)[0] / 2.0
        off += 2
    power = None
    if len(data) >= off + 2:
        power = struct.unpack_from("<h", data, off)[0]
    return {"power": power, "cadence": cadence}


class BleCounters:
    def __init__(self):
        self.reset()
        self._csc_prev = None

    def reset(self):
        self.counts = {
            "cps": 0,
            "hr": 0,
            "csc": 0,
            "ftms": 0,
            "rsc": 0,
            "ftms_power": 0,
            "ftms_cadence": 0,
        }
        self.values = {
            "power": None,
            "hr": None,
            "cadence": None,
            "ftms_power": None,
            "ftms_cadence": None,
            "rsc_speed": None,
            "rsc_cadence": None,
        }
        self.errors = []

    def handle_cps(self, _, data):
        self.counts["cps"] += 1
        power = parse_cps(bytes(data))
        self.values["power"] = power
        if power is None or power < 0 or power > 2000:
            self.errors.append(f"CPS 功率异常: {power}")

    def handle_hr(self, _, data):
        self.counts["hr"] += 1
        hr = parse_hr(bytes(data))
        self.values["hr"] = hr
        if hr is None or hr < 30 or hr > 240:
            self.errors.append(f"HRS 心率异常: {hr}")

    def handle_csc(self, _, data):
        self.counts["csc"] += 1
        cadence, self._csc_prev = parse_csc(bytes(data), self._csc_prev)
        if cadence is not None:
            self.values["cadence"] = cadence
            if cadence < 0 or cadence > 260:
                self.errors.append(f"CSC 踏频异常: {cadence}")

    def handle_ftms(self, _, data):
        self.counts["ftms"] += 1
        parsed = parse_ftms_indoor_bike(bytes(data))
        if parsed["power"] is not None:
            self.counts["ftms_power"] += 1
            self.values["ftms_power"] = parsed["power"]
            if parsed["power"] < 0 or parsed["power"] > 2000:
                self.errors.append(f"FTMS 功率异常: {parsed['power']}")
        if parsed["cadence"] is not None:
            self.counts["ftms_cadence"] += 1
            self.values["ftms_cadence"] = parsed["cadence"]
            if parsed["cadence"] < 0 or parsed["cadence"] > 260:
                self.errors.append(f"FTMS 踏频异常: {parsed['cadence']}")

    def handle_rsc(self, _, data):
        self.counts["rsc"] += 1
        parsed = parse_rsc(bytes(data))
        if parsed:
            self.values["rsc_speed"] = parsed["speed_kmh"]
            self.values["rsc_cadence"] = parsed["cadence"]


async def find_device(name_prefix, timeout):
    dev = await BleakScanner.find_device_by_name(name_prefix, timeout=timeout)
    if dev:
        return dev
    devices = await BleakScanner.discover(timeout=timeout)
    for item in devices:
        if item.name and item.name.startswith(name_prefix):
            return item
    return None


async def subscribe_available(client, counters):
    found = {c.uuid for s in client.services for c in s.characteristics}
    subscriptions = [
        ("cps", CPS_UUID, counters.handle_cps),
        ("hr", HR_UUID, counters.handle_hr),
        ("csc", CSC_UUID, counters.handle_csc),
        ("ftms", FTMS_BIKE_UUID, counters.handle_ftms),
        ("rsc", RSC_UUID, counters.handle_rsc),
    ]
    active = []
    for name, uuid, handler in subscriptions:
        if uuid not in found:
            continue
        await client.start_notify(uuid, handler)
        active.append(name)
    return active


def expect_positive(counts, key, failures, label, min_count):
    if counts.get(key, 0) < min_count:
        failures.append(f"{label} 应有通知，实际 {counts.get(key, 0)}")


def expect_zero(counts, key, failures, label):
    if counts.get(key, 0) != 0:
        failures.append(f"{label} 应停止通知，实际 {counts.get(key, 0)}")


def evaluate_case(case, counters, min_count):
    counts = counters.counts
    failures = []
    mask = case["mask"]
    power_on = (mask & POWER_BIT) != 0
    cadence_on = (mask & CADENCE_BIT) != 0
    hr_on = (mask & HR_BIT) != 0

    if power_on:
        expect_positive(counts, "cps", failures, "CPS 功率", min_count)
        expect_positive(counts, "ftms_power", failures, "FTMS 功率字段", min_count)
    else:
        expect_zero(counts, "cps", failures, "CPS 功率")
        expect_zero(counts, "ftms_power", failures, "FTMS 功率字段")

    if cadence_on:
        expect_positive(counts, "csc", failures, "CSC 踏频", min_count)
        expect_positive(counts, "ftms_cadence", failures, "FTMS 踏频字段", min_count)
    else:
        expect_zero(counts, "csc", failures, "CSC 踏频")
        expect_zero(counts, "ftms_cadence", failures, "FTMS 踏频字段")

    if hr_on:
        expect_positive(counts, "hr", failures, "HRS 心率", min_count)
    else:
        expect_zero(counts, "hr", failures, "HRS 心率")

    if power_on or cadence_on:
        expect_positive(counts, "ftms", failures, "FTMS Indoor Bike", min_count)
    else:
        expect_zero(counts, "ftms", failures, "FTMS Indoor Bike")

    failures.extend(counters.errors)
    return failures


def post_config(base_url, sport_profile, mask):
    payload = {"sport_profile": sport_profile, "broadcast_mask": mask}
    saved = request_json(base_url, "/api/config", payload)
    if saved.get("ok") is not True:
        raise RuntimeError(f"POST /api/config 未返回 ok=true: {saved}")


async def run(args):
    base_url = args.base_url.rstrip("/")
    original = request_json(base_url, "/api/state")
    original_profile = original.get("sport_profile", "cycling")
    original_mask = original.get("broadcast_mask", ALL_BITS) or ALL_BITS

    print(f"[ble-broadcast] 原始配置: profile={original_profile} mask={original_mask}")
    post_config(base_url, "cycling", ALL_BITS)
    await asyncio.sleep(args.profile_settle_sec)

    dev = await find_device(args.device_name, args.scan_timeout)
    if not dev:
        raise RuntimeError(f"未发现 BLE 设备: {args.device_name}")
    print(f"[ble-broadcast] 连接 {dev.name} {dev.address}")

    counters = BleCounters()
    all_failures = []
    try:
        async with BleakClient(dev) as client:
            active = await subscribe_available(client, counters)
            print(f"[ble-broadcast] 已订阅: {','.join(active) if active else '无'}")
            if not active:
                raise RuntimeError("未订阅到任何 BLE 通知特征")

            cases = [
                {"name": "all_on", "mask": ALL_BITS},
                {"name": "power_off", "mask": CADENCE_BIT | HR_BIT},
                {"name": "hr_only", "mask": HR_BIT},
                {"name": "power_only", "mask": POWER_BIT},
            ]
            for case in cases:
                print(f"[ble-broadcast] 场景 {case['name']} mask={case['mask']}")
                post_config(base_url, "cycling", case["mask"])
                await asyncio.sleep(args.settle_sec)
                counters.reset()
                await asyncio.sleep(args.sample_sec)
                failures = evaluate_case(case, counters, args.min_count)
                print(f"[ble-broadcast]   counts={counters.counts} values={counters.values}")
                if failures:
                    for item in failures:
                        print(f"[ble-broadcast]   FAIL {item}")
                    all_failures.extend([f"{case['name']}: {item}" for item in failures])
                else:
                    print(f"[ble-broadcast]   PASS")
    finally:
        try:
            post_config(base_url, original_profile, original_mask)
            print(f"[ble-broadcast] 已恢复配置: profile={original_profile} mask={original_mask}")
        except Exception as exc:
            print(f"[ble-broadcast] WARN 恢复原始配置失败: {exc}")

    if all_failures:
        print(f"[ble-broadcast] FAIL: {len(all_failures)} 项失败")
        return 1
    print("[ble-broadcast] PASS: 广播项开关 BLE 数据面验证通过")
    return 0


def main():
    parser = argparse.ArgumentParser(description="BikePower BLE 广播项开关真机门禁")
    parser.add_argument("--base-url", default="http://192.168.4.1", help="配网页 API 根地址")
    parser.add_argument("--device-name", default=DEVICE_NAME, help="BLE 设备名前缀")
    parser.add_argument("--scan-timeout", type=float, default=10.0, help="BLE 扫描超时秒数")
    parser.add_argument("--profile-settle-sec", type=float, default=3.0, help="切到骑行 profile 后等待秒数")
    parser.add_argument("--settle-sec", type=float, default=1.5, help="每次切 broadcast_mask 后等待秒数")
    parser.add_argument("--sample-sec", type=float, default=4.0, help="每个场景采样秒数")
    parser.add_argument("--min-count", type=int, default=2, help="启用项最少通知次数")
    args = parser.parse_args()
    return asyncio.run(run(args))


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
