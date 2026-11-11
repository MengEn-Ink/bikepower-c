#!/usr/bin/env python3
"""同一目标的多服务只读采样；不改配置、不复位、不清绑定。"""
import argparse
import asyncio
import json
import time
from pathlib import Path
from ble_config_e2e import DeviceSession
from ble_protocol import parse_cps, parse_hr

CPS = '00002a63-0000-1000-8000-00805f9b34fb'
HRS = '00002a37-0000-1000-8000-00805f9b34fb'

async def run(args):
    s = DeviceSession('BikePower', args.timeout, address=args.address)
    result = {'errors': [], 'frames': {'CPS': [], 'HRS': []}, 'seconds': args.seconds,
              'configuration_written': False, 'reset': False}
    started = time.monotonic()
    try:
        await s.connect()
        meta, groups = await s.read_snapshot()
        result['meta'] = meta
        result['mask'] = groups['core'][14]
        result['bridge_source'] = groups['bridge'][1]
        result['characteristics'] = [{'service': svc.uuid, 'uuid': c.uuid, 'properties': c.properties}
                                    for svc in s.client.services for c in svc.characteristics]
        for label, uuid, parser in [('CPS', CPS, parse_cps), ('HRS', HRS, parse_hr)]:
            if s.client.services.get_characteristic(uuid) is None:
                result['errors'].append(label + ': service absent')
                continue
            def cb(_, data, label=label, parser=parser):
                try:
                    result['frames'][label].append({'elapsed': round(time.monotonic()-started, 3),
                                                   'hex': bytes(data).hex(), 'value': parser(data)})
                except ValueError as exc:
                    result['errors'].append(label + ': ' + str(exc))
            await s.client.start_notify(uuid, cb)
        await asyncio.sleep(args.seconds)
        result['config_unchanged'] = (await s.read_snapshot())[1] == groups
        if not result['config_unchanged']:
            result['errors'].append('configuration changed during read-only probe')
        for label in result['frames']:
            if len(result['frames'][label]) < args.seconds - 2:
                result['errors'].append(label + ': insufficient notifications')
    except (Exception, asyncio.CancelledError) as exc:
        result['errors'].append(type(exc).__name__ + ': ' + str(exc))
    finally:
        try:
            await s.disconnect()
        except (Exception, asyncio.CancelledError) as exc:
            result['errors'].append('release: ' + str(exc))
        result['released'] = s.client is None
        result['status'] = 'FAIL' if result['errors'] else 'PASS'
        Path(args.out).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'status': result['status'], 'errors': result['errors'],
                      'mask': result.get('mask'), 'frames': {k: len(v) for k,v in result['frames'].items()}}, ensure_ascii=False))
    return int(bool(result['errors']))

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--address', required=True)
    p.add_argument('--seconds', type=int, default=20)
    p.add_argument('--timeout', type=int, default=20)
    p.add_argument('--out', required=True)
    a = p.parse_args()
    if a.seconds < 3 or a.timeout < 1:
        p.error('seconds must be >=3; timeout must be positive')
    raise SystemExit(asyncio.run(run(a)))
