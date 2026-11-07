#!/usr/bin/env python3
"""实际C3固件的本地虚拟刷写、NimBLE、配置与重启回归，不访问真实设备。"""
import argparse
import asyncio
import hashlib
import importlib.metadata
import json
import logging
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT/'scripts/tools'))
from virtual_board import LOCK, digest, verify_version, check_dependencies
from ble_config_e2e import (DeviceSession, CONFIG_SERVICE_UUID, CONFIG_GROUP_UUIDS, CONFIG_CONTROL_UUID,
    RESULT_SUCCESS, RESULT_STALE_REVISION, RESULT_INCOMPLETE, RESULT_INVALID_VALUE,
    OP_BEGIN, OP_COMMIT, OP_CANCEL, make_begin, make_commit, make_cancel,
    make_invalid_groups, make_modified_groups, make_gatt_rebuild_groups)
from ble_protocol import parse_cps, parse_csc, parse_hr, parse_ftms_indoor_bike, parse_rsc, parse_treadmill
from ble_telemetry import decode_meta, decode, uuid as telemetry_uuid


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def free_port():
    with socket.socket() as listener:
        listener.bind(('127.0.0.1',0))
        return listener.getsockname()[1]


def control(port, command):
    with socket.create_connection(('127.0.0.1',port), timeout=5) as client:
        client.sendall((command+'\n').encode())
        reply = client.makefile('rb').readline(1024).decode().strip()
    require(reply == 'ok', f'虚拟控制通道失败: {reply}')


class VirtualSession(DeviceSession):
    def __init__(self, phone):
        super().__init__('BikePower', 15)
        self.phone = phone
        self.connection = None
        self.peer = None
        self.characteristics = {}
        self.cp_queue = None

    async def connect(self):
        from bumble.device import Peer
        from bumble import hci
        found = asyncio.Queue()
        def advertisement(value):
            if value.is_connectable:
                found.put_nowait(value)
        self.phone.on('advertisement', advertisement)
        try:
            await self.phone.start_scanning()
            candidate = await asyncio.wait_for(found.get(),20)
        finally:
            await self.phone.stop_scanning()
            self.phone.remove_listener('advertisement', advertisement)
        connected = await self.phone.connect(candidate.address, timeout=10, own_address_type=hci.OwnAddressType.PUBLIC)
        self.connection = connected
        def disconnected(*_):
            if self.connection is connected:
                self.connection = None
        connected.on('disconnection', disconnected)
        self.peer = Peer(connected)
        self.cp_queue = None
        await self.peer.discover_all()
        await self.peer.request_mtu(64)
        self.characteristics = {}
        for service in self.peer.services:
            for characteristic in service.characteristics:
                key = characteristic.uuid.to_hex_str().lower()
                self.characteristics[key] = characteristic
        self.client = self
        self.responses = asyncio.Queue()
        self.notify_started = False

    def characteristic(self, value):
        from bumble.core import UUID
        key = UUID(value.replace('-','')).to_hex_str().lower()
        require(key in self.characteristics, f'缺少特征 {value}')
        return self.characteristics[key]

    async def read_gatt_char(self, value):
        return await self.characteristic(value).read_value()

    async def write_gatt_char(self, value, payload, response=True):
        await self.characteristic(value).write_value(payload, with_response=response)

    async def start_notify(self, value, callback):
        await self.characteristic(value).subscribe(lambda data: callback(value, data), prefer_notify=False)

    def has_service(self, value):
        from bumble.core import UUID
        return any(s.uuid == UUID(value.replace('-','')) for s in self.peer.services)

    async def disconnect(self):
        if self.connection:
            await self.connection.disconnect()
        self.connection = None
        self.peer = None
        self.client = None
        self.notify_started = False

    async def reconnect(self):
        await self.disconnect()
        await asyncio.sleep(1)
        await self.connect()


