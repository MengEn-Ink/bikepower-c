#!/usr/bin/env python3
"""只读解析NVS有效记录，比较配置/身份/密钥；不打印payload。"""
import argparse,hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'build/toolchain/esp-idf-v5.5.4/components/nvs_flash/nvs_partition_tool'))
from nvs_parser import NVS_Partition

def records(path):
    p=NVS_Partition('nvs',bytearray(path.read_bytes())); ns={};out={}
    for page in p.pages:
        if page.is_empty:continue
        assert page.header['crc']['original']==page.header['crc']['computed'],'page CRC mismatch'
        for e in page.entries:
            if e.state!='Written':continue
            m=e.metadata;assert m['crc']['original']==m['crc']['computed'],'entry CRC mismatch'
            if m['namespace']==0:ns[e.data['value']]=e.key
    for page in p.pages:
        for e in page.entries:
            if e.state!='Written' or e.metadata['namespace']==0:continue
            m=e.metadata;typ=m['type'];namespace=ns.get(m['namespace'],str(m['namespace']))
            if typ in ('blob_data','blob','string'):
                value=b''.join(bytes(c.raw) for c in e.children)[:e.data['size']]
                assert m['crc']['data_original']==m['crc']['data_computed'],'data CRC mismatch'
            else:value=bytes(e.raw[24:])
            out[f'{namespace}/{e.key}/{typ}/{m["chunk_index"]}']=hashlib.sha256(value).hexdigest()
    return out
p=argparse.ArgumentParser();p.add_argument('before',type=Path);p.add_argument('after',type=Path,nargs='?');p.add_argument('--output',type=Path,required=True);args=p.parse_args()
a=records(args.before);report={'valid_records':len(a),'records':a}
if args.after:
    b=records(args.after);changed=sorted(k for k in set(a)|set(b) if a.get(k)!=b.get(k))
    protected=[k for k in changed if not (k.startswith(('phy/','ble_gatt_cache/','nimble_bond/cccd_sec_','nimble_bond/csfc_sec_')))]
    report.update(after_valid_records=len(b),changed=changed,protected_changes=protected,comparison_scope='用户配置/身份/密钥保持；内部校准、布局及CCCD变化单独列出')
    if protected:raise RuntimeError('配置/身份/密钥变化：'+str(protected))
args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('PASS valid NVS records=',len(a),'protected records unchanged' if args.after else 'CRC verified')
