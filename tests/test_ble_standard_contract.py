#!/usr/bin/env python3
"""运行生产 FTMS 回调与广播组装，校验标准位图、边界和控制事务。"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from check_wifi_done_contract import extract_function


def functions(source, signatures):
    result = []
    for name, signature in signatures:
        definition = re.search(r"^" + re.escape(signature) + r"\s+" + name + r"\([^;]*\)\s*\{",
                               source, re.M)
        body = extract_function(source[definition.start():], name) if definition else ""
        assert body, f"缺少生产函数 {name}"
        result.append(signature + " " + body)
    return "\n".join(result)


def run(source):
    with tempfile.TemporaryDirectory(prefix="bikepower-standard-") as temp:
        folder = Path(temp)
        (folder / "esp_err.h").write_text("#pragma once\n")
        file = folder / "contract.c"
        file.write_text(source)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-I", str(folder), "-I", str(ROOT / "test"),
                        "-I", str(ROOT / "main"), "-I",
                        str(ROOT / "components/bikepower_domain/include"),
                        str(file), "-o", str(folder / "contract")], check=True)
        subprocess.run([str(folder / "contract")], check=True)


PREAMBLE = r"""
#include <assert.h>
#include <stdio.h>
#include <stdatomic.h>
#include "ble_service_stubs.h"
#include "bikepower_config.h"
#include "domain_config.h"
#include "ble_bikepower.h"
unsigned s_lock_depth;
static inline __attribute__((unused)) void test_log(const char *tag,const char *format,...)
{(void)tag;(void)format;}
#define TAG "BLE"
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define BLE_HS_CONN_HANDLE_NONE 0xffff
#define ESP_LOGD(...) ((void)0)
int g_profile;
static bool g_stopping, g_ready;
static atomic_bool s_security_store_failed __attribute__((unused));
static uint16_t g_conn_handle;
static uint8_t g_broadcast_mask;
static bool g_bridge_mode __attribute__((unused));
static portMUX_TYPE s_data_lock;
static struct { bool real; } s_output;
static int chr_read_buf(struct ble_gatt_access_ctxt *c, const uint8_t *b, size_t n)
{ return os_mbuf_append(c->om, b, n); }
"""

FEATURE_CHECKS = r"""
static void read_value(ble_gatt_access_fn *cb, const uint8_t *expected, size_t n)
{
    struct os_mbuf om = {0};
    struct ble_gatt_access_ctxt c = {.op=BLE_GATT_ACCESS_OP_READ_CHR, .om=&om};
    assert(cb(1, 2, &c, NULL) == 0);
    assert(om.length == n && memcmp(om.data, expected, n) == 0);
    c.op=BLE_GATT_ACCESS_OP_WRITE_CHR;
    assert(cb(1, 2, &c, NULL) == BLE_ATT_ERR_UNLIKELY);
    c.op=BLE_GATT_ACCESS_OP_READ_CHR;
    om.length=sizeof(om.data);
    assert(cb(1, 2, &c, NULL) != 0);
}
int main(void)
{
    (void)g_stopping; (void)g_ready; (void)g_conn_handle; (void)g_broadcast_mask;
    (void)s_data_lock;
    const uint8_t cycling[]={2,68,0,0,8,0,0,0};
    const uint8_t bridge[]={2,68,0,0,8,0,0,0};
    const uint8_t running[]={4,20,0,0,1,0,0,0};
    const uint8_t power[]={0,0,0xd0,7,1,0};
    const uint8_t speed[]={0x2c,1,0xc4,9,1,0};
    g_profile=SPORT_PROFILE_CYCLING;
    read_value(ftms_feat_cb,cycling,sizeof(cycling));
    read_value(ftms_power_range_cb,power,sizeof(power));
    s_output.real=true;
    read_value(ftms_feat_cb,bridge,sizeof(bridge));
    g_profile=SPORT_PROFILE_RUNNING;
    read_value(ftms_feat_cb,running,sizeof(running));
    read_value(ftms_speed_range_cb,speed,sizeof(speed));
    assert(s_lock_depth==0);
    puts("FTMS Feature/范围：骑行、桥接、跑步、小端边界与只读错误路径通过");
}
"""

CP_STUBS = r"""
#define BLE_ATT_ERR_CCCD_IMPROPERLY_CONFIGURED FTMS_ATT_ERR_CCCD_IMPROPERLY_CONFIGURED
#define BLE_ATT_ERR_PROCEDURE_ALREADY_IN_PROGRESS FTMS_ATT_ERR_PROCEDURE_IN_PROGRESS
static atomic_bool g_control_async;
static atomic_uchar g_control_op, g_control_parameter;
static atomic_bool g_ftms_subscribed, g_ftms_control, g_ftms_pending;
static uint16_t g_ftms_cp_handle=9, g_ftms_status_handle=10;
static uint8_t g_ftms_resp_buf[3], g_ftms_status_buf[2];
static size_t g_ftms_status_len;
static ble_erg_cb_t g_erg_cb;
static ble_erg_clear_cb_t g_erg_clear_cb;
static ble_run_control_cb_t g_run_control_cb;
static unsigned calls, responses, statuses;
static uint32_t g_ftms_response_token;
static atomic_uint_least32_t s_control_generation;
static portMUX_TYPE s_indication_lock;
static uint16_t g_ftms_owner=BLE_HS_CONN_HANDLE_NONE,g_ftms_connection=BLE_HS_CONN_HANDLE_NONE;
static void *client_find(uint16_t c){return c==g_conn_handle?(void*)1:NULL;}
static bool client_indicates(uint16_t c,uint16_t h){(void)h;return c==g_conn_handle&&g_ftms_subscribed;}
static bool indication_submit(uint32_t token,const uint8_t *b,size_t n);
static bool capacity=true;
static uint32_t indication_reserve(uint16_t c,uint16_t h)
{assert(c==1 && h==9);return capacity?1:0;}
static uint8_t response[3];
static bool accepted=true, send_ok=true;
static int value, control_type;
static bool indicate_try(uint16_t h,const uint8_t *b,size_t n)
{ assert(h==9 && n==3); responses++; memcpy(response,b,n); return send_ok; }
static bool indication_submit(uint32_t token,const uint8_t *b,size_t n)
{(void)token;return indicate_try(9,b,n);}
#define indicate_one(h,b,n) ((void)indicate_try(h,b,n))
static void notify_one(uint16_t h,const uint8_t *b,size_t n)
{ assert(h==10 && n==(b[0]==2?2u:1u)); statuses++; }
static bool erg(int v) { calls++; value=v; return accepted; }
static bool clear(int op) { calls++; value=op; return accepted; }
static bool run_control(ble_run_control_type_t t,int v)
{ calls++; value=v; control_type=t; return accepted; }
static void reset(void)
{
    g_ftms_subscribed=true; g_ftms_control=true; g_ftms_pending=false;
    g_ftms_owner=1;g_ftms_connection=1;
    g_profile=SPORT_PROFILE_CYCLING; g_conn_handle=1; g_ready=true; g_stopping=false;
    accepted=true; send_ok=true; capacity=true; calls=responses=statuses=0;
    g_erg_cb=erg; g_erg_clear_cb=clear; g_run_control_cb=run_control;
}
static int write_bytes(const uint8_t *b,size_t n)
{
    struct os_mbuf om={0}; memcpy(om.data,b,n); om.length=n;
    struct ble_gatt_access_ctxt c={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=&om};
    return ftms_cp_cb(1,9,&c,NULL);
}
static void expect(const uint8_t *b,size_t n,uint8_t result,unsigned count)
{
    assert(write_bytes(b,n)==0);
    assert(responses==1 && response[0]==0x80 && response[1]==b[0] && response[2]==result);
    assert(calls==count);
}
"""

CP_CHECKS = r"""
int main(void)
{
    (void)chr_read_buf; (void)g_broadcast_mask; (void)s_output;
    const uint8_t request[]={0}, power[]={5,200,0}, speed[]={2,0xe8,3};
    const uint8_t start[]={7}, stop[]={8,1}, pause[]={8,2}, reset_op[]={1}, unknown[]={0xff};
    reset(); g_ftms_subscribed=false;
    assert(write_bytes(power,3)==BLE_ATT_ERR_CCCD_IMPROPERLY_CONFIGURED && calls==0 && responses==0);
    reset(); g_ftms_pending=true;
    assert(write_bytes(power,3)==BLE_ATT_ERR_PROCEDURE_ALREADY_IN_PROGRESS && calls==0);
    reset(); capacity=false;
    assert(write_bytes(power,3)==BLE_ATT_ERR_INSUFFICIENT_RES && calls==0 && responses==0);
    reset(); g_ftms_control=false;
    expect(power,3,5,0);
    reset(); g_ftms_control=false;
    expect(request,1,1,0); assert(g_ftms_control && g_ftms_pending);
    assert(write_bytes(power,3)==BLE_ATT_ERR_PROCEDURE_ALREADY_IN_PROGRESS);
    reset(); expect(power,3,1,1); assert(value==200);
    reset(); accepted=false; expect(power,3,4,1);
    reset(); g_erg_cb=NULL; expect(power,3,4,0);
    reset(); s_output.real=true; expect(power,3,4,0); s_output.real=false;
    for(int v=-1;v<=2001;v+=2002) {
        reset(); uint8_t b[]={5,(uint8_t)v,(uint8_t)(v>>8)}; expect(b,3,3,0);
    }
    for(int v=0;v<=2000;v+=2000) {
        reset(); uint8_t b[]={5,(uint8_t)v,(uint8_t)(v>>8)}; expect(b,3,1,1);
    }
    reset(); expect(speed,3,2,0);
    reset(); g_profile=SPORT_PROFILE_RUNNING; expect(power,3,2,0);
    reset(); g_profile=SPORT_PROFILE_RUNNING; expect(speed,3,1,1);
    assert(value==1000 && control_type==BLE_RUN_CTRL_SPEED);
    for(int v=299;v<=2501;v+=2202) {
        reset(); g_profile=SPORT_PROFILE_RUNNING;
        uint8_t b[]={2,(uint8_t)v,(uint8_t)(v>>8)}; expect(b,3,3,0);
    }
    for(int v=300;v<=2500;v+=2200) {
        reset(); g_profile=SPORT_PROFILE_RUNNING;
        uint8_t b[]={2,(uint8_t)v,(uint8_t)(v>>8)}; expect(b,3,1,1);
    }
    const uint8_t *ops[]={start,stop,pause,reset_op};
    const size_t lengths[]={1,2,2,1};
    for(size_t i=0;i<4;i++) {
        reset(); g_profile=SPORT_PROFILE_RUNNING; expect(ops[i],lengths[i],1,1);
        assert(statuses==1);
        assert(g_ftms_status_buf[0]==(i==0?4:i==3?1:2));
        if(i==1||i==2) assert(g_ftms_status_buf[1]==(i==1?1:2));
        reset(); g_profile=SPORT_PROFILE_RUNNING; accepted=false;
        expect(ops[i],lengths[i],4,1); assert(statuses==0);
        reset(); expect(ops[i],lengths[i],1,i==0?0:1);
    }
    reset(); expect(unknown,1,2,0);
    reset(); assert(write_bytes(power,0)==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN && calls==0);
    reset(); expect(power,2,3,0);
    reset(); uint8_t extra[]={5,200,0,0}; expect(extra,4,3,0);
    reset(); uint8_t invalid_stop[]={8,3}; expect(invalid_stop,2,3,0);
    reset(); uint8_t long_packet[21]={5};
    assert(write_bytes(long_packet,21)==BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN && calls==0);
    reset(); send_ok=false; g_ftms_control=false;
    assert(write_bytes(request,1)==BLE_ATT_ERR_INSUFFICIENT_RES);
    assert(!g_ftms_pending && !g_ftms_control);
    reset(); g_conn_handle=2; assert(write_bytes(power,3)!=0 && calls==0);
    assert(s_lock_depth==0);
    puts("FTMS 控制：订阅/控制权/串行、所有操作码、参数边界、回调与发送失败通过");
}
"""

ADV_STUBS = r"""
#define BLE_HS_ADV_F_DISC_GEN 2
#define BLE_HS_ADV_F_BREDR_UNSUP 4
#define BLE_GAP_CONN_MODE_UND 1
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_HS_FOREVER 0
typedef struct {uint16_t value;} ble_uuid16_t;
#define BLE_UUID16_INIT(n) {n}
struct ble_gap_adv_params {int conn_mode,disc_mode,itvl_min,itvl_max;};
struct ble_hs_adv_fields {
    uint8_t flags;
    const ble_uuid16_t *uuids16;
    size_t num_uuids16;
    bool uuids16_is_complete;
    const uint8_t *name;
    size_t name_len;
    bool name_is_complete;
    const uint8_t *svc_data_uuid16;
    size_t svc_data_uuid16_len;
};
int ble_bikepower_conn_count(void){return 0;}
static bool ble_gap_adv_active(void){return false;}
static bool s_rebuild_pending;
static uint8_t g_own_addr_type;
static char g_device_name[DEVICE_NAME_MAX];
static int fail_at, step;
static unsigned primary_count, response_count;
static int gap_event_cb(void *event,void *arg) {(void)event;(void)arg;return 0;}
static int fields_check(const struct ble_hs_adv_fields *f,bool rsp)
{
    step++; if(step==fail_at) return 7;
    size_t size=(f->flags?3:0)+(f->num_uuids16?2+2*f->num_uuids16:0)
        +(f->name_len?2+f->name_len:0)+(f->svc_data_uuid16_len?2+f->svc_data_uuid16_len:0);
    assert(size<=31 && !f->uuids16_is_complete);
    if(f->name_len<strlen(g_device_name)) assert((f->name[f->name_len]&0xc0)!=0x80);
    if(rsp) {
        response_count++; assert(f->name_is_complete && f->name_len==strlen(g_device_name));
        assert(f->num_uuids16==((g_broadcast_mask&4)?1:0));
    } else {
        primary_count++;
        bool ftms=(g_broadcast_mask&3)!=0 && (g_profile==SPORT_PROFILE_RUNNING || !g_bridge_mode);
        assert((f->svc_data_uuid16_len==5)==ftms);
        if(ftms) {
            assert(f->svc_data_uuid16[0]==0x26 && f->svc_data_uuid16[1]==0x18);
            assert(f->svc_data_uuid16[2]==1 && f->svc_data_uuid16[4]==0);
            assert(f->svc_data_uuid16[3]==(g_profile==SPORT_PROFILE_RUNNING?1:32));
        }
    }
    return 0;
}
static int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *f) {return fields_check(f,false);}
static int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *f) {return fields_check(f,true);}
static int ble_gap_adv_start(uint8_t t,void *a,int d,const struct ble_gap_adv_params *p,
                             int (*cb)(void*,void*),void *arg)
{
    (void)t;(void)a;(void)d;(void)cb;(void)arg;
    assert(p->conn_mode==1 && p->disc_mode==2); step++; return step==fail_at?7:0;
}
"""

ADV_CHECKS = r"""
int main(void)
{
    (void)chr_read_buf; (void)g_ready; (void)g_conn_handle; (void)s_output;
    const char *names[]={"BikePower","12345678901234567890123","骑行功率计开发"};
    for(int p=0;p<2;p++) for(int m=1;m<=7;m++) for(size_t n=0;n<3;n++) {
        g_profile=p;g_broadcast_mask=m;
        strcpy(g_device_name,names[n]);step=0;
        assert(start_advertising()==0 && step==3);
    }
    for(int i=1;i<=3;i++) {step=0;fail_at=i;assert(start_advertising()==7);}
    step=0;g_stopping=true;assert(start_advertising()==0 && step==0);
    assert(primary_count==42+2 && response_count==42+1 && s_lock_depth==0);
    primary_count=response_count=0;g_bridge_mode=true;g_stopping=false;fail_at=0;
    for(int p=0;p<2;p++)for(int m=1;m<=7;m++){
        g_profile=p;g_broadcast_mask=m;step=0;assert(start_advertising()==0);
    }
    assert(primary_count==14&&response_count==14);
    puts("广播：双模式×7 掩码×3 名称、31 字节预算、UTF-8、错误返回与停止守卫通过");
}
"""

GAP_STUBS = r"""
#define BLE_GAP_EVENT_CONNECT 1
#define BLE_GAP_EVENT_DISCONNECT 2
#define BLE_GAP_EVENT_ADV_COMPLETE 3
#define BLE_GAP_EVENT_SUBSCRIBE 4
#define BLE_GAP_EVENT_NOTIFY_TX 5
#define BLE_GAP_EVENT_ENC_CHANGE 6
#define BLE_HS_EDONE 14
#define BLE_ERR_UNSUPP_REM_FEATURE 26
#define BLE_GAP_ROLE_SLAVE 1
struct ble_gap_conn_desc {uint16_t conn_handle;uint8_t role;};
static int ble_gap_conn_find(uint16_t h,struct ble_gap_conn_desc *d)
{if(h!=1)return 7;d->conn_handle=h;d->role=BLE_GAP_ROLE_SLAVE;return 0;}
struct ble_gap_event {
    int type;
    struct {int status;uint16_t conn_handle;} connect;
    struct {int reason;struct ble_gap_conn_desc conn;} disconnect;
    struct {uint16_t conn_handle,attr_handle;bool cur_indicate,cur_notify;} subscribe;
    struct {uint16_t conn_handle,attr_handle;bool indication;int status;} notify_tx;
};
static atomic_bool g_ftms_subscribed,g_ftms_control,g_ftms_pending;
static uint16_t g_ftms_cp_handle=9;
static int ads,closed;
static int start_advertising(void) {ads++;return 0;}
static void ble_config_service_disconnect(void) {closed++;}
static void ble_telemetry_service_connect(uint16_t connection) {(void)connection;}
static void ble_telemetry_service_disconnect(void) {}
static void ble_telemetry_service_subscribe(uint16_t handle,bool notify) {(void)handle;(void)notify;}
static void mark_start_failed(const char *reason,int rc) {(void)reason;(void)rc;assert(false);}
static void ble_power_bridge_host_reset(void) {}
static void subscription_update(uint16_t h,bool e) {(void)h;(void)e;}
static uint16_t g_rsc_cp_handle=11;
static bool g_rsc_subscribed;
static void indication_reset(void) {}
static void indication_subscription_revoked(uint16_t h) {if(h==9) g_ftms_control=false;}
static void indication_complete(uint16_t connection,uint16_t handle,int status)
{
    if(connection==g_conn_handle && handle==g_ftms_cp_handle && status) {
        g_ftms_pending=false;
        if(status!=BLE_HS_EDONE) g_ftms_control=false;
    }
}
"""

GAP_CHECKS = r"""
int main(void)
{
    (void)chr_read_buf;(void)g_ready;(void)g_broadcast_mask;(void)s_data_lock;(void)s_output;
    g_ready=true;g_conn_handle=BLE_HS_CONN_HANDLE_NONE;
    struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={.status=0,.conn_handle=1}};
    g_ftms_pending=g_ftms_control=g_ftms_subscribed=true;gap_event_cb(&e,NULL);
    assert(g_conn_handle==1 && !g_ftms_pending && !g_ftms_control && !g_ftms_subscribed);
    e.type=BLE_GAP_EVENT_SUBSCRIBE;e.subscribe.conn_handle=1;e.subscribe.attr_handle=9;
    e.subscribe.cur_indicate=true;gap_event_cb(&e,NULL);assert(g_ftms_subscribed);
    e.subscribe.conn_handle=2;e.subscribe.cur_indicate=false;gap_event_cb(&e,NULL);
    assert(g_ftms_subscribed);
    e.type=BLE_GAP_EVENT_NOTIFY_TX;
    e.notify_tx.conn_handle=1;e.notify_tx.attr_handle=9;e.notify_tx.indication=true;
    g_ftms_pending=g_ftms_control=true;gap_event_cb(&e,NULL);assert(g_ftms_pending);
    e.notify_tx.attr_handle=10;e.notify_tx.status=BLE_HS_EDONE;
    gap_event_cb(&e,NULL);assert(g_ftms_pending);
    e.notify_tx.attr_handle=9;gap_event_cb(&e,NULL);
    assert(!g_ftms_pending && g_ftms_control);
    g_ftms_pending=true;e.notify_tx.status=99;gap_event_cb(&e,NULL);
    assert(!g_ftms_pending && !g_ftms_control);
    e.type=BLE_GAP_EVENT_SUBSCRIBE;e.subscribe.conn_handle=1;e.subscribe.attr_handle=9;
    g_ftms_control=true;gap_event_cb(&e,NULL);assert(!g_ftms_subscribed && !g_ftms_control);
    e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=1;
    g_ftms_pending=g_ftms_control=g_ftms_subscribed=true;
    gap_event_cb(&e,NULL);
    assert(g_conn_handle==0xffff && !g_ftms_pending && !g_ftms_control && !g_ftms_subscribed);
    assert(ads==1 && closed==1);
    g_ftms_pending=g_ftms_control=g_ftms_subscribed=true;g_conn_handle=1;on_reset(3);
    assert(g_conn_handle==0xffff && !g_ftms_pending && !g_ftms_control && !g_ftms_subscribed);
    puts("GAP：订阅、其他句柄隔离、发送/确认/失败、断开与复位事务清理通过");
}
"""

TX_STUBS = r"""
static int g_notify_lock=1;
static bool lock_ok=true,alloc_ok=true;
static int send_rc,freed,sends,terminated;
static uint16_t g_ftms_cp_handle=9,g_rsc_cp_handle=11;
static atomic_bool g_ftms_pending,g_ftms_control,g_rsc_pending,g_rsc_subscribed;
static uint32_t g_rsc_response_token;
static void subscriptions_reset(void) {}
#define BLE_HS_EDONE 14
#define BLE_HS_ENOMEM 6
#define BLE_ERR_REM_USER_CONN_TERM 19
static uint16_t last_handle;
static uint8_t last_payload[12];
static bool synchronous_ack;
#define pdTRUE 1
static int xSemaphoreTake(int lock,int ticks) {assert(lock==1 && ticks==0);return lock_ok;}
static void xSemaphoreGive(int lock) {assert(lock==1);freed++;}
static struct os_mbuf *ble_hs_mbuf_from_flat(const uint8_t *b,size_t n)
{static struct os_mbuf om;memcpy(om.data,b,n);om.length=n;return alloc_ok?&om:NULL;}
static void indication_complete(uint16_t,uint16_t,int);
static int ble_gatts_indicate_custom(uint16_t conn,uint16_t h,struct os_mbuf *om)
{
    assert(conn==1 && om && s_lock_depth==0);sends++;last_handle=h;
    memcpy(last_payload,om->data,om->length);
    indication_complete(conn,h,0);
    if(synchronous_ack) indication_complete(conn,h,BLE_HS_EDONE);
    return send_rc;
}
static int ble_gap_terminate(uint16_t c,int reason)
{assert(c==1 && reason==19 && s_lock_depth==0);terminated++;return 0;}
"""

TX_CHECKS = r"""
int main(void)
{
    (void)chr_read_buf;(void)g_broadcast_mask;(void)s_data_lock;(void)s_output;
    g_ready=true;g_conn_handle=1;uint8_t b[]={0x80,0,1};
    g_ftms_pending=g_ftms_control=true;
    g_ftms_response_token=indication_reserve(1,9);
    assert(indicate_try(9,b,3) && sends==1 && g_ftms_pending);
    uint32_t token=indication_reserve(1,10);
    uint8_t second[]={2,3,4};
    indicate_one(token,second,3);second[0]=99;
    uint32_t third=indication_reserve(1,10);
    indicate_one(third,b,3);
    assert(sends==1 && s_indication_count==3);
    indication_complete(2,9,BLE_HS_EDONE);
    indication_complete(1,10,BLE_HS_EDONE);
    indication_complete(1,9,0);
    assert(s_indication_count==3 && g_ftms_pending);
    indication_complete(1,9,BLE_HS_EDONE);
    assert(!g_ftms_pending && g_ftms_control);
    indication_pump_nolock();
    assert(sends==2 && last_handle==10 && last_payload[0]==2);
    indication_complete(1,10,BLE_HS_EDONE);
    indication_pump_nolock();assert(sends==3);
    indication_complete(1,10,BLE_HS_EDONE);
    assert(s_indication_count==0);
    // 异步 COMMIT 的预留位置先于后续响应，未完成时不得越过。
    token=indication_reserve(1,10);
    third=indication_reserve(1,9);
    assert(indication_submit(third,b,3) && sends==3);
    assert(indication_submit(token,b,3) && sends==4);
    indication_complete(1,10,BLE_HS_EDONE);
    indication_pump_nolock();assert(sends==5);
    indication_complete(1,9,BLE_HS_EDONE);
    // 满队列、错误输入与生命周期守卫。
    for(int i=0;i<BLE_INDICATION_QUEUE_SIZE;i++) assert(indication_reserve(1,10));
    assert(!indication_reserve(1,10));
    indication_reset();assert(!indication_submit(token,b,3));
    token=indication_reserve(1,10);
    assert(!indication_submit(token,NULL,3) && !indication_submit(token,b,13));
    g_ready=false;assert(!indication_reserve(1,9));
    g_ready=true;g_stopping=true;assert(!indication_reserve(1,9));
    g_stopping=false;assert(!indication_reserve(2,9) && !indication_reserve(1,0));
    indication_reset();
    // 锁忙仍保存响应，下次周期再发；不持有调用者的缓冲指针。
    lock_ok=false;token=indication_reserve(1,10);
    assert(indication_submit(token,b,3) && sends==5);
    lock_ok=true;indication_pump_nolock();assert(sends==6);
    indication_complete(1,10,99);indication_pump_nolock();
    assert(terminated==1 && !indication_reserve(1,9) && sends==6);
    indication_reset();
    alloc_ok=false;token=indication_reserve(1,9);
    g_ftms_pending=g_ftms_control=true;
    assert(indication_submit(token,b,3));
    assert(!g_ftms_pending && !g_ftms_control && s_indication_failed);
    indication_reset();alloc_ok=true;send_rc=3;
    token=indication_reserve(1,9);assert(indication_submit(token,b,3));
    assert(s_indication_failed);
    indication_reset();send_rc=0;synchronous_ack=true;
    token=indication_reserve(1,9);assert(indication_submit(token,b,3));
    assert(!s_indication_active && s_indication_count==0 && s_lock_depth==0);
    synchronous_ack=false;indication_reset();
    g_ftms_pending=g_ftms_control=true;
    g_ftms_response_token=indication_reserve(1,9);
    third=indication_reserve(1,10);
    assert(indication_submit(third,b,3));
    int before=sends;
    indication_subscription_revoked(9);
    assert(!g_ftms_pending && !g_ftms_control && !g_ftms_response_token && s_indication_count==1);
    assert(!indication_submit(token,b,3));
    indication_pump_nolock();assert(sends==before+1 && last_handle==10);
    indication_complete(1,10,BLE_HS_EDONE);
    for(int i=0;i<32;i++) {
        g_rsc_pending=true;g_rsc_response_token=indication_reserve(1,11);
        assert(g_rsc_response_token);
        indication_subscription_revoked(11);
        assert(!g_rsc_pending && !g_rsc_response_token && s_indication_count==0);
    }
    g_ftms_pending=g_ftms_control=true;
    g_ftms_response_token=indication_reserve(1,9);
    assert(indication_submit(g_ftms_response_token,b,3));
    indication_subscription_revoked(9);
    assert(g_ftms_pending && !g_ftms_control && !g_ftms_response_token && s_indication_active);
    indication_complete(1,9,BLE_HS_EDONE);
    assert(!g_ftms_pending && !s_indication_active && s_indication_count==0);
    puts("Indication：跨特征 FIFO、复制、容量、异步预留、确认、失败断连、退订释放及代次隔离通过");
}
"""

TICK_STUBS = r"""
#define pdTRUE 1
static int g_notify_lock=1;
static bool lock_ok=true;
static int xSemaphoreTake(int h,int t) {(void)h;(void)t;return lock_ok;}
static void xSemaphoreGive(int h) {(void)h;}
static void indication_pump_nolock(void) {}
struct ble_npl_event {int unused;};
static atomic_bool s_data_notify_pending=true;
typedef struct {uint8_t mask;} ble_output_snapshot_t;
static ble_output_snapshot_t output_snapshot(void)
{return (ble_output_snapshot_t){g_broadcast_mask};}
static void pack_running(const ble_output_snapshot_t *d) {(void)d;}
static void pack_all(const ble_output_snapshot_t *d) {(void)d;}
static uint16_t g_rsc_val_handle=1,g_hr_val_handle=2,g_ftms_treadmill_handle=3;
static uint16_t g_cps_val_handle=4,g_csc_val_handle=5,g_ftms_bike_handle=6;
static uint8_t g_rsc_buf[10],g_hr_buf[2],g_ftms_treadmill_buf[10];
static uint8_t g_cps_buf[8],g_csc_buf[5],g_ftms_bike_buf[8];
static size_t g_ftms_treadmill_len=10,g_cps_len=8,g_ftms_bike_len=8;
static bool s_bridge_csc_valid=true;
static unsigned counts[7];
static void notify_one_nolock(uint16_t h,const uint8_t *b,size_t n)
{(void)b;(void)n;if(h)counts[h]++;}
int ble_bikepower_conn_count(void){return g_conn_handle==BLE_HS_CONN_HANDLE_NONE?0:1;}
static void notify_client_nolock(uint16_t c,uint16_t h,const uint8_t *b,size_t n){(void)c;(void)h;(void)b;(void)n;}
static void ble_telemetry_service_notify(void (*send)(uint16_t,uint16_t,const uint8_t*,size_t)) {(void)send;}
static void ble_telemetry_service_lock_skipped(void) {}
"""

TICK_CHECKS = r"""
int main(void)
{
    (void)chr_read_buf;(void)s_output;
    g_profile=SPORT_PROFILE_RUNNING;g_ready=true;g_conn_handle=1;
    for(uint8_t mask=1;mask<=7;mask++){
        g_broadcast_mask=mask;memset(counts,0,sizeof(counts));
        g_rsc_val_handle=(mask&3)?1:0;
        g_ftms_treadmill_handle=(mask&3)?3:0;
        s_data_notify_pending=true;notify_tick(NULL);s_data_notify_pending=true;notify_tick(NULL);
        assert(counts[1]==((mask&3)?2u:0u));
        assert(counts[3]==((mask&3)?2u:0u));
        assert(counts[2]==((mask&4)?2u:0u));
    }
    memset(counts,0,sizeof(counts));g_stopping=true;notify_tick(NULL);
    g_stopping=false;g_ready=false;notify_tick(NULL);
    g_ready=true;lock_ok=false;notify_tick(NULL);
    for(int i=0;i<7;i++)assert(counts[i]==0);
    assert(s_lock_depth==0);
    puts("跑步通知：7 种掩码与停止、未就绪、锁忙守卫通过");
}
"""


def main():
    source = (ROOT / "main/ble_bikepower.c").read_text()
    mode = sys.argv[1] if len(sys.argv) > 1 else "all"
    if mode in ("all", "feature"):
        buffers = []
        for name in ("g_ftms_cycling_feature_buf", "g_ftms_running_feature_buf",
                     "g_ftms_power_range_buf", "g_ftms_speed_range_buf"):
            match = re.search(r"static (?:const )?uint8_t\s+" + name + r"[^;]+;", source)
            assert match, f"缺少标准特征值 {name}"
            buffers.append(match.group(0))
        callbacks = functions(source, [(n, "static int") for n in
                                      ("ftms_feat_cb", "ftms_power_range_cb", "ftms_speed_range_cb")])
        run(PREAMBLE + "\n".join(buffers) + callbacks + FEATURE_CHECKS)
    if mode in ("all", "control"):
        callbacks = functions(source, [("ftms_release_owner", "static void"), ("ftms_send_response", "static void"),
                                       ("ftms_send_status", "static void"), ("ftms_operation_accepted", "static void"), ("ftms_cp_cb", "static int")])
        run(PREAMBLE + CP_STUBS.replace("static int write_bytes", "static int ftms_cp_cb(uint16_t, uint16_t, struct ble_gatt_access_ctxt *, void *);\nstatic int write_bytes")
            + callbacks + CP_CHECKS)
    if mode in ("all", "advertising"):
        callbacks = functions(source, [("start_advertising", "static int")])
        run(PREAMBLE + ADV_STUBS + callbacks + ADV_CHECKS)
    if mode in ("all", "lifecycle", "transmission"):
        subprocess.run([sys.executable, str(ROOT / 'tests/test_ble_multi_client.py')], check=True)
    if mode in ("all", "notification"):
        callbacks=functions(source,[("host_notify_event","static void")]).replace('host_notify_event(struct ble_npl_event *event)', 'notify_tick(struct ble_npl_event *event)')
        run(PREAMBLE + TICK_STUBS + callbacks + TICK_CHECKS)


if __name__ == "__main__":
    main()
