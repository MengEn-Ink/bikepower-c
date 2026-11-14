"""复位开发板后，测量重新广播的时间（不改判据，只取证）。"""
import asyncio, time
import serial
from bleak import BleakScanner

PORT = "COM205"
TARGET = "BP_3AEE"


def hard_reset():
    s = serial.Serial(PORT, 115200, timeout=1)
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.15)
    s.setRTS(False)
    time.sleep(0.05)
    s.close()


async def wait_adv(timeout_s=90):
    t0 = time.monotonic()
    seen = None
    while time.monotonic() - t0 < timeout_s:
        devs = await BleakScanner.discover(timeout=2.0, return_adv=True)
        for d, adv in devs.values():
            name = adv.local_name or d.name or ""
            if name == TARGET or d.address.upper() == "10:20:BA:CC:3A:EE":
                seen = time.monotonic() - t0
                print(f"[probe] 重新出现：{seen:.1f}s name={name} rssi={adv.rssi}", flush=True)
                return seen
        print(f"[probe] t={time.monotonic() - t0:.0f}s 未出现（本轮 {len(devs)} 台）", flush=True)
    print(f"[probe] {timeout_s}s 内始终未广播", flush=True)
    return None


if __name__ == "__main__":
    print("[probe] 串口硬复位 ...", flush=True)
    hard_reset()
    t_reset = time.monotonic()
    got = asyncio.run(wait_adv())
    if got is not None:
        print(f"[probe] 结论：复位后 {got:.1f}s 恢复广播", flush=True)
    else:
        print("[probe] 结论：复位后未在 90s 内恢复广播", flush=True)
