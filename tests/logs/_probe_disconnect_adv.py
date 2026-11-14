"""连上→断开后，测量设备恢复广播的时间（定位 E2E 重连失败根因）。"""
import asyncio, sys, time
sys.path.insert(0, r"D:\workspace\bikepower-c\tests")
from bleak import BleakClient, BleakScanner


async def find(timeout=15.0):
    devs = await BleakScanner.discover(timeout=timeout, return_adv=True)
    for d, adv in devs.values():
        if (adv.local_name or d.name or "").startswith("BP"):
            return d
    return None


async def wait_back(t0, limit=60):
    while time.monotonic() - t0 < limit:
        d = await find(timeout=3.0)
        if d:
            print(f"[probe] 广播恢复：{time.monotonic() - t0:.1f}s  addr={d.address}", flush=True)
            return True
        print(f"[probe] t={time.monotonic() - t0:.0f}s 尚未恢复广播", flush=True)
    print(f"[probe] {limit}s 内未恢复广播", flush=True)
    return False


async def main():
    d = await find()
    if not d:
        print("[probe] 起始就没扫到设备，先复位再试")
        return
    print(f"[probe] 连接 {d.address} ...", flush=True)
    c = BleakClient(d, timeout=20, winrt={"use_cached_services": False})
    await c.connect()
    print("[probe] 已连接；保持 3 秒", flush=True)
    await asyncio.sleep(3)
    await c.disconnect()
    print("[probe] 已断开，开始观察广播", flush=True)
    await wait_back(time.monotonic())


asyncio.run(main())
