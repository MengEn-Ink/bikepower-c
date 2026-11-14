#!/usr/bin/env python3
"""精确目标、反复独立连接、三路数值与ERG，finally恢复八组并重启回读。"""
import argparse
import asyncio
import hashlib
import importlib.util
import json
import os
import struct
import time
from pathlib import Path
from ble_config_e2e import DeviceSession, ROOT, reset_device, validate_groups, verify_firmware, expected_firmware_name
from ble_protocol import parse_cps, parse_hr
from ble_telemetry import decode_meta, uuid as tele_uuid
from core_harness import DeviceLease, aggregate, validate_samples, write_json

CPS = '00002a63-0000-1000-8000-00805f9b34fb'
HRS = '00002a37-0000-1000-8000-00805f9b34fb'
CP = '00002ad9-0000-1000-8000-00805f9b34fb'


def artifact_paths(out):
    out = Path(out)
    return out, out.with_name(out.stem + '-original-private.json')


def check_output_paths(out):
    existing = [str(path) for path in artifact_paths(out) if path.exists()]
    if existing:
        raise FileExistsError('证据已存在；请选择新报告文件，禁止覆盖: ' + ', '.join(existing))


def missing_device_dependencies():
    required = {'bleak', 'serial', 'esptool'}
    if os.name == 'nt':
        required.add('winrt.windows.devices.bluetooth')
    missing = []
    for name in sorted(required):
        try:
            present = importlib.util.find_spec(name) is not None
        except (ImportError, ModuleNotFoundError):
            present = False
        if not present:
            missing.append(name)
    return missing


async def control_preflight(address):
    if os.name != 'nt':
        return {'platform': os.name, 'paired': None, 'encryption_verified': False}
    from winrt.windows.devices.bluetooth import BluetoothLEDevice
    dev = await BluetoothLEDevice.from_bluetooth_address_async(int(address.replace(':', ''), 16))
    try:
        if dev is None or not dev.device_information.pairing.is_paired:
            raise RuntimeError('Windows目标未配对；先完成系统配对，禁止写入测试配置')
        return {'platform': 'nt', 'paired': True, 'encryption_verified': False}
    finally:
        if dev is not None:
            dev.close()


