#!/usr/bin/env python3
"""验证生产持久绑定接线、NVS恢复/更新及失败关闭；不替代无线验收。"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from test_ble_standard_contract import functions
from check_wifi_done_contract import extract_function

PREAMBLE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 11
#define ESP_ERR_INVALID_STATE 12
#define ESP_ERR_INVALID_SIZE 13
#define ESP_ERR_NOT_SUPPORTED 14
#define BLE_HS_EINVAL 3
#define BLE_HS_ENOENT 5
#define BLE_HS_ENOTSUP 8
#define BLE_HS_ESTORE_CAP 10
#define BLE_HS_ESTORE_FAIL 11
#define BLE_HS_EALREADY 2
#define BLE_HS_IO_NO_INPUT_OUTPUT 3
#define BLE_SM_PAIR_KEY_DIST_ENC 1
#define BLE_SM_PAIR_KEY_DIST_ID 2
#define BLE_STORE_EVENT_FULL 2
#define BLE_STORE_EVENT_OVERFLOW 1
#define BLE_STORE_OBJ_TYPE_OUR_SEC 1
#define BLE_STORE_OBJ_TYPE_PEER_SEC 2
#define BLE_STORE_OBJ_TYPE_CCCD 3
#define BLE_STORE_OBJ_TYPE_PEER_ADDR 6
#define BLE_STORE_OBJ_TYPE_LOCAL_IRK 7
#define BLE_STORE_OBJ_TYPE_CSFC 8
#define CONFIG_BT_NIMBLE_MAX_BONDS 4
#define CONFIG_BT_NIMBLE_MAX_CCCDS 48
#define MYNEWT_VAL_BLE_STORE_MAX_CSFCS 4
#define MYNEWT_VAL_BLE_SM_BONDING 1
#define MYNEWT_VAL_BLE_STORE_MAX_BONDS 4
#define MYNEWT_VAL_BLE_SM_SC 1
#define MYNEWT_VAL(x) MYNEWT_VAL_##x
#define NIMBLE_BLE_CONNECT 1
#define BLE_HS_LOG(...) ((void)0)
#define BLE_SM_PROC_F_AUTHENTICATED 1
#define BLE_SM_PROC_F_SC 2
#define BLE_SM_PROC_F_BONDING 4
#define BLE_GAP_REPEAT_PAIRING_RETRY 1
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define NVS_TYPE_ANY 255
#define NVS_TYPE_BLOB 66
#define NVS_KEY_NAME_MAX_SIZE 16
#define TAG "BLE"
#define ESP_LOGE(...) ((void)0)
#define BLE_BOND_NVS_PARTITION "nvs"
#define BLE_BOND_NVS_NAMESPACE "nimble_bond"
#define BLE_BOND_NVS_OUR_SEC_PREFIX "our_sec_"
#define BLE_BOND_NVS_PEER_SEC_PREFIX "peer_sec_"
#define BLE_BOND_NVS_CCCD_PREFIX "cccd_sec_"
#define BLE_BOND_NVS_CSFC_PREFIX "csfc_sec_"
#define BLE_BOND_NVS_IRK_PREFIX "local_irk_"
#define BLE_BOND_NVS_RPA_PREFIX "rpa_rec_"
typedef int esp_err_t;
typedef unsigned nvs_handle_t;
typedef struct { uint8_t type,val[6]; } ble_addr_t;
static const ble_addr_t any={0};
#define BLE_ADDR_ANY (&any)
struct ble_store_key_sec { ble_addr_t peer_addr; uint8_t idx; };
struct ble_store_key_cccd { ble_addr_t peer_addr; uint16_t chr_val_handle; uint8_t idx; };
struct ble_store_key_csfc { ble_addr_t peer_addr; uint8_t idx; };
struct ble_store_key_local_irk { ble_addr_t addr; uint8_t idx; };
struct ble_store_key_rpa_rec { ble_addr_t peer_rpa_addr; uint8_t idx; };
union ble_store_key { struct ble_store_key_sec sec; struct ble_store_key_cccd cccd;
 struct ble_store_key_csfc csfc; struct ble_store_key_local_irk local_irk; struct ble_store_key_rpa_rec rpa_rec; };
struct ble_store_value_sec { ble_addr_t peer_addr; uint16_t bond_count; uint8_t key_size,authenticated,sc,ltk[16]; };
struct ble_store_value_cccd { ble_addr_t peer_addr; uint16_t chr_val_handle,flags; };
struct ble_store_value_csfc { ble_addr_t peer_addr; uint8_t csfc[1]; };
struct ble_store_value_local_irk { ble_addr_t addr; uint8_t irk[16]; };
struct ble_store_value_rpa_rec { ble_addr_t peer_rpa_addr,peer_addr; };
union ble_store_value { struct ble_store_value_sec sec; struct ble_store_value_cccd cccd;
 struct ble_store_value_csfc csfc; struct ble_store_value_local_irk local_irk; struct ble_store_value_rpa_rec rpa_rec; };
struct ble_store_status_event { int event_code; };
struct ble_gap_repeat_pairing { uint16_t conn_handle; int cur_key_size,cur_authenticated,cur_sc,new_key_size,new_authenticated,new_sc,new_bonding; };
typedef unsigned ble_sm_proc_flags;
static struct { int sm_io_cap,sm_sc,sm_bonding,sm_mitm,sm_our_key_dist,sm_their_key_dist;
 int (*store_read_cb)(int,const union ble_store_key*,union ble_store_value*);
 int (*store_write_cb)(int,const union ble_store_value*);
 int (*store_delete_cb)(int,const union ble_store_key*);
 int (*store_status_cb)(struct ble_store_status_event*,void*);
} ble_hs_cfg;
static atomic_bool s_security_store_failed;
static esp_err_t security_store_sync_value(int,const union ble_store_value*);
static unsigned init_calls,writes,deletes,reads,commits,closes;
static int read_rc,write_rc,delete_rc,open_rc,commit_rc,set_rc;
static union ble_store_value ram;
static int ram_type;
static void ble_store_config_init(void) { init_calls++; }
static void ble_store_key_from_value(int t,union ble_store_key *k,const union ble_store_value *v) {
 memset(k,0,sizeof(*k));
 switch(t) {
 case 1: case 2:k->sec.peer_addr=v->sec.peer_addr;break;
 case 3:k->cccd.peer_addr=v->cccd.peer_addr;k->cccd.chr_val_handle=v->cccd.chr_val_handle;break;
 case 8:k->csfc.peer_addr=v->csfc.peer_addr;break;
 case 7:k->local_irk.addr=v->local_irk.addr;break;
 case 6:k->rpa_rec.peer_rpa_addr=v->rpa_rec.peer_rpa_addr;break;
 default:assert(0);
 }
}
static int ble_store_config_read(int t,const union ble_store_key *k,union ble_store_value *v) {
 (void)k;reads++;if(read_rc)return read_rc;if(t!=ram_type)return BLE_HS_ENOENT;*v=ram;return 0;
}
static int ble_store_config_write(int t,const union ble_store_value *v) {
 writes++;if(write_rc)return write_rc;ram_type=t;ram=*v;return 0;
}
static int ble_store_config_delete(int t,const union ble_store_key *k) {
 (void)t;(void)k;deletes++;return delete_rc;
}
typedef struct { char key[16]; unsigned type; } nvs_entry_info_t;
static struct { nvs_entry_info_t info; union ble_store_value value; size_t size; } db[5];
static unsigned db_count;
static int cursor;
typedef int *nvs_iterator_t;
static int nvs_open(const char *name,int mode,nvs_handle_t *h) {
 assert(strcmp(name,"nimble_bond")==0);(void)mode;*h=1;return open_rc;
}
static void nvs_close(nvs_handle_t h) { assert(h==1);closes++; }
static int nvs_entry_find_in_handle(nvs_handle_t h,int t,nvs_iterator_t *it) {
 assert(h==1&&t==NVS_TYPE_ANY);cursor=0;*it=db_count?&cursor:NULL;return db_count?0:ESP_ERR_NVS_NOT_FOUND;
}
static int nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t *info) { assert(it);*info=db[*it].info;return 0; }
static int nvs_entry_next(nvs_iterator_t *it) { if(++cursor<(int)db_count)return 0;*it=NULL;return ESP_ERR_NVS_NOT_FOUND; }
static void nvs_release_iterator(nvs_iterator_t it) { (void)it; }
static int nvs_get_blob(nvs_handle_t h,const char *key,void *data,size_t *size) {
 assert(h==1);for(unsigned i=0;i<db_count;i++)if(!strcmp(key,db[i].info.key)) {
 if(*size<db[i].size)return ESP_ERR_INVALID_SIZE;memcpy(data,&db[i].value,db[i].size);*size=db[i].size;return 0;
 } return ESP_ERR_NVS_NOT_FOUND;
}
static int nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size) {
 assert(h==1);if(set_rc)return set_rc;
 for(unsigned i=0;i<db_count;i++)if(!strcmp(key,db[i].info.key)){memcpy(&db[i].value,data,size);db[i].size=size;return 0;}
 return ESP_ERR_NVS_NOT_FOUND;
}
static int nvs_commit(nvs_handle_t h) { assert(h==1);commits++;return commit_rc; }
static void fixture(void) {
 memset(&ble_hs_cfg,0,sizeof(ble_hs_cfg));memset(db,0,sizeof(db));memset(&ram,0,sizeof(ram));
 db_count=0;ram_type=1;read_rc=write_rc=delete_rc=open_rc=commit_rc=set_rc=0;
 init_calls=writes=deletes=reads=commits=closes=0;s_security_store_failed=false;
}
static void record(const char *name,int type,size_t size) {
 db_count=1;strcpy(db[0].info.key,name);db[0].info.type=NVS_TYPE_BLOB;db[0].size=size;db[0].value=ram;ram_type=type;
}
'''
CHECKS = r'''
int main(void) {
 fixture();assert(configure_session_security()==0);assert(init_calls==1);
 assert(ble_hs_cfg.sm_bonding==1&&ble_hs_cfg.sm_sc==1&&ble_hs_cfg.sm_mitm==0);
 assert(ble_hs_cfg.sm_io_cap==3&&ble_hs_cfg.sm_our_key_dist==3&&ble_hs_cfg.sm_their_key_dist==3);
 assert(ble_hs_cfg.store_read_cb==security_store_read&&ble_hs_cfg.store_write_cb==security_store_write);
 assert(ble_hs_cfg.store_delete_cb==security_store_delete&&ble_hs_cfg.store_status_cb==security_store_status);
 ble_hs_cfg.store_read_cb=ble_store_config_read;
 ble_hs_cfg.store_write_cb=ble_store_config_write;
 ble_hs_cfg.store_delete_cb=ble_store_config_delete;
 unsigned initializations=init_calls;
 assert(security_store_activate()==0 && init_calls==initializations);
 assert(ble_hs_cfg.store_read_cb==security_store_read&&ble_hs_cfg.store_write_cb==security_store_write&&ble_hs_cfg.store_delete_cb==security_store_delete);
 s_security_store_failed=true;
 ble_hs_cfg.store_read_cb=ble_store_config_read;
 assert(security_store_activate()==ESP_ERR_INVALID_STATE && s_security_store_failed && ble_hs_cfg.store_read_cb==ble_store_config_read);
 fixture();open_rc=42;assert(security_store_activate()==42 && s_security_store_failed && init_calls==0);
 fixture();assert(configure_session_security()==0);
 union ble_store_key key={0};union ble_store_value value={0};
 assert(security_store_read(1,NULL,&value)==BLE_HS_EINVAL);
 assert(security_store_write(1,NULL)==BLE_HS_EINVAL);
 assert(security_store_delete(1,NULL)==BLE_HS_EINVAL);
 struct ble_store_status_event e={.event_code=BLE_STORE_EVENT_FULL};
 assert(security_store_status(&e,NULL)==BLE_HS_ESTORE_CAP&&deletes==0);
 e.event_code=BLE_STORE_EVENT_OVERFLOW;assert(security_store_status(&e,NULL)==BLE_HS_ESTORE_CAP);
 e.event_code=9;assert(security_store_status(&e,NULL)==BLE_HS_ENOTSUP);
 assert(security_store_status(NULL,NULL)==BLE_HS_EINVAL);
 fixture();open_rc=42;assert(configure_session_security()==42&&s_security_store_failed);
 fixture();ram.sec.ltk[0]=7;record("our_sec_1",1,sizeof(ram.sec));assert(security_store_validate()==0);
 db[0].size--;assert(security_store_validate()==ESP_ERR_INVALID_SIZE);
 db[0].size++;ram.sec.ltk[0]=8;assert(security_store_validate()==ESP_ERR_INVALID_STATE);
 db[0].value=ram;strcpy(db[0].info.key,"our_sec_5");assert(security_store_validate()==ESP_ERR_INVALID_STATE);
 strcpy(db[0].info.key,"our_sec_0");assert(security_store_validate()==ESP_ERR_INVALID_STATE);
 strcpy(db[0].info.key,"our_sec_x");assert(security_store_validate()==ESP_ERR_INVALID_STATE);
 strcpy(db[0].info.key,"unknown");assert(security_store_validate()==ESP_ERR_INVALID_STATE);
 fixture();record("our_sec_1",1,sizeof(ram.sec));value.sec.ltk[0]=9;
 assert(security_store_write(1,&value)==0&&db[0].value.sec.ltk[0]==9&&commits==1);
 assert(security_store_read(1,&key,&value)==0);
 fixture();record("our_sec_1",1,sizeof(ram.sec));commit_rc=43;value.sec.ltk[0]=1;
 assert(security_store_write(1,&value)==BLE_HS_ESTORE_FAIL&&s_security_store_failed);
 unsigned calls=reads;assert(security_store_read(1,&key,&value)==BLE_HS_ESTORE_FAIL&&reads==calls);
 assert(security_store_write(1,&value)==BLE_HS_ESTORE_FAIL&&writes==1);
 fixture();write_rc=BLE_HS_ESTORE_CAP;assert(security_store_write(1,&value)==BLE_HS_ESTORE_CAP&&!s_security_store_failed);
 fixture();write_rc=BLE_HS_ESTORE_FAIL;assert(security_store_write(1,&value)==BLE_HS_ESTORE_FAIL&&s_security_store_failed);
 fixture();delete_rc=BLE_HS_ESTORE_FAIL;assert(security_store_delete(1,&key)==BLE_HS_ESTORE_FAIL&&s_security_store_failed);
 fixture();delete_rc=BLE_HS_ENOENT;assert(security_store_delete(1,&key)==BLE_HS_ENOENT&&!s_security_store_failed);
 fixture();record("cccd_sec_1",3,sizeof(ram.cccd));value=(union ble_store_value){0};value.cccd.flags=1;
 assert(security_store_write(3,&value)==0&&db[0].value.cccd.flags==1&&commits==1);
 puts("生产绑定接线/NVS恢复/同身份更新/容量与写入失败关闭通过（host替身，不代表无线）");return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--idf-host-src", default="")
    args = parser.parse_args()
    source = (ROOT / "main/ble_bikepower.c").read_text(encoding="utf-8")
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
    startup = extract_function(source, "ble_bikepower_start")
    assert 'MYNEWT_VAL_BLE_SM_BONDING=0' not in cmake
    assert 'CONFIG_BT_NIMBLE_NVS_PERSIST=y' in defaults
    assert 'CONFIG_BT_NIMBLE_MAX_BONDS=4' in defaults
    assert 'CONFIG_BT_NIMBLE_HS_PVCY=y' in defaults
    assert 'CONFIG_BT_NIMBLE_HANDLE_REPEAT_PAIRING_DELETION=n' in defaults
    assert startup.index("configure_session_security()") > startup.index("nimble_port_init()")
    assert startup.index("configure_session_security()") < startup.index("nimble_port_freertos_init(")
    assert source.count("BLE_GATT_CHR_F_WRITE_ENC") >= 2
    assert 's_security_store_failed' in extract_function(source, 'ftms_cp_cb')
    assert 'ble_hs_id_infer_auto(0,' in extract_function(source, 'on_sync'), '不切换本机RPA广播'
    assert 'ble_svc_gatt_changed(' in extract_function(source, 'on_sync'), '绑定客户端必须获知服务表变化'
    sync = extract_function(source, 'on_sync')
    assert sync.index('security_store_activate()') < sync.index('ble_bond_cache_sync(')
    assert sync.index('security_store_activate()') < sync.index('start_advertising()')
    assert 'ble_store_config_init(' not in extract_function(source, 'security_store_activate')
    assert 'ble_store_util_status_rr' not in source
    sigs = [(name, 'static int') for name in ('security_store_read','security_store_write','security_store_delete','security_store_status')]
    sigs += [(name,'static esp_err_t') for name in ('security_store_validate','security_store_sync_value','security_store_activate','configure_session_security')]
    production = functions(source, sigs)
    compiler = os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc')
    assert compiler, '需要host C编译器，不允许跳过'
    flags = ['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation']
    if os.name != 'nt': flags += ['-fsanitize=address,undefined']
    with tempfile.TemporaryDirectory(prefix='bikepower-bond-security-') as temp:
        folder = Path(temp)
        def run(name, content):
            path = folder / (name+'.c')
            path.write_text(content, encoding='utf-8')
            exe = folder / name
            subprocess.run([compiler,*flags,str(path),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
        run('production',PREAMBLE+production+CHECKS)
        if args.idf_host_src:
            upstream = Path(args.idf_host_src)
            sm = (upstream/'ble_sm.c').read_text(encoding='utf-8')
            code = functions(sm,[('ble_sm_chk_store_overflow_by_type','static int'),('ble_sm_chk_repeat_pairing','static int')])
            helpers = r'''
static unsigned count,active;static int count_error,bond_error;
static void ble_hs_lock(void) {} static void ble_hs_unlock(void) {}
static int ble_sm_num_procs(void) {return active;}
static int ble_store_util_count(int t,int *out) {(void)t;*out=count;return count_error;}
static int ble_store_full_event(int t,uint16_t c) {(void)t;(void)c;struct ble_store_status_event e={.event_code=2};return security_store_status(&e,NULL);}
static int ble_sm_read_bond(uint16_t c,struct ble_store_value_sec *v) {(void)c;*v=ram.sec;return bond_error;}
static int ble_gap_repeat_pairing_event(struct ble_gap_repeat_pairing *e) {(void)e;return 0;}
'''
            checks = r'''
int main(void) {
fixture();count=0;assert(ble_sm_chk_store_overflow_by_type(1,1)==0);
count=4;assert(ble_sm_chk_store_overflow_by_type(1,1)==BLE_HS_ESTORE_CAP&&deletes==0);
count=3;active=1;assert(ble_sm_chk_store_overflow_by_type(2,1)==BLE_HS_ESTORE_CAP);
count_error=BLE_HS_ESTORE_FAIL;assert(ble_sm_chk_store_overflow_by_type(1,1)==BLE_HS_ESTORE_FAIL);
bond_error=BLE_HS_ENOENT;assert(ble_sm_chk_repeat_pairing(1,0,16)==0);
bond_error=0;assert(ble_sm_chk_repeat_pairing(1,0,16)==BLE_HS_EALREADY&&deletes==0);
bond_error=BLE_HS_ESTORE_FAIL;assert(ble_sm_chk_repeat_pairing(1,0,16)==BLE_HS_ESTORE_FAIL);
puts("实际IDF容量与重复配对前置分支通过；不自动删除旧绑定");return 0;
}
'''
            run('upstream',PREAMBLE+production+helpers+code+checks)
            store_path = upstream/'ble_store_config.c'
            if not store_path.exists():
                store_path = upstream.parent/'store/config/src/ble_store_config.c'
            store_source = store_path.read_text(encoding='utf-8')
            privacy_source = (upstream/'ble_hs_pvcy.c').read_text(encoding='utf-8')
            host_source = (upstream/'ble_hs.c').read_text(encoding='utf-8')
            assert 'ble_store_config_init();' in extract_function(privacy_source, 'ble_hs_pvcy_set_default_irk')
            host_sync = extract_function(host_source, 'ble_hs_sync')
            assert host_sync.index('ble_hs_startup_go()') < host_sync.index('ble_hs_cfg.sync_cb()')
            actual_init = functions(store_source, [('ble_store_config_init', 'void')])
            sequence_helpers = r'''
#define MYNEWT_VAL_BLE_STATIC_TO_DYNAMIC 0
#define MYNEWT_VAL_ENC_ADV_DATA 0
#define SYSINIT_ASSERT_ACTIVE() ((void)0)
static int ble_store_config_num_our_secs,ble_store_config_num_peer_secs,ble_store_config_num_cccds,ble_store_config_num_csfcs,ble_store_config_num_rpa_recs,ble_store_config_num_local_irks;
static void ble_store_config_conf_init(void) {init_calls++;}
static unsigned cache_calls,advertise_calls,failure_calls,telemetry_calls;
static int security_store_read(int,const union ble_store_key*,union ble_store_value*);
static int security_store_write(int,const union ble_store_value*);
static int security_store_delete(int,const union ble_store_key*);
static bool g_ready,g_start_failed;
static unsigned char g_own_addr_type;
#define BLE_GATT_CHANGED_START_HANDLE 1
#define BLE_GATT_CHANGED_END_HANDLE 65535
static int ble_hs_util_ensure_addr(int n) {(void)n;return 0;}
static int ble_hs_id_infer_auto(int n,unsigned char *t) {(void)n;*t=0;return 0;}
static void mark_start_failed(const char *m,int n) {(void)m;(void)n;failure_calls++;g_start_failed=true;g_ready=false;}
static int ble_bond_cache_sync(bool *changed) {cache_calls++;*changed=false;assert(ble_hs_cfg.store_read_cb==security_store_read);assert(ble_hs_cfg.store_write_cb==security_store_write);assert(ble_hs_cfg.store_delete_cb==security_store_delete);return 0;}
static void ble_svc_gatt_changed(unsigned a,unsigned b) {(void)a;(void)b;}
static int start_advertising(void) {advertise_calls++;assert(ble_hs_cfg.store_read_cb==security_store_read);return 0;}
static void ble_telemetry_service_sync(void) {telemetry_calls++;}
#define ESP_LOGI(...) ((void)0)
'''
            sequence_checks = r'''
int main(void) {
fixture();assert(configure_session_security()==0);assert(init_calls==1);
ble_store_config_init();assert(init_calls==2);
assert(ble_hs_cfg.store_read_cb==ble_store_config_read && ble_hs_cfg.store_write_cb==ble_store_config_write && ble_hs_cfg.store_delete_cb==ble_store_config_delete);
on_sync();assert(init_calls==2 && cache_calls==1 && advertise_calls==1 && telemetry_calls==1 && g_ready && !failure_calls);
ble_store_config_init();s_security_store_failed=true;on_sync();assert(init_calls==3 && failure_calls==1 && cache_calls==1 && advertise_calls==1 && s_security_store_failed && !g_ready);
s_security_store_failed=false;open_rc=42;on_sync();assert(failure_calls==2 && cache_calls==1 && advertise_calls==1 && s_security_store_failed && !g_ready);
puts("真实IDF存储初始化覆盖→生产on_sync重新安装→缓存/广播次序及失败锁存阻断通过（host替身）");return 0;
}
'''
            preamble = PREAMBLE.replace('static void ble_store_config_init(void) { init_calls++; }', 'void ble_store_config_init(void);')
            run('startup_sequence', preamble+sequence_helpers+actual_init+production+'static void '+extract_function(source, 'on_sync')+sequence_checks)

if __name__ == '__main__':
    main()