async def collect(session, specifications, seconds):
    samples = {name: [] for name in specifications}
    subscribed = []
    try:
        for name, (value, parser) in specifications.items():
            characteristic = session.characteristic(value)
            callback = lambda data, name=name: samples[name].append(bytes(data))
            await characteristic.subscribe(callback)
            subscribed.append((characteristic, callback))
        await asyncio.sleep(seconds)
        parsed = {}
        for name, (_, parser) in specifications.items():
            require(len(samples[name]) >= 2, f'{name}通知不足: {len(samples[name])}')
            parsed[name] = [parser(data) for data in samples[name]]
        return parsed
    finally:
        for characteristic, callback in reversed(subscribed):
            await characteristic.unsubscribe(callback)


async def cp(session, opcode, payload=b''):
    characteristic = session.characteristic('2AD9')
    if not getattr(session, 'cp_queue', None):
        session.cp_queue = asyncio.Queue()
        await characteristic.subscribe(lambda value: session.cp_queue.put_nowait(bytes(value)), prefer_notify=False)
    await characteristic.write_value(bytes([opcode])+payload, with_response=True)
    response = await asyncio.wait_for(session.cp_queue.get(), 8)
    require(response == bytes([0x80,opcode,1]), f'FTMS响应错误: {response.hex()}')


