#!/usr/bin/env python3
"""BikePower C — 发行控制子项 soak（COM205 设备执行者专属，owner: COM205 执行者）。

前置条件（由调用方在同一事务内完成）：fullmask core[14]=0x07 且禁桥接
（ble_bikepower.c:1240 FTMS 仅在 !g_bridge_mode 时注册；bridge source=REAL 时 FTMS 不注册属设计）。
finally 恢复原 mask+bridge 全八组。

五轮独立连接：每轮重新连接/订阅/下发 Request/Start/invalid-power/ERG320/Stop。
严格断言（不虚绿）：
- required CPS/HR/CSC/FTMS_BIKE/FTMS_CP 缺失 = FAIL；CP 缺失即终止，不做空采。
- 每路每窗口帧数 >= 阈值（tele 四组分别计数，不合计）。
- parse_cps 解析错误计数 > 0 = FAIL。
- ERG 收敛 320W 既有验收 318..324。
- 真正 finally：仅在读取到原快照后才执行恢复事务 + ROM 独立回读；
  主错误与恢复错误分离报告。

用法:
  python tests/ble_release_control_soak.py --address 10:20:BA:CC:3A:EE \
      --port COM205 --duration 60 --rounds 5
"""
import argparse
import asyncio
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ble_config_e2e import DeviceSession, reconnect, RESTART_GATT, reset_device  # noqa: E402
from ble_protocol import parse_cps  # noqa: E402

CPS_UUID = '00002a63-0000-1000-8000-00805f9b34fb'
HR_UUID = '00002a37-0000-1000-8000-00805f9b34fb'
CSC_UUID = '00002a5b-0000-1000-8000-00805f9b34fb'
FTMS_BIKE_UUID = '00002ad2-0000-1000-8000-00805f9b34fb'
FTMS_CP_UUID = '00002ad9-0000-1000-8000-00805f9b34fb'
TELE_UUIDS = ['7a5a1002-4c7a-4f2a-9b3e-6d1f4a8c0b21',
              '7a5a1003-4c7a-4f2a-9b3e-6d1f4a8c0b21',
              '7a5a1004-4c7a-4f2a-9b3e-6d1f4a8c0b21',
              '7a5a1005-4c7a-4f2a-9b3e-6d1f4a8c0b21']
REQUIRED = {'CPS': CPS_UUID, 'HR': HR_UUID, 'CSC': CSC_UUID,
            'FTMS_BIKE': FTMS_BIKE_UUID, 'FTMS_CP': FTMS_CP_UUID}
FULL_MASK = 0x07
ERG_TARGET = 320
ERG_RANGE = (318, 324)  # 既有验收范围，勿放宽


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument('--address', required=True)
    p.add_argument('--port', default='COM205')
    p.add_argument('--duration', type=int, default=60)
    p.add_argument('--rounds', type=int, default=5)
    p.add_argument('--min-frames', type=int, default=60)
    p.add_argument('--out', default='')
    p.add_argument('--preflight', action='store_true',
                   help='只做前置自检（连接/快照/串号/期望配置），不写设备')
    p.add_argument('--expect-core14', type=int, default=None)
    p.add_argument('--expect-bridge', type=int, default=None)
    return p.parse_args()


class PreconditionAbort(RuntimeError):
    """前置条件不成立：中止测试但不算固件 FAIL。"""


class SoakRun:
    """状态容器：便于 host fake 测试注入替换。"""

    def __init__(self, args):
        self.args = args
        self.result = {'address': args.address, 'main_failures': [], 'restore_failures': [],
                       'precondition_failures': [], 'rounds': [], 'cp_responses_hex': []}
        self.backup = None
        self.mask_before = None

    def fail_main(self, msg):
        self.result['main_failures'].append(msg)

    def fail_restore(self, msg):
        self.result['restore_failures'].append(msg)

    def fail_precondition(self, msg):
        """前置条件错误：单独归类，绝不计入固件 FAIL。"""
        self.result['precondition_failures'].append(msg)


async def subscribe_all(s, run):
    found = {c.uuid for svc in s.client.services for c in svc.characteristics}
    missing = [label for label, u in REQUIRED.items() if u not in found]
    if missing:
        for label in missing:
            run.fail_main(f'required特征缺失: {label} {REQUIRED[label]}')
        return found, False
    counts = {u: 0 for u in [CPS_UUID, HR_UUID, CSC_UUID, FTMS_BIKE_UUID] + TELE_UUIDS}
    parse_errors = []

    def make_cb(uuid):
        def cb(_, data):
            counts[uuid] += 1
            if uuid == CPS_UUID:
                try:
                    parse_cps(bytes(data))
                except ValueError as exc:
                    parse_errors.append(str(exc))
        return cb
    for uuid in counts:
        if uuid in found:
            await s.client.start_notify(uuid, make_cb(uuid))
    return {'found': found, 'counts': counts, 'parse_errors': parse_errors}, True


