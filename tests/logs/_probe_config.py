"""只读：打印设备当前八组配置，用于确认 E2E 失败后是否残留测试值。"""
import asyncio, sys
sys.path.insert(0, r"D:\workspace\bikepower-c\tests")
import serial
from ble_config_e2e import DeviceSession  # noqa: E402


def hard_reset():
    s = serial.Serial("COM205", 115200, timeout=1)
    s.setDTR(False); s.setRTS(True); __import__("time").sleep(0.15); s.setRTS(False)
    __import__("time").sleep(0.05); s.close()


async def main():
    hard_reset()
    await asyncio.sleep(5)
    s = DeviceSession("BP_", 25.0, "")
    await s.connect()
    meta, snap = await s.read_snapshot()
    print("revision =", meta["revision"], " schema =", meta["schema"], " addr =", s.address)
    for k, v in snap.items():
        print(f"  {k} = {v}")
    await s.disconnect()


asyncio.run(main())