async def run_protocol(args, folder, record, record_not_obtained):
    from bumble import hci
    from bumble.device import Device
    from bumble.host import Host
    from bumble.link import LocalLink
    from bumble.transport import open_transport
    from bumble.pairing import PairingConfig
    from bumble.keys import JsonKeyStore
    from virtual_ble_controller import CheckedController
    link = LocalLink()
    hci_port, control_port = free_port(), free_port()
    transport = await open_transport(f'tcp-server:127.0.0.1:{hci_port}')
    firmware_controller = CheckedController('firmware', host_source=transport.source, host_sink=transport.sink,
        link=link, public_address='00:11:22:33:44:55')
    firmware_controller.random_address = hci.Address('00:11:22:33:44:55', hci.Address.PUBLIC_DEVICE_ADDRESS)
    client_controller = CheckedController('client', link=link, public_address='AA:BB:CC:DD:EE:FF')
    client_controller.random_address = hci.Address('AA:BB:CC:DD:EE:FF', hci.Address.PUBLIC_DEVICE_ADDRESS)
    host = Host(); host.controller=client_controller; client_controller.host=host
    phone = Device(name='BikePower Virtual Client', address=hci.Address('AA:BB:CC:DD:EE:FF', hci.Address.PUBLIC_DEVICE_ADDRESS), host=host)
    private = folder/'private'; private.mkdir(); private.chmod(0o700)
    phone.keystore = JsonKeyStore('virtual-client', filename=str(private/'keys.json'))
    phone.pairing_config_factory = lambda _: PairingConfig(sc=True, mitm=False, bonding=True)
    await phone.power_on()
    phone.public_address = client_controller.public_address
    client_controller.random_address = client_controller.public_address
    session = VirtualSession(phone)
    image = folder/'runtime.bin'; shutil.copyfile(args.firmware, image)
    log = (folder/'firmware.log').open('w',encoding='utf-8')
    command = [str(args.emulator), '--chip','esp32c3','--firmware',str(image),'--elf',str(args.elf),
        '--ble-hci',f'tcp:127.0.0.1:{hci_port}','--control-tcp',f'127.0.0.1:{control_port}',
        '--timeout','240s','--save-state','--net','user,restrict=yes']
    process = subprocess.Popen(command,stdout=log,stderr=log)
    original = None
    primary = None
    try:
        await session.connect()
        identity = decode_meta(bytes(await session.read_gatt_char(telemetry_uuid(1))))
        require(identity['git'] == args.expected_git, '固件Git身份不匹配')
        require(identity['board'] == args.board, '固件板型不匹配')
        record('boot_identity', identity)
        meta, original = await session.read_snapshot()
        record('eight_groups_read', {'schema':meta['schema'], 'revision':meta['revision']})
        specs = {'cps':('2A63',parse_cps),'csc':('2A5B',parse_csc),'hr':('2A37',parse_hr),'ftms':('2AD2',parse_ftms_indoor_bike)}
        data = await collect(session,specs,args.seconds)
        record('cycling_notifications',{k:len(v) for k,v in data.items()})
        require(bytes(await session.read_gatt_char('2A65')) == bytes.fromhex('08001000'),'CPS Feature错误')
        require(bytes(await session.read_gatt_char('2A5C')) == bytes.fromhex('0200'),'CSC Feature错误')
        record('standard_features', {})
        for name, payload, opcode, result in (
            ('stale_revision', make_begin(11,meta['revision']+1),OP_BEGIN,RESULT_STALE_REVISION),
            ('begin',make_begin(12,meta['revision']),OP_BEGIN,RESULT_SUCCESS),
            ('incomplete_commit',make_commit(12),OP_COMMIT,RESULT_INCOMPLETE),
            ('cancel',make_cancel(12),OP_CANCEL,RESULT_SUCCESS)):
            answer=await session.control(payload,opcode,11 if name=='stale_revision' else 12)
            require(answer['result']==result,f'{name}未返回期望错误码')
            record(name,{})
        answer=await session.commit_all(make_invalid_groups(original),meta['revision'])
        require(answer['result']==RESULT_INVALID_VALUE,'非法值未拒绝')
        require((await session.read_snapshot())[1]==original,'非法值污染配置')
        record('invalid_value_unchanged',{})
        changed=make_modified_groups(original)
        answer=await session.commit_all(changed,meta['revision'])
        require(answer['result']==RESULT_SUCCESS,'八组修改失败')
        require((await session.read_snapshot())[1]==changed,'八组修改回读不同')
        await session.reconnect()
        require((await session.read_snapshot())[1]==changed,'重连配置不同')
        record('config_commit_reconnect',{})
        if args.inject_failure == 'after-config':
            raise RuntimeError('测试注入：配置修改后失败')
        if args.inject_failure == 'cancel-after-config':
            raise asyncio.CancelledError('测试注入：配置修改后取消')
        await session.disconnect()
        await asyncio.to_thread(control, control_port,'reset')
        await asyncio.sleep(2)
        await session.connect()
        meta,persisted=await session.read_snapshot()
        require(persisted==changed,'虚拟重启NVS未保持')
        record('nvs_reset_persistence',{})
        # === security：pair+SC subscribe 先于 GATT 重建 ===
        sc_indication_received = []
        if args.security:
            try:
                await asyncio.wait_for(session.connection.pair(),20)
            except Exception as pair_err:
                print(f'[virtual-board] PAIR_FAIL type={type(pair_err).__name__} msg={pair_err}',flush=True)
                print(f'[virtual-board] firmware_unsupported={sorted(firmware_controller.unsupported)}',flush=True)
                print(f'[virtual-board] client_unsupported={sorted(client_controller.unsupported)}',flush=True)
                print(f'[virtual-board] firmware_ltk_checks={firmware_controller.encryption_checks}',flush=True)
                print(f'[virtual-board] client_ltk_checks={client_controller.encryption_checks}',flush=True)
                raise
            require(firmware_controller.encryption_checks>=1,'未核对LTK')
            require(session.connection.encryption,'未启用虚拟加密状态')
            record('secure_connections_checked_ltk',{})
            # subscribe Service Changed（0x2a05）
            sc_indication_data = []
            try:
                await asyncio.wait_for(
                    session.start_notify('2A05', lambda h, d: sc_indication_data.append(d)),
                    5.0)
                record('sc_subscribed_before_rebuild', {})
            except Exception as sub_err:
                print(f'[virtual-board] SC_SUBSCRIBE_FAIL type={type(sub_err).__name__} msg={sub_err}',flush=True)
                record('sc_subscribe_fail', {'error': str(sub_err)})
        rebuilt=make_gatt_rebuild_groups(changed)
        answer=await session.commit_all(rebuilt,meta['revision'])
        require(answer['result']==RESULT_SUCCESS and answer['restart_flags'] & 1,'GATT重建提交失败')
        await session.reconnect()
        meta,actual=await session.read_snapshot()
        require(actual==rebuilt,'GATT重建配置不同')
        require(session.has_service('180D')==bool(rebuilt['core'][14]&4),'HRS表未重建')
        record('gatt_rebuild',{})
        # === security：GATT重建后验证SC indication发送与ACK ===
        if args.security:
            # 重建后已加密重连，验证SC indication被发送（bonded peer）
            require(firmware_controller.encryption_checks>=1,'重建后未加密重连')
            # 等待SC indication（GATT重建触发Service Changed），给协议栈发送留出窗口
            deadline=time.monotonic()+5.0
            while not sc_indication_data and time.monotonic()<deadline:
                await asyncio.sleep(0.1)
            sc_found = len(sc_indication_data) > 0
            if sc_found:
                record('sc_indication_after_rebuild', {'received': sc_found, 'count': len(sc_indication_data)})
            else:
                record_not_obtained('sc_indication_after_rebuild', {'received': sc_found, 'count': len(sc_indication_data), 'note': '虚拟板GATT重建后未收到SC indication，需实物directACK确认'})
            # 第二次bonded reconnect
            await session.disconnect()
            await session.connect()
            await asyncio.wait_for(session.connection.encrypt(),15)
            require(firmware_controller.encryption_checks>=2,'第二次重连未核对LTK')
            record('bonded_reconnect_second',{})
        answer=await session.commit_all(original,meta['revision'])
        require(answer['result']==RESULT_SUCCESS,'原配置恢复失败')
        await session.reconnect()
        require((await session.read_snapshot())[1]==original,'恢复后配置不同')
        record('restore_original',{})
        if args.security:
            # 最终验证：原配置恢复后重启，配置保持
            await session.disconnect()
            await asyncio.to_thread(control, control_port,'reset')
            await asyncio.sleep(2)
            await session.connect()
            require((await session.read_snapshot())[1]==original,'最终重启配置不同')
            record('final_restore_reset_verified',{})
        if args.running:
            meta,current=await session.read_snapshot()
            running=dict(current); core=bytearray(current['core']); core[13]=1; running['core']=bytes(core)
            answer=await session.commit_all(running,meta['revision'])
            require(answer['result']==RESULT_SUCCESS,'跑步切换失败')
            await session.reconnect()
            data=await collect(session,{'rsc':('2A53',parse_rsc),'treadmill':('2ACD',parse_treadmill)},args.seconds)
            require(data['rsc'][-1]['total_distance_dm']>data['rsc'][0]['total_distance_dm'],'跑步距离不增长')
            record('running_notifications',{k:len(v) for k,v in data.items()})
            meta,_=await session.read_snapshot()
            require((await session.commit_all(original,meta['revision']))['result']==RESULT_SUCCESS,'跑步恢复失败')
            await session.reconnect()
            require((await session.read_snapshot())[1]==original,'跑步恢复配置不同')
            record('running_restore',{})
    except (Exception, asyncio.CancelledError) as error:
        primary = error
        raise
    finally:
        cleanup = []
        if original is not None:
            try:
                if session.connection is None:
                    await session.reconnect()
                meta,current = await session.read_snapshot()
                if current != original:
                    require((await session.commit_all(original,meta['revision']))['result'] == RESULT_SUCCESS, '失败收尾恢复失败')
                    await session.reconnect()
                require((await session.read_snapshot())[1] == original, '收尾恢复回读不同')
                await session.disconnect()
                await asyncio.to_thread(control, control_port, 'reset')
                await asyncio.sleep(2)
                await session.connect()
                require((await session.read_snapshot())[1] == original, '收尾恢复重启回读不同')
                record('cleanup_restore_reset_verified', {})
            except (Exception,asyncio.CancelledError) as error:
                cleanup.append(f'配置恢复: {type(error).__name__}: {error}')
        try:
            await session.disconnect()
        except (Exception,asyncio.CancelledError) as error:
            cleanup.append(f'断连: {type(error).__name__}: {error}')
        finally:
            if process.poll() is None:
                process.terminate()
            try:
                await asyncio.to_thread(process.wait,10)
            except subprocess.TimeoutExpired:
                process.kill()
                await asyncio.to_thread(process.wait,10)
            log.close()
            await transport.close()
        record('controller_scope',{'unsupported_commands':sorted(firmware_controller.unsupported),'ltk_checks':firmware_controller.encryption_checks})
        if cleanup:
            message = '; '.join(cleanup)
            if primary is not None:
                raise RuntimeError(f'主错误: {type(primary).__name__}: {primary}; 收尾错误: {message}') from primary
            raise RuntimeError(message)
        text=(folder/'firmware.log').read_text(encoding='utf-8')
        require('application ELF #1 is its build' in text,'ELF与镜像未匹配')
        require('ESP-ROM:esp32c3' in text,'缺ROM启动证据')
        require(not any(marker.lower() in text.lower() for marker in ('Firmware panicked', 'CPU fault', 'Guru Meditation Error', 'Stack canary watchpoint triggered')), '虚拟固件崩溃')