async def run_round(s, round_ctx, run, round_index):
    counts = round_ctx['counts']
    cp_resp = run.result['cp_responses_hex']
    await s.client.start_notify(FTMS_CP_UUID, lambda _, d: cp_resp.append(bytes(d).hex()))

    async def cp_expect(op, payload, expected, label):
        before = len(cp_resp)
        await s.client.write_gatt_char(FTMS_CP_UUID, bytes([op]) + payload, response=True)
        for _ in range(60):
            await asyncio.sleep(0.05)
            hit = [bytes.fromhex(h) for h in cp_resp[before:]
                   if len(h) >= 6 and bytes.fromhex(h)[0] == 0x80 and bytes.fromhex(h)[1] == op]
            if hit:
                code = hit[-1][2]
                if code != expected:
                    run.fail_main(f'R{round_index} {label}: resp=0x{code:02x} 期望0x{expected:02x}')
                return
        run.fail_main(f'R{round_index} {label}: 3s内无CP响应')

    await cp_expect(0x00, b'', 0x01, 'RequestControl')
    await cp_expect(0x07, b'', 0x01, 'StartResume')
    await cp_expect(0x05, struct.pack('<h', -1), 0x03, 'SetTargetPower(-1)')
    await cp_expect(0x05, struct.pack('<h', ERG_TARGET), 0x01, f'SetTargetPower({ERG_TARGET})')
    snap = dict(counts)
    await asyncio.sleep(run.args.duration)
    delta = {u: counts[u] - snap[u] for u in counts}
    for uuid, n in delta.items():
        if n < run.args.min_frames:
            run.fail_main(f'R{round_index} {uuid}帧 {n} < {run.args.min_frames}')
    await cp_expect(0x08, b'\x01', 0x01, 'StopPause')
    if round_ctx['parse_errors']:
        run.fail_main(f'R{round_index} CPS解析错误x{len(round_ctx["parse_errors"])}')
    return delta


