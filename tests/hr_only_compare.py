#!/usr/bin/env python3
"""纯心率对照：临时把广播掩码切到仅 HRS(core[14]=4)，让板端 GATT/广播只暴露 HRS+DIS。

用法：
  python hr_only_compare.py --address 10:20:BA:CC:3A:EE            # 切到纯心率
  python hr_only_compare.py --address 10:20:BA:CC:3A:EE --restore # 恢复全开 mask=15
不调用串口、不复位 MCU；依赖 commit 的 RESTART_GATT 重建 GATT 与广播。
"""
import argparse
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ble_config_e2e import (DeviceSession, make_commit, make_begin, make_cancel,
                            RESULT_SUCCESS, RESTART_GATT, CONFIG_GROUP_UUIDS,
                            validate_groups, ConfigE2EError)

HR_ONLY_MASK = 0x04
FULL_MASK = 0x0F


async def run(args):
    s = DeviceSession('BikePower', args.timeout, address=args.address)
    await s.connect()
    try:
        meta, groups = await s.read_snapshot()
        old_mask = groups['core'][14]
        print(f"[hr-only] 当前 core[14] broadcast_mask=0x{old_mask:02x} revision={meta['revision']}")

        new_groups = {k: bytearray(v) for k, v in groups.items()}
        new_core = new_groups['core']
        if args.restore:
            new_core[14] = FULL_MASK
            label = f"恢复全开 mask=0x{FULL_MASK:02x}"
        else:
            new_core[14] = HR_ONLY_MASK
            label = f"切纯心率 mask=0x{HR_ONLY_MASK:02x}（仅 HRS+DIS）"
        new_groups = {k: bytes(v) for k, v in new_groups.items()}
        validate_groups(new_groups)

        tid = __import__('secrets').randbits(32) or 1
        resp = await s.control(make_begin(tid, meta['revision']), 0x01, tid)
        if resp['result'] != RESULT_SUCCESS:
            raise ConfigE2EError(f"BEGIN 失败: {resp}")
        await s.write_groups(new_groups)
        resp = await s.control(make_commit(tid), 0x02, tid)
        if resp['result'] != RESULT_SUCCESS:
            raise ConfigE2EError(f"COMMIT 失败: {resp}")
        print(f"[hr-only] {label} 提交成功 restart_flags=0x{resp['restart_flags']:02x}")

        await s.disconnect()
        await asyncio.sleep(3)
        # 重连读回确认
        await s.connect()
        meta2, groups2 = await s.read_snapshot()
        now = groups2['core'][14]
        print(f"[hr-only] 读回 core[14]=0x{now:02x} revision={meta2['revision']}")
        print("[hr-only] " + ("已恢复全开。" if args.restore else
                             "板端现为纯心率形态，GATT 仅 HRS+DIS。请在骑鬥心率页搜索连接。"))
    finally:
        try:
            await s.disconnect()
        except Exception:
            pass


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--address', required=True)
    p.add_argument('--timeout', type=int, default=20)
    p.add_argument('--restore', action='store_true')
    a = p.parse_args()
    raise SystemExit(asyncio.run(run(a)))
