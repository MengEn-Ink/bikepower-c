#!/usr/bin/env python3
"""实际NimBLE虚拟双下游及CPS上游三链路测试；不访问真机。"""
import argparse, asyncio, json, logging, shutil, struct, subprocess, time
from pathlib import Path
from virtual_board_e2e import VirtualSession, free_port, require, control
from ble_config_e2e import CONFIG_GROUP_UUIDS, make_begin, make_cancel, OP_BEGIN, OP_CANCEL
from virtual_board_e2e import cp
from bumble import att, hci
from bumble.device import Device
from bumble.host import Host
from bumble.link import LocalLink
from bumble.transport import open_transport
from bumble.pairing import PairingConfig
from bumble.keys import JsonKeyStore
from bumble.gatt import Characteristic, Service
from bumble.core import UUID
from virtual_ble_controller import CheckedController
from virtual_observation import observe_device_time, require_frame_counts
from virtual_board import digest, firmware_sdk_version
from test_process_runtime import prepare_test_signals, stop_process
from ble_telemetry import decode, decode_meta, uuid as telemetry_uuid

async def run(args):
    if not 1 <= args.seconds <= 120:
        raise ValueError('设备观察窗需在1..120秒')
    source_hash, elf_hash = digest(args.firmware), digest(args.elf)
    started = time.monotonic()
    folder=args.report_dir/('session-'+str(time.time_ns()));folder.mkdir(parents=True)
    link=LocalLink(); port,ctl=free_port(),free_port()
    transport=await open_transport(f'tcp-server:127.0.0.1:{port}')
    fw=CheckedController('firmware',host_source=transport.source,host_sink=transport.sink,link=link,public_address='00:11:22:33:44:55')
    fw.random_address=fw.public_address
    async def device(name,address):
        c=CheckedController(name,link=link,public_address=address);c.random_address=c.public_address
        host=Host();host.controller=c;c.host=host
        d=Device(name=name,address=c.public_address,host=host)
        d.keystore=JsonKeyStore(name,filename=str(folder/(name+'-private-keys.json')))
        d.pairing_config_factory=lambda _:PairingConfig(sc=True,mitm=False,bonding=True)
        await d.power_on();d.public_address=c.public_address;c.random_address=c.public_address
        return d
    first=await device('client-a','AA:BB:CC:DD:EE:01')
    second=await device('client-b','AA:BB:CC:DD:EE:02')
    upstream=await device('upstream','AA:BB:CC:DD:EE:03')
    measurement=Characteristic('2A63',Characteristic.Properties.NOTIFY,Characteristic.Permissions.READABLE,b'\x20\x00\xfa\x00\x01\x00\x00\x04')
    upstream.add_services([Service('1818',[measurement])])
    await upstream.start_advertising(auto_restart=True)
    a,b=VirtualSession(first),VirtualSession(second)
    a.target_address=b.target_address=fw.public_address
    image=folder/'runtime-private.bin';shutil.copyfile(args.firmware,image)
    log=(folder/'firmware.log').open('w',encoding='utf-8')
    proc=subprocess.Popen([str(args.emulator),'--chip','esp32c3','--firmware',str(image),'--elf',str(args.elf),'--ble-hci',f'tcp:127.0.0.1:{port}','--control-tcp',f'127.0.0.1:{ctl}','--timeout','600s','--save-state','--net','user,restrict=yes'],stdout=log,stderr=log)
    results=[];original=None;error=None
    stage='startup'
    def passed(name,details=None):
        results.append({'name':name,'status':'PASS','detail':details or {}});print('PASS',name,details or {},flush=True)
    async def notifications(session,samples):
        await session.characteristic('2A63').subscribe(lambda v:samples.append(bytes(v)))
    try:
        await a.connect();_,original=await a.read_snapshot();await b.connect()
        identity = decode_meta(bytes(await a.read_gatt_char(telemetry_uuid(1))))
        require(identity['git'] == args.expected_git, '固件Git身份不匹配')
        async def read_clock():
            before = decode_meta(bytes(await a.read_gatt_char(telemetry_uuid(1))))
            cpu = decode(1, bytes(await a.read_gatt_char(telemetry_uuid(3))))
            after = decode_meta(bytes(await a.read_gatt_char(telemetry_uuid(1))))
            require(before['boot'] == after['boot'], '时钟采样跨重启')
            return after['boot'], cpu['uptime_s']
        passed('two_downstream_discovery')
        left,right=[],[];await notifications(a,left);await notifications(b,right);await asyncio.sleep(5)
        require(len(left)>=3 and len(right)>=3,'双下游通知不足');require(set(left)&set(right),'两下游未共享CPS快照')
        passed('same_snapshot_fanout',{'a':len(left),'b':len(right)})
        await a.connection.pair();await b.connection.pair()
        await cp(a,0)
        b.cp_queue=asyncio.Queue();await b.characteristic('2AD9').subscribe(lambda v:b.cp_queue.put_nowait(bytes(v)),prefer_notify=False)
        await b.write_gatt_char('2AD9',b'\x00')
        denied=await asyncio.wait_for(b.cp_queue.get(),8);require(denied==b'\x80\x00\x05','FTMS抢控制未拒绝');passed('control_owner_isolation')
        meta,_=await a.read_snapshot();ans=await a.control(make_begin(701,meta['revision']),OP_BEGIN,701);require(ans['result']==0,'BEGIN失败')
        try:
            await b.write_gatt_char(CONFIG_GROUP_UUIDS['core'],original['core'])
        except att.ATT_Error as e:
            require(e.error_code == att.ErrorCode.WRITE_NOT_PERMITTED, '非owner分组写错误码不同')
        else:
            raise RuntimeError('非owner分组写未拒绝')
        ans=await b.control(make_cancel(701),OP_CANCEL,701);require(ans['result']!=0,'非owner取消未拒绝')
        ans=await a.control(make_cancel(701),OP_CANCEL,701);require(ans['result']==0,'owner取消失败');passed('config_owner_isolation')
        blocked=dict(original);core=bytearray(blocked['core']);core[14]=7 if core[14]==15 else 15;blocked['core']=bytes(core)
        meta,_=await a.read_snapshot();denied=await a.commit_all(blocked,meta['revision']);require(denied['result']!=0,'双下游在线服务表变更未拒绝');require((await a.read_snapshot())[1]==original,'拒绝后配置被修改');passed('dual_client_rebuild_rejected')
        await b.disconnect();before=len(left);await asyncio.sleep(3);require(len(left)>before,'断开B影响A');await b.connect();passed('single_disconnect_reconnect')
        await b.disconnect();meta,current=await a.read_snapshot()
        bridge=bytearray(current['bridge']);bridge[1]=1;bridge[2]=1;bridge[3]=0;bridge[4:10]=bytes(upstream.public_address);bridge[15:]=b'\x08Upstream';current['bridge']=bytes(bridge)
        stage='bridge-switch'
        answer=await a.commit_all(current,meta['revision']);require(answer['result']==0,'启用上游提交失败');await a.reconnect();await b.connect()
        left,right=[],[];await notifications(a,left);await notifications(b,right)
        stage='three-link'
        async def publish(index):
            await upstream.notify_subscribers(measurement, struct.pack('<HhHH',32,250,index+1,(index*1024)&65535))
        window = await observe_device_time(read_clock, publish, args.seconds, args.seconds*3+15)
        require(len(upstream.connections)==1,'上游连接未建立')
        minimum = require_frame_counts(len(left), len(right), window)
        require(any(struct.unpack_from('<h',v,2)[0]==250 for v in left) and any(struct.unpack_from('<h',v,2)[0]==250 for v in right),'上游功率未分别转发到两下游')
        require(not a.has_service('1826') and not b.has_service('1826'),'真实桥接误提供FTMS')
        passed('three_link_cps_bridge',{'a':len(left),'b':len(right),'upstream_connections':len(upstream.connections), 'minimum':minimum, 'window':window})
        await next(iter(upstream.connections.values())).disconnect()
        checkpoint=(len(left),len(right))
        async def publish_recovery(index):
            await upstream.notify_subscribers(measurement,struct.pack('<HhHH',32,251,index+100,(index*1024)&65535))
        recovery_window = await observe_device_time(read_clock, publish_recovery, 75, 240)
        require(len(upstream.connections)==1 and a.connection is not None and b.connection is not None,'两下游在线时上游恢复失败')
        require(len(left)>checkpoint[0] and len(right)>checkpoint[1],'上游掉线时下游停止输出')
        require(any(struct.unpack_from('<h',v,2)[0]==251 for v in left[checkpoint[0]:]) and any(struct.unpack_from('<h',v,2)[0]==251 for v in right[checkpoint[1]:]),'上游恢复数据未转发')
        passed('upstream_recovery_two_slots_occupied', {'window':recovery_window})
    except (Exception,asyncio.CancelledError) as e:
        error=f'{stage}: {type(e).__name__}: {e}';print('FAIL',error,flush=True)
    finally:
        try:
            await b.disconnect()
            if original is not None:
                if a.connection is None:await a.reconnect()
                meta,current=await a.read_snapshot()
                if current!=original:
                    require((await a.commit_all(original,meta['revision']))['result']==0,'原配置恢复失败');await a.reconnect()
                require((await a.read_snapshot())[1]==original,'原配置回读不同')
                await a.disconnect();await asyncio.to_thread(control,ctl,'reset');await asyncio.sleep(2);await a.connect()
                require((await a.read_snapshot())[1]==original,'恢复后重启不同');passed('restore_reset_verified')
        except (Exception,asyncio.CancelledError) as e:
            error=(error+'; ' if error else '')+'cleanup: '+str(e)
        for label,session in (('client-a',a),('client-b',b)):
            try:
                await session.disconnect()
            except (Exception,asyncio.CancelledError) as e:
                error=(error+'; ' if error else '')+label+' disconnect: '+str(e)
        try:
            await asyncio.to_thread(stop_process, proc, 10)
        except (Exception,asyncio.CancelledError) as e:
            error=(error+'; ' if error else '')+'process cleanup: '+str(e)
        finally:
            log.close()
            try:await transport.close()
            except (Exception,asyncio.CancelledError) as e:
                error=(error+'; ' if error else '')+'transport cleanup: '+str(e)
        if digest(args.firmware) != source_hash:
            error = (error+'; ' if error else '')+'输入镜像被修改'
        report={'status':'FAIL' if error else 'PASS','error':error,'results':results,
                'firmware_sha256':source_hash,'elf_sha256':elf_hash,'expected_git':args.expected_git,
                'esp_idf':firmware_sdk_version(args.firmware), 'duration_seconds':round(time.monotonic()-started,3),
                'clock':'device_uptime with bounded host deadline',
                'scope':'actual firmware NimBLE + virtual HCI; no RF/WiFi proof'}
        (folder/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print('REPORT',folder/'report.json',flush=True)
    return 1 if error else 0

if __name__=='__main__':
    prepare_test_signals()
    p=argparse.ArgumentParser();p.add_argument('--emulator',type=Path,required=True);p.add_argument('--firmware',type=Path,required=True);p.add_argument('--elf',type=Path,required=True);p.add_argument('--report-dir',type=Path,required=True);p.add_argument('--seconds',type=int,default=60)
    p.add_argument('--expected-git', required=True)
    logging.basicConfig(level=logging.ERROR)
    raise SystemExit(asyncio.run(run(p.parse_args())))
