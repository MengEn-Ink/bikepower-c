#!/usr/bin/env python3
"""只读精确目标八组配置及CPS，保存快照并核验前后保持。"""
import argparse,asyncio,json,struct,sys
from pathlib import Path
from bleak import BleakClient,BleakScanner
from ble_config_e2e import CONFIG_GROUP_UUIDS,CONFIG_META_UUID
async def run(args):
    found=await BleakScanner.find_device_by_address(args.address,timeout=15)
    if found is None:raise RuntimeError('精确目标未广播，不回退同名设备')
    async with BleakClient(found,timeout=30,winrt={'use_cached_services':False}) as client:
        first=bytes(await client.read_gatt_char(CONFIG_META_UUID))
        groups={k:bytes(await client.read_gatt_char(v)).hex() for k,v in CONFIG_GROUP_UUIDS.items()}
        last=bytes(await client.read_gatt_char(CONFIG_META_UUID))
        if first!=last:raise RuntimeError('读取期间Meta变化')
        serial=bytes(await client.read_gatt_char('00002a25-0000-1000-8000-00805f9b34fb')).decode()
        if serial!=args.serial:raise RuntimeError('DIS串号与指定板不符')
        samples=[];memory=[]
        memory_uuid='7a5a1002-4c7a-4f2a-9b3e-6d1f4a8c0b21'
        await client.start_notify(memory_uuid,lambda _,v:memory.append(bytes(v)))
        await client.start_notify('00002a63-0000-1000-8000-00805f9b34fb',lambda _,v:samples.append(bytes(v).hex()))
        seen=await BleakScanner.find_device_by_address(args.address,timeout=3)
        await asyncio.sleep(args.seconds)
        await client.stop_notify(memory_uuid)
        await client.stop_notify('00002a63-0000-1000-8000-00805f9b34fb')
        if len(samples)<max(3,args.seconds*0.8):raise RuntimeError('CPS通知不足')
        if not memory or any(len(v)!=20 or v[0]!=1 for v in memory):raise RuntimeError('遥测记录不足或格式错误')
        minimum_heap=min(struct.unpack_from('<I',v,8)[0] for v in memory)
        if minimum_heap<81920:raise RuntimeError('本次单下游最低堆低于80KiB')
        final_groups={k:bytes(await client.read_gatt_char(v)).hex() for k,v in CONFIG_GROUP_UUIDS.items()}
        final_meta=bytes(await client.read_gatt_char(CONFIG_META_UUID))
        if groups!=final_groups or first!=final_meta:raise RuntimeError('持续观察后配置或Meta变化')
        report={'meta':first.hex(),'groups':groups,'serial':serial,'memory_samples':len(memory),'minimum_heap':minimum_heap,'resource_scope':'one physical Windows downstream, no WiFi/upstream','cps_count':len(samples),'cps':samples,'advertising_while_connected':seen is not None,'services':[s.uuid for s in client.services]}
        if args.baseline:
            baseline=json.loads(args.baseline.read_text(encoding='utf-8'))
            if groups!=baseline['groups'] or serial!=baseline['serial']:raise RuntimeError('配置或身份与基线不同')
        args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print('PASS exact target read-only: groups=8 cps=',len(samples),'advertising_while_connected=',seen is not None)
p=argparse.ArgumentParser();p.add_argument('--address',required=True);p.add_argument('--serial',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--baseline',type=Path);p.add_argument('--seconds',type=float,default=10)
asyncio.run(run(p.parse_args()))