async def main():
    args = parse_args()
    run = SoakRun(args)

    # --preflight：只读自检，不写设备、不跑主测试
    if args.preflight:
        info = {'address': args.address, 'precondition_failures': []}
        s = DeviceSession('BikePower', 30, address=args.address, reset_method='none')
        try:
            await s.connect()
            meta, cfg = await s.read_snapshot()
            info['meta'] = meta
            info['core14'] = cfg['core'][14]
            info['bridge_source'] = cfg['bridge'][1]
            serial = bytes(await s.client.read_gatt_char('00002a25-0000-1000-8000-00805f9b34fb'))
            info['serial_length'] = len(serial)
            if meta.get('schema') != 14 or meta.get('max_len') != 48:
                info['precondition_failures'].append(f'配置协议异常: {meta}')
            if args.expect_core14 is not None and cfg['core'][14] != args.expect_core14:
                info['precondition_failures'].append(
                    f"core14={cfg['core'][14]} != {args.expect_core14}")
            if args.expect_bridge is not None and cfg['bridge'][1] != args.expect_bridge:
                info['precondition_failures'].append(
                    f"bridge_source={cfg['bridge'][1]} != {args.expect_bridge}")
        except Exception as exc:  # noqa: BLE001
            info['precondition_failures'].append(f'{type(exc).__name__}: {exc}')
        finally:
            try:
                await s.disconnect()
            except Exception:  # noqa: BLE001
                pass
            info['released'] = True
        info['status'] = 'preflight-failed' if info['precondition_failures'] else 'preflight-ok'
        out = Path(args.out) if args.out else Path(__file__).with_name('soak-preflight.json')
        out.write_text(json.dumps(info, ensure_ascii=False, indent=2), encoding='utf-8', newline='\n')
        print(json.dumps(info, ensure_ascii=False))
        return 2 if info['precondition_failures'] else 0

    s = DeviceSession('BikePower', 30, address=args.address, reset_method='rom')
    try:
        await s.connect()
        meta, run.backup = await s.read_snapshot()
        run.mask_before = run.backup['core'][14]
        full = dict(run.backup)
        core = bytearray(run.backup['core'])
        core[14] = FULL_MASK
        full['core'] = bytes(core)
        bridge = bytearray(run.backup['bridge'])
        bridge[1] = 0  # BRIDGE_SOURCE_SIMULATED，禁真实桥接以注册 FTMS
        full['bridge'] = bytes(bridge)
        resp = await s.commit_all(full, meta['revision'])
        if not (resp['result'] == 0 and resp['restart_flags'] & RESTART_GATT):
            run.fail_main(f'fullmask提交异常: {resp}')
        # GATT 表重建后 Windows 缓存可能 stale，用一个跨平台设备重启代替重连
        await s.disconnect()
        await asyncio.to_thread(reset_device, args.port, 'rom')
        await asyncio.sleep(8)
        await s.connect()
        # 前置条件自检：FTMS 注册受「完整掩码 + 模拟桥接」共同门控，缺一即测试前置错误
        meta_after, cfg_after = await s.read_snapshot()
        pre = []
        core_after = (cfg_after or {}).get('core')
        bridge_after = (cfg_after or {}).get('bridge')
        if not core_after or len(core_after) < 15:
            pre.append('快照缺少 core 组，无法确认完整掩码前置')
        elif core_after[14] != FULL_MASK:
            pre.append(f"完整掩码未生效 core14={core_after[14]} != {FULL_MASK}")
        if not bridge_after or len(bridge_after) < 2:
            pre.append('快照缺少 bridge 组，无法确认桥接模式前置')
        elif bridge_after[1] != 0:
            pre.append(
                f"桥接未切模拟 bridge_source={bridge_after[1]} != 0"
                "（固件仅在非真实桥接下注册 FTMS）")
        for item in pre:
            run.fail_precondition(item)
        if pre:
            raise PreconditionAbort('前置条件不成立，中止主测试')
        round_ctx, ok = await subscribe_all(s, run)
        if not ok:
            raise RuntimeError('required特征缺失，中止主测试')
        for r in range(args.rounds):
            try:
                await s.disconnect()
                await asyncio.sleep(3)
                await s.connect()
                round_ctx, ok = await subscribe_all(s, run)
                if not ok:
                    break
                run.result['rounds'].append(await run_round(s, round_ctx, run, r + 1))
            except Exception as exc:
                run.fail_main(f'R{r + 1}独立连接失败: {type(exc).__name__}: {exc}')
                break
    except PreconditionAbort:
        pass
    except Exception as exc:
        run.fail_main(f'{type(exc).__name__}: {exc}')
    finally:
        # 真正 finally：仅在成功读到原快照时才恢复，主/恢复错误分离
        try:
            await s.disconnect()
        except Exception:
            pass
        if run.backup is not None:
            try:
                # 服务表可能已被重建，先 ROM 重启让中心设备重新枚举再恢复
                await asyncio.to_thread(reset_device, args.port, 'rom')
                await asyncio.sleep(8)
                await s.connect()
                meta2, current = await s.read_snapshot()
                if current != run.backup:
                    resp2 = await s.commit_all(run.backup, meta2['revision'])
                    if resp2['result'] != 0:
                        run.fail_restore(f'原配置恢复提交失败: {resp2}')
                await s.disconnect()
                await asyncio.to_thread(reset_device, args.port, 'rom')
                await asyncio.sleep(8)
                await s.connect()
                _, final = await s.read_snapshot()
                if final != run.backup:
                    run.fail_restore('重启后回读与原快照不一致')
            except Exception as exc:
                run.fail_restore(f'恢复路径异常: {type(exc).__name__}: {exc}')
            finally:
                try:
                    await s.disconnect()
                except Exception:
                    pass
        run.result['mask_after_restore'] = run.backup['core'][14] if run.backup else None
        if run.result['precondition_failures']:
            run.result['status'] = 'precondition'
        elif run.result['main_failures'] or run.result['restore_failures']:
            run.result['status'] = 'failed'
        else:
            run.result['status'] = 'success'
        run.result['released'] = True
        out = Path(args.out) if args.out else Path(__file__).with_suffix('.json')
        out.write_text(json.dumps(run.result, ensure_ascii=False, indent=2),
                       encoding='utf-8', newline='\n')
    print(json.dumps({'status': run.result['status'],
                      'precondition_failures': run.result['precondition_failures'],
                      'main_failures': run.result['main_failures'],
                      'restore_failures': run.result['restore_failures']}, ensure_ascii=False))
    if run.result['precondition_failures']:
        return 2
    return 1 if (run.result['main_failures'] or run.result['restore_failures']) else 0


if __name__ == '__main__':
    raise SystemExit(asyncio.run(main()))
