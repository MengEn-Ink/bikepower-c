#!/usr/bin/env python3
"""生产订阅布局缓存回归：仅清旧CCCD，保留绑定与配置；host不替代无线。"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[1]
PREAMBLE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 11
#define ESP_ERR_INVALID_STATE 12
#define ESP_ERR_INVALID_SIZE 13
#define ESP_ERR_INVALID_ARG 14
#define ESP_FAIL -1
#define BLE_HS_ENOENT 5
#define BLE_STORE_OBJ_TYPE_CCCD 3
#define BLE_GATTS_CLT_CFG_F_INDICATE 2
#define NIMBLE_ADAPTER_CCCD_INDICATE_FLAG 2
#define CONFIG_BT_NIMBLE_MAX_CCCDS 48
#define BLE_UUID_TYPE_16 16
#define BLE_UUID_TYPE_32 32
#define BLE_UUID_TYPE_128 128
#define BLE_GATT_REGISTER_OP_SVC 1
#define BLE_GATT_REGISTER_OP_CHR 2
#define BLE_GATT_REGISTER_OP_DSC 3
#define BLE_GATT_SVC_UUID16 0x1801
#define BLE_SVC_GATT_CHR_SERVICE_CHANGED_UUID16 0x2a05
#define BLE_BOND_CACHE_FORMAT_VERSION 1
#define BLE_BOND_CACHE_DIGEST_SIZE 32
#define BLE_BOND_CACHE_NAMESPACE "ble_gatt_cache"
#define BLE_BOND_CACHE_LAYOUT_KEY "layout_sha"
#define BLE_BOND_NVS_NAMESPACE "nimble_bond"
#define BLE_BOND_NVS_CCCD_PREFIX "cccd_sec_"
#define NVS_READWRITE 1
#define NVS_READONLY 0
#define NVS_TYPE_ANY 255
#define BLE_UUID16_DECLARE(v) (&(ble_uuid16_t){.u={16},.value=(v)}.u)
typedef int esp_err_t;
typedef unsigned nvs_handle_t;
typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint16_t value; } ble_uuid16_t;
typedef struct { ble_uuid_t u; uint32_t value; } ble_uuid32_t;
typedef struct { ble_uuid_t u; uint8_t value[16]; } ble_uuid128_t;
struct ble_gatt_svc_def { uint8_t type; const ble_uuid_t *uuid; const struct ble_gatt_chr_def *characteristics; };
struct ble_gatt_chr_def { const ble_uuid_t *uuid; uint16_t flags; };
struct ble_gatt_dsc_def { const ble_uuid_t *uuid; uint8_t att_flags; };
struct ble_gatt_register_ctxt { uint8_t op; union {
 struct {uint16_t handle; const struct ble_gatt_svc_def *svc_def;} svc;
 struct {uint16_t def_handle,val_handle;const struct ble_gatt_chr_def *chr_def;const struct ble_gatt_svc_def *svc_def;} chr;
 struct {uint16_t handle;const struct ble_gatt_dsc_def *dsc_def;const struct ble_gatt_chr_def *chr_def;const struct ble_gatt_svc_def *svc_def;} dsc;
}; };
typedef struct {uint8_t type,val[6];} ble_addr_t;
static const ble_addr_t any={0};
#define BLE_ADDR_ANY (&any)
struct ble_store_value_cccd {ble_addr_t peer_addr;uint16_t chr_val_handle,flags;unsigned value_changed:1;};
union ble_store_key { struct {ble_addr_t peer_addr;uint16_t chr_val_handle;uint8_t idx;} cccd; };
union ble_store_value {struct ble_store_value_cccd cccd;};
typedef SHA256_CTX mbedtls_sha256_context;
static void mbedtls_sha256_init(mbedtls_sha256_context *c){memset(c,0,sizeof(*c));}
static void mbedtls_sha256_free(mbedtls_sha256_context *c){(void)c;}
static int mbedtls_sha256_starts(mbedtls_sha256_context*c,int n){assert(!n);return SHA256_Init(c)?0:-1;}
static int mbedtls_sha256_update(mbedtls_sha256_context*c,const unsigned char*p,size_t n){return SHA256_Update(c,p,n)?0:-1;}
static int mbedtls_sha256_finish(mbedtls_sha256_context*c,unsigned char*p){return SHA256_Final(p,c)?0:-1;}
static struct ble_store_value_cccd entries[CONFIG_BT_NIMBLE_MAX_CCCDS];static unsigned count,deletes;
static uint8_t saved[32];static size_t saved_size;static bool have_saved;
static int fail_delete,fail_commit,fail_set,fail_open,fail_find,fail_read,leave_stale;
static int fail_read_at=-1;
static unsigned binding_writes,config_writes,commits;
static int ble_store_config_read(int t,const union ble_store_key*k,union ble_store_value*v){
 assert(t==3);if(fail_read)return fail_read;
 if(fail_read_at>=0 && k->cccd.idx==(unsigned)fail_read_at)return 30;
 for(unsigned i=0,idx=0;i<count;i++){
 if(k->cccd.chr_val_handle && k->cccd.chr_val_handle!=entries[i].chr_val_handle)continue;
 if(k->cccd.peer_addr.val[0] && k->cccd.peer_addr.val[0]!=entries[i].peer_addr.val[0])continue;
 if(idx++==k->cccd.idx){v->cccd=entries[i];return 0;}
 }return BLE_HS_ENOENT;
}
static void ble_store_key_from_value(int t,union ble_store_key*k,const union ble_store_value*v){assert(t==3);memset(k,0,sizeof(*k));k->cccd.peer_addr=v->cccd.peer_addr;k->cccd.chr_val_handle=v->cccd.chr_val_handle;}
static int ble_store_config_delete(int t,const union ble_store_key*k){
 assert(t==3);if(fail_delete)return fail_delete;
 for(unsigned i=0;i<count;i++)if(k->cccd.chr_val_handle==entries[i].chr_val_handle && k->cccd.peer_addr.val[0]==entries[i].peer_addr.val[0]){
 deletes++;memmove(entries+i,entries+i+1,(count-i-1)*sizeof(*entries));count--;return 0;
 }return BLE_HS_ENOENT;
}
static int ble_store_write_cccd(const struct ble_store_value_cccd *v){
 for(unsigned i=0;i<count;i++)if(entries[i].chr_val_handle==v->chr_val_handle && entries[i].peer_addr.val[0]==v->peer_addr.val[0]){entries[i]=*v;return 0;}
 return -1;
}
static int ble_gatts_find_chr(const ble_uuid_t*s,const ble_uuid_t*c,uint16_t*d,uint16_t*v){
 assert(((const ble_uuid16_t*)s)->value==0x1801 && ((const ble_uuid16_t*)c)->value==0x2a05 && d==NULL);
 if(fail_find)return fail_find;*v=8;return 0;
}
static int nvs_open(const char*n,int m,nvs_handle_t*h){
 assert(!strcmp(n,"ble_gatt_cache")||!strcmp(n,"nimble_bond"));(void)m;*h=!strcmp(n,"ble_gatt_cache")?1:2;return fail_open;
}
static void nvs_close(nvs_handle_t h){assert(h==1||h==2);}
static int nvs_get_blob(nvs_handle_t h,const char*k,void*v,size_t*n){
 if(h==1){assert(!strcmp(k,"layout_sha"));if(!have_saved)return ESP_ERR_NVS_NOT_FOUND;assert(*n>=saved_size);memcpy(v,saved,saved_size);*n=saved_size;return 0;}
 assert(h==2);unsigned idx=0;assert(sscanf(k,"cccd_sec_%u",&idx)==1 && idx>0);if(idx>count)return ESP_ERR_NVS_NOT_FOUND;
 assert(*n>=sizeof(*entries));memcpy(v,entries+idx-1,sizeof(*entries));*n=sizeof(*entries);return 0;
}
static int nvs_set_blob(nvs_handle_t h,const char*k,const void*v,size_t n){
 assert(h==1 && !strcmp(k,"layout_sha") && n==32);if(fail_set)return fail_set;memcpy(saved,v,n);saved_size=n;have_saved=true;return 0;
}
static int nvs_commit(nvs_handle_t h){assert(h==1);commits++;return fail_commit;}
typedef struct{char key[16];unsigned type;}nvs_entry_info_t;
static int cursor;typedef int*nvs_iterator_t;
static int nvs_entry_find_in_handle(nvs_handle_t h,int t,nvs_iterator_t*it){assert(h==2&&t==255);cursor=0;*it=count?&cursor:NULL;return count?0:ESP_ERR_NVS_NOT_FOUND;}
static int nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t*i){assert(it);snprintf(i->key,sizeof(i->key),"cccd_sec_%u",(unsigned)(*it+1));return 0;}
static int nvs_entry_next(nvs_iterator_t*it){if(++cursor<(int)count)return 0;*it=NULL;return ESP_ERR_NVS_NOT_FOUND;}
static void nvs_release_iterator(nvs_iterator_t it){(void)it;}
/* 运行实际系统服务的范围更新与编码，其他协议栈依赖由 host 替身提供。 */
#define MYNEWT_VAL(x) 0
#define BLE_GATT_ACCESS_OP_READ_CHR 1
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_GATT_CHANGED_START_HANDLE 1
#define BLE_GATT_CHANGED_END_HANDLE 65535
struct os_mbuf {uint8_t data[4];};
struct ble_gatt_access_ctxt {int op;struct os_mbuf *om;const struct ble_gatt_chr_def *chr;};
static uint16_t ble_svc_gatt_start_handle,ble_svc_gatt_end_handle;
static uint16_t ble_svc_gatt_changed_val_handle=8;
static const struct ble_gatt_chr_def system_chr[1]={{0}};
static const struct ble_gatt_svc_def ble_svc_gatt_defs[1]={{.characteristics=system_chr}};
static unsigned updates;
static void *os_mbuf_extend(struct os_mbuf *m,size_t n){assert(n==4);return m->data;}
static void put_le16(uint8_t *p,uint16_t v){p[0]=v&255;p[1]=v>>8;}
static void ble_gatts_chr_updated(uint16_t h){
 assert(h==8);updates++;
 for(unsigned i=0;i<count;i++)if(entries[i].chr_val_handle==h && (entries[i].flags&2))entries[i].value_changed=1;
}
'''
CHECKS = r'''
static void layout(unsigned handle,unsigned uuid,unsigned flags){
 static ble_uuid16_t s={{16},0x1801};ble_uuid16_t c={{16},uuid};
 struct ble_gatt_svc_def svc={.type=1,.uuid=&s.u};struct ble_gatt_chr_def chr={&c.u,flags};
 ble_bond_cache_begin();struct ble_gatt_register_ctxt a={.op=1,.svc={1,&svc}};ble_bond_cache_register(&a,NULL);
 struct ble_gatt_register_ctxt b={.op=2,.chr={handle-1,handle,&chr,&svc}};ble_bond_cache_register(&b,NULL);
}
static void fixture(void){
 count=3;entries[0]=(struct ble_store_value_cccd){.chr_val_handle=8,.flags=2};
 entries[1]=(struct ble_store_value_cccd){.peer_addr={.val={1}},.chr_val_handle=38,.flags=1};
 entries[2]=(struct ble_store_value_cccd){.peer_addr={.val={2}},.chr_val_handle=59,.flags=2};
 deletes=commits=0;have_saved=false;saved_size=32;fail_delete=fail_commit=fail_set=fail_open=fail_find=fail_read=leave_stale=0;
 fail_read_at=-1;
}
int main(void){
 fixture();bool changed=false;layout(8,0x2a05,2);assert(ble_bond_cache_sync(&changed)==0&&changed&&count==1&&deletes==2&&commits==1);
 assert(entries[0].chr_val_handle==8&&entries[0].flags==2&&entries[0].value_changed && binding_writes==0&&config_writes==0);
 changed=true;assert(ble_bond_cache_sync(&changed)==0&&!changed&&commits==1);
 layout(8,0x2a05,2);changed=true;assert(ble_bond_cache_sync(&changed)==0&&!changed&&deletes==2&&commits==1);
 entries[count++]=(struct ble_store_value_cccd){.chr_val_handle=80,.flags=1};layout(9,0x2a05,2);assert(ble_bond_cache_sync(&changed)==0&&changed&&count==1&&deletes==3);
 layout(9,0x2a06,2);assert(ble_bond_cache_sync(&changed)==0&&changed);
 layout(9,0x2a06,1);assert(ble_bond_cache_sync(&changed)==0&&changed);
 fixture();fail_delete=27;layout(8,0x2a05,2);assert(ble_bond_cache_sync(&changed)!=0&&!have_saved&&commits==0);
 fixture();fail_commit=28;layout(8,0x2a05,2);assert(ble_bond_cache_sync(&changed)==28);have_saved=false;fail_commit=0;layout(8,0x2a05,2);assert(ble_bond_cache_sync(&changed)==0&&changed&&count==1);
 fixture();fail_find=29;layout(8,0x2a05,2);assert(ble_bond_cache_sync(&changed)!=0&&deletes==0&&!have_saved);
 fixture();fail_read=30;layout(8,0x2a05,2);assert(ble_bond_cache_sync(&changed)!=0&&deletes==0&&!have_saved);
 fixture();have_saved=true;saved_size=31;layout(8,0x2a05,2);assert(ble_bond_cache_sync(&changed)==ESP_ERR_INVALID_SIZE&&deletes==0);
 fixture();layout(8,0x2a05,2);assert(ble_bond_cache_sync(NULL)==ESP_ERR_INVALID_STATE);
 /* 首轮启动设置范围后，在确认前重启；摘要不变但待确认订阅仍持久保存。 */
 fixture();layout(8,0x2a05,2);assert(ble_bond_cache_sync(&changed)==0&&changed);
 ble_svc_gatt_changed(1,65535);
 ble_svc_gatt_start_handle=ble_svc_gatt_end_handle=0;updates=0;
 layout(8,0x2a05,2);assert(ble_bond_cache_sync(&changed)==0&&!changed&&commits==1);
 struct os_mbuf m={0};struct ble_gatt_access_ctxt access={.op=1,.om=&m,.chr=system_chr};
 assert(ble_svc_gatt_access(1,8,&access,NULL)==0);
 assert(m.data[0]==1 && m.data[1]==0 && m.data[2]==255 && m.data[3]==255);
 assert(updates==1 && entries[0].value_changed);
 /* 已确认与未订阅记录不触发重新发现。 */
 entries[0].value_changed=0;updates=0;
 assert(ble_bond_cache_sync(&changed)==0&&!changed&&updates==0);
 entries[0].value_changed=1;entries[0].flags=0;
 assert(ble_bond_cache_sync(&changed)==0&&!changed&&updates==0);
 entries[0].flags=2;fail_read=30;
 assert(ble_bond_cache_sync(&changed)!=0&&updates==0&&commits==1);
 fail_read=0;entries[count++]=(struct ble_store_value_cccd){.peer_addr={.val={1}},.chr_val_handle=8,.flags=2,.value_changed=1};
 assert(ble_bond_cache_sync(&changed)==0&&!changed&&updates==1&&count==2);
 updates=0;fail_read_at=1;
 assert(ble_bond_cache_sync(&changed)!=0&&updates==0);
 fail_read_at=-1;
 /* 满容量仍有界检查，保留每个 peer 的指示订阅。 */
 count=CONFIG_BT_NIMBLE_MAX_CCCDS;
 for(unsigned i=0;i<count;i++)entries[i]=(struct ble_store_value_cccd){.peer_addr={.val={i}},.chr_val_handle=8,.flags=2,.value_changed=1};
 assert(ble_bond_cache_sync(&changed)==0&&!changed&&updates==1&&count==CONFIG_BT_NIMBLE_MAX_CCCDS);
 puts("GATT布局：首次/同表/句柄及UUID变化/属性变化、跨peer旧订阅清理、保留ServiceChanged与密钥/配置、故障拒启动通过");return 0;
}
'''
def extract_function(source, name):
    start = re.search(r'\b' + re.escape(name) + r'\([^;{}]*\)\s*\{', source).start()
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

def main():
    source = (ROOT / 'main/ble_bond_cache.c').read_text(encoding='utf-8')
    production = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
    startup = (ROOT / 'main/ble_bikepower.c').read_text(encoding='utf-8')
    assert 'ble_hs_cfg.gatts_register_cb = ble_bond_cache_register;' in startup
    assert startup.index('ble_bond_cache_sync(&layout_changed)') < startup.index('rc = start_advertising();', startup.index('static void on_sync'))
    assert 'ble_store_config_delete(BLE_STORE_OBJ_TYPE_CCCD' in production
    assert 'BLE_STORE_OBJ_TYPE_OUR_SEC' not in production and 'BLE_STORE_OBJ_TYPE_PEER_SEC' not in production
    idf = Path(os.environ.get('IDF_PATH', str(Path.home() / 'esp/esp-idf-v5.5.4')))
    service = idf / 'components/bt/host/nimble/nimble/nimble/host/services/gatt/src/ble_svc_gatt.c'
    if service.is_file():
        system = service.read_text(encoding='utf-8')
        actual = ('static int ' + extract_function(system, 'ble_svc_gatt_access') +
                  '\nvoid ' + extract_function(system, 'ble_svc_gatt_changed'))
        print('执行本地 IDF 系统服务范围更新与编码函数', flush=True)
    else:
        if os.environ.get('BIKEPOWER_TEST_REQUIRE_IDF') == '1':
            raise FileNotFoundError('未找到系统服务源码，请设置 IDF_PATH')
        print('未安装 IDF：执行 host 范围替身；不计为实际 IDF 编码验证', flush=True)
        actual = r'''
static int ble_svc_gatt_access(uint16_t c,uint16_t h,struct ble_gatt_access_ctxt *a,void *v){
 (void)c;(void)h;(void)v;assert(a->op==1 && a->chr==ble_svc_gatt_defs[0].characteristics);
 uint8_t *p=os_mbuf_extend(a->om,4);
 put_le16(p,ble_svc_gatt_start_handle);put_le16(p+2,ble_svc_gatt_end_handle);return 0;
}
static void ble_svc_gatt_changed(uint16_t a,uint16_t b){
 ble_svc_gatt_start_handle=a;ble_svc_gatt_end_handle=b;
 ble_gatts_chr_updated(ble_svc_gatt_changed_val_handle);
}
'''
    with tempfile.TemporaryDirectory(prefix='bikepower-cache-test-') as temp:
        path = Path(temp)/'test.c';path.write_text(PREAMBLE+actual+production+CHECKS,encoding='utf-8')
        exe = Path(temp)/'test'
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-Wno-deprecated-declarations','-fsanitize=address,undefined',str(path),'-lcrypto','-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__ == '__main__':main()