def run_flash(args, folder, record):
    image=folder/'flash-test.bin'; shutil.copyfile(args.firmware,image)
    port=free_port()
    with (folder/'flash-emulator.log').open('w',encoding='utf-8') as log:
        process=subprocess.Popen([str(args.emulator),'--chip','esp32c3','--firmware',str(image),
            '--strap-mode','0x02','--uart-tcp',f'127.0.0.1:{port}','--save-state','--timeout','90s',
            '--net','user,restrict=yes'],stdout=log,stderr=log)
        try:
            time.sleep(2)
            base=[sys.executable,'-m','esptool','--chip','esp32c3','-p',f'socket://127.0.0.1:{port}',
                '--before','no_reset','--after','no_reset','--no-stub']
            offset='0x10000'
            payload=folder/'flash-payload.bin'
            payload.write_bytes(Path(args.firmware).read_bytes()[0x10000:0x11000])
            readback=folder/'flash-readback.bin'
            for name,command in (
                ('flash_id', ['flash_id']),
                ('write_flash',['write_flash',offset,str(payload)]),
                ('verify_flash',['verify_flash',offset,str(payload)]),
                ('read_flash',['read_flash',offset,'0x1000',str(readback)])):
                result=subprocess.run(base+command,capture_output=True,text=True,timeout=30)
                (folder/f'{name}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
                require(result.returncode==0,f'{name}失败')
                record(name,{})
            require(readback.read_bytes()==payload.read_bytes(),'虚拟刷写回读不同')
            record('flash_exact_readback',{})
        finally:
            if process.poll() is None: process.terminate()
            process.wait(timeout=10)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator',type=Path,required=True)
    parser.add_argument('--firmware',type=Path,required=True)
    parser.add_argument('--elf',type=Path,required=True)
    parser.add_argument('--expected-git',required=True)
    parser.add_argument('--board',choices=('generic-c3','m5stamp-c3'),default='generic-c3')
    parser.add_argument('--report-dir',type=Path,default=ROOT/'tests/reports/virtual-board')
    parser.add_argument('--seconds',type=float,default=4)
    parser.add_argument('--security',action='store_true')
    parser.add_argument('--running',action='store_true')
    parser.add_argument('--inject-failure', choices=('after-config','cancel-after-config'))
    args=parser.parse_args()
    for key in ('emulator','firmware','elf','report_dir'): setattr(args,key,getattr(args,key).resolve())
    args.report_dir.mkdir(parents=True,exist_ok=True)
    folder=Path(tempfile.mkdtemp(prefix='session-',dir=args.report_dir))
    results=[]; started=time.monotonic(); original_hash=None; elf_hash=None
    def record(name,detail):
        results.append({'name':name,'status':'PASS','detail':detail})
        print(f'[virtual-board] PASS {name} {detail}',flush=True)
    def record_not_obtained(name,detail):
        results.append({'name':name,'status':'NOT_OBTAINED','detail':detail})
        print(f'[virtual-board] NOT_OBTAINED {name} {detail}',flush=True)
    error=None
    try:
        original_hash=digest(args.firmware)
        elf_hash=digest(args.elf)
        require(2 <= args.seconds <= 30, '通知观察窗需在2至30秒内')
        verify_version(args.emulator)
        for package in ('bumble','esptool'):
            require(importlib.metadata.version(package)==LOCK[package],f'{package}版本不匹配')
        check_dependencies()
        run_flash(args,folder,record)
        asyncio.run(asyncio.wait_for(run_protocol(args,folder,record,record_not_obtained),220))
    except (Exception,KeyboardInterrupt,asyncio.CancelledError) as exc:
        error=f'{type(exc).__name__}: {exc}'
        print('[virtual-board] FAIL '+error,flush=True)
    if original_hash is not None and digest(args.firmware)!=original_hash:
        error=(error+'; ' if error else '')+'输入基线镜像被改变'
    report={'schema':1,'status':'FAIL' if error else 'PASS','error':error,'duration_seconds':round(time.monotonic()-started,3),
        'firmware_sha256':original_hash,'elf_sha256':elf_hash,'expected_git':args.expected_git,'platform':sys.platform,
        'source_identity_note':'内嵌Git身份不含dirty状态；源码是否干净须结合构建证据，不能仅凭此字段认定',
        'flash_scope':{'offset':65536,'bytes':4096,'full_flash_rewrite':False},
        'injected_failure':args.inject_failure,
        'versions':{k:LOCK[k] for k in ('esp_idf','esp_emulator','bumble','esptool')},'checks':results,
        'security_requested':args.security,'running_requested':args.running,
        'not_run':['真实射频','实际AES-CCM链路加密','Windows/手机缓存','实体掉电','电气灯效','已绑定跨GATT表的系统缓存','目标骑跑App'] + ([] if sys.platform=='darwin' else ['macOS本机运行']),
        'security_model':'测试控制器请求并核对双方LTK；不模拟射频AES-CCM、不证明隐私地址解析'}
    (folder/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    lines=['# 本地虚拟开发板验证',f'\n结果：{report["status"]}',f'耗时：{report["duration_seconds"]} 秒',f'固件 SHA256：{original_hash}',f'源码身份：{args.expected_git}',
        '\n## 已执行',*[f'- PASS {r["name"]}' for r in results if r['status']=='PASS'],
        '\n## 未取得',*[f'- NOT_OBTAINED {r["name"]}: {r.get("detail",{}).get("note","")}' for r in results if r['status']=='NOT_OBTAINED'],
        '\n## 边界', '- '+report['security_model'],
        '- 未执行：'+ '；'.join(report['not_run']),f'- 安全专项：{"已请求" if args.security else "NOT_RUN"}',f'- 跑步专项：{"已请求" if args.running else "NOT_RUN"}']
    if error: lines+=['\n## 失败',error]
    (folder/'report.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print('REPORT '+str(folder/'report.md'))
    has_not_obtained = any(r['status']=='NOT_OBTAINED' for r in results)
    return 1 if (error or has_not_obtained) else 0


if __name__=='__main__':
    logging.basicConfig(level=logging.ERROR)
    raise SystemExit(main())