async def exercise(args):
    check_output_paths(args.out)
    report = {'source': 'real-device', 'main_errors': [], 'restore_errors': [], 'rounds': [],
              'restored': False, 'released': False, 'expected_rounds': args.rounds,
              'seconds_per_round': args.seconds, 'configuration_written': False,
              'scope': 'data-only' if args.data_only else 'data-and-control',
              'control_status': 'NOT_RUN' if args.data_only else 'FAIL'}
    if args.firmware:
        report['expected_app_sha256'] = hashlib.sha256(Path(args.firmware).read_bytes()).hexdigest()
    s = DeviceSession(args.name, args.timeout, address=args.address, reset_method='rom')
    original = None
    preflight_passed = False
    async def disconnect():
        await s.disconnect()
        if s.client is not None:
            raise RuntimeError('连接未释放')
    try:
        missing = missing_device_dependencies()
        if missing:
            raise RuntimeError('缺少设备测试依赖: ' + ', '.join(missing))
        if not args.data_only:
            report['control_preflight'] = await control_preflight(args.address)
        preflight_passed = True
        await s.connect()
        await verify_firmware(s, expected_firmware_name(args.board))
        report['identity'] = decode_meta(bytes(await s.client.read_gatt_char(tele_uuid(1))))
        if report['identity']['board'] != args.board:
            raise RuntimeError('板型身份不匹配，拒绝写入')
        if getattr(args, 'expected_git', None) and report['identity']['git'] != args.expected_git:
            raise RuntimeError('构建Git身份不匹配，拒绝沿用镜像证据')
        meta, baseline = await s.read_snapshot()
        original = baseline
        if args.original:
            saved = json.loads(Path(args.original).read_text(encoding='utf-8'))
            if saved.get('errors'):
                raise RuntimeError('原始快照有错误，拒绝用作恢复源')
            candidate_original = {k: bytes.fromhex(v) for k,v in saved['groups'].items()}
            validate_groups(candidate_original)
            if not saved.get('dis', {}).get('2a25'):
                raise RuntimeError('外部原始快照缺少设备身份，禁止写入')
            serial = bytes(await s.client.read_gatt_char('00002a25-0000-1000-8000-00805f9b34fb')).decode('utf-8')
            if serial != saved['dis']['2a25']:
                raise RuntimeError('原始快照不属于当前设备，禁止写入')
            original = candidate_original
        original_path = artifact_paths(args.out)[1]
        write_json(original_path,
                   {'groups': {k:v.hex() for k,v in original.items()}, 'identity': report['identity'],
                    'dis': {'2a25': bytes(await s.client.read_gatt_char('00002a25-0000-1000-8000-00805f9b34fb')).decode('utf-8')}})
        report['original_snapshot'] = original_path.name
        for index in range(args.rounds):
            row = {'round': index + 1, 'mask': 15 if index % 2 == 0 else 7, 'status': 'FAIL'}
            report['rounds'].append(row)
            await s.connect()
            meta, current = await s.read_snapshot()
            target = dict(current)
            core = bytearray(target['core'])
            core[13] = 0
            core[14] = row['mask']
            target['core'] = bytes(core)
            bridge = bytearray(target['bridge'])
            bridge[1] = 0
            target['bridge'] = bytes(bridge)
            validate_groups(target)
            if target != current:
                report['configuration_written'] = True
                response = await s.commit_all(target, meta['revision'])
                row['commit'] = response
                if response['result'] != 0:
                    raise RuntimeError('场景提交失败: ' + str(response))
            await disconnect()
            await asyncio.sleep(args.settle)
            await s.connect()
            if (await s.read_snapshot())[1] != target:
                raise RuntimeError('场景重连回读不匹配')
            chars = {c.uuid for svc in s.client.services for c in svc.characteristics}
            needed = {CPS, HRS}
            if not args.data_only: needed.add(CP)
            csc = '00002a5b-0000-1000-8000-00805f9b34fb'
            if row['mask'] == 7: needed.add(csc)
            elif csc in chars: raise RuntimeError('踏频随功率模式不应注册CSC')
            if not needed <= chars:
                raise RuntimeError('场景缺少必要特征: ' + str(needed-chars))
            frames = {'CPS': [], 'HRS': []}
            parse_errors = []
            start = time.monotonic()
            def callback(label, parser):
                def receive(_, data):
                    try:
                        frames[label].append({'time': time.monotonic()-start, 'value': parser(bytes(data))})
                    except ValueError as exc: parse_errors.append(label + ': ' + str(exc))
                return receive
            await s.client.start_notify(CPS, callback('CPS', parse_cps))
            await s.client.start_notify(HRS, callback('HRS', parse_hr))
            responses = asyncio.Queue()
            if not args.data_only:
                await s.client.start_notify(CP, lambda _,d: responses.put_nowait(bytes(d)))
            row['control'] = []
            async def command(op, payload, expected):
                await s.client.write_gatt_char(CP, bytes([op])+payload, response=True)
                deadline = time.monotonic()+3
                while True:
                    packet = await asyncio.wait_for(responses.get(), max(.001, deadline-time.monotonic()))
                    if len(packet) != 3 or packet[0] != 0x80:
                        raise RuntimeError('控制响应损坏: ' + packet.hex())
                    if packet[1] != op: raise RuntimeError('控制响应opcode不匹配')
                    row['control'].append(packet.hex())
                    if packet[2] != expected: raise RuntimeError('控制结果错误: '+packet.hex())
                    break
            if not args.data_only:
                await command(0, b'', 1)
                await command(7, b'', 1)
                await command(5, struct.pack('<h', -1), 3)
                await command(5, struct.pack('<h', 320), 1)
            for values in frames.values(): values.clear()
            start = time.monotonic()
            await asyncio.sleep(args.seconds)
            summary = validate_samples(frames, args.seconds, args.seconds-2,
                                       None if args.data_only else (318, 324))
            summary['errors'].extend(parse_errors)
            if not s.client.is_connected: summary['errors'].append('采样期间意外断开')
            if not args.data_only:
                await command(8, b'\x01', 1)
            row['summary'] = summary
            row['frames'] = frames
            row['status'] = 'FAIL' if summary['errors'] else 'PASS'
            if summary['errors']: report['main_errors'].extend(summary['errors'])
            await disconnect()
            write_json(args.out, report)
    except (Exception, asyncio.CancelledError) as exc:
        report['main_errors'].append(type(exc).__name__ + ': ' + str(exc))
    finally:
        try: await disconnect()
        except (Exception, asyncio.CancelledError) as exc:
            report['restore_errors'].append('release-before-restore: ' + str(exc))
        if original is not None:
            try:
                await s.connect()
                meta, current = await s.read_snapshot()
                if current != original:
                    response = await s.commit_all(original, meta['revision'])
                    if response['result'] != 0: raise RuntimeError('恢复提交失败: ' + str(response))
                await disconnect()
                await asyncio.sleep(args.settle)
                await s.connect()
                if (await s.read_snapshot())[1] != original: raise RuntimeError('恢复八组回读不匹配')
                await disconnect()
                await asyncio.to_thread(reset_device, args.port, 'rom')
                await asyncio.sleep(args.settle)
                await s.connect()
                if (await s.read_snapshot())[1] != original: raise RuntimeError('重启后原配置不匹配')
                report['restored'] = True
            except (Exception, asyncio.CancelledError) as exc:
                report['restore_errors'].append(type(exc).__name__ + ': ' + str(exc))
        try: await disconnect()
        except (Exception, asyncio.CancelledError) as exc:
            report['restore_errors'].append('final-release: ' + str(exc))
        report['released'] = s.client is None
        if not preflight_passed:
            report['precondition_errors'] = list(report['main_errors'])
            report['restore_status'] = 'NOT_REQUIRED'
            report['control_status'] = 'NOT_RUN'
            report['status'] = 'FAIL'
        else:
            if not report['restored']: report['restore_errors'].append('原配置恢复未证实')
            report['status'] = aggregate(report['rounds'], report['main_errors'], report['restore_errors'], args.rounds)
        if not args.data_only and report['status'] == 'PASS': report['control_status'] = 'PASS'
        report['full_acceptance'] = report['status'] == 'PASS' and report['control_status'] == 'PASS'
        write_json(args.out, report)
    print(json.dumps({k: report[k] for k in ('status','main_errors','restore_errors','restored','released')}, ensure_ascii=False))
    return int(report['status'] != 'PASS')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--address', required=True)
    p.add_argument('--port', required=True)
    p.add_argument('--board', choices=('generic-c3','m5stamp-c3'), required=True)
    p.add_argument('--name', default='BP_')
    p.add_argument('--rounds', type=int, default=5)
    p.add_argument('--seconds', type=int, default=60)
    p.add_argument('--timeout', type=int, default=30)
    p.add_argument('--settle', type=float, default=6)
    p.add_argument('--data-only', action='store_true', help='仅数值/重连，不运行FTMS控制；不能替代控制验收')
    p.add_argument('--expected-git', help='要求板端遥测Git身份精确匹配；不替代镜像读回')
    p.add_argument('--firmware')
    p.add_argument('--original', help='本轮写入前完整快照，私有且不上传')
    p.add_argument('--out', required=True)
    args = p.parse_args()
    if args.rounds < 1 or args.seconds < 10 or args.settle < 1 or args.timeout < 1:
        p.error('rounds>=1 seconds>=10 settle>=1 timeout>=1')
    with DeviceLease(ROOT/'build/device-locks', args.address):
        return asyncio.run(exercise(args))

if __name__ == '__main__': raise SystemExit(main())
