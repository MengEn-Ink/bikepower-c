#!/usr/bin/env python3
"""生产控制点/CCCD/应用mailbox固定序列，替身仅限传输和主循环调度。"""
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
import test_ble_standard_contract as t

def main():
    source=(ROOT/'main/ble_bikepower.c').read_text()
    funcs=t.functions(source,[("ftms_send_response","static void"),("ftms_send_status","static void"),
        ("ftms_operation_accepted","static void"),("rsc_cp_cb","static int"),("ftms_cp_cb","static int"),
        ("ble_bikepower_control_ticket","uint32_t"),("ble_bikepower_control_current","bool"),
        ("ble_bikepower_control_complete","void")])
    stubs=t.CP_STUBS.replace('static int write_bytes','static int ftms_cp_cb(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);\nstatic int write_bytes')
    stubs=stubs.replace('static bool indication_submit(uint32_t token,const uint8_t *b,size_t n)\n{(void)token;return indicate_try(9,b,n);}', '')
    stubs=stubs.replace('static bool client_indicates(uint16_t c,uint16_t h){(void)h;return c==g_conn_handle&&g_ftms_subscribed;}', '')
    extra=r'''
static uint16_t g_rsc_cp_handle=11;
static bool g_rsc_subscribed,g_rsc_pending;
static uint16_t g_rsc_connection=65535;
static bool client_indicates(uint16_t c,uint16_t h){return c==g_conn_handle&&(h==11?g_rsc_subscribed:g_ftms_subscribed);}
static uint32_t g_rsc_response_token;
static ble_rsc_distance_cb_t g_rsc_distance_cb;
static bool indication_submit(uint32_t token,const uint8_t *b,size_t n)
{assert(token==1&&n==3);responses++;memcpy(response,b,n);return send_ok;}
static bool set_distance(uint32_t dm){calls++;value=(int)dm;return accepted;}
static int rsc_cp_cb(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);
static int rsc_write(const uint8_t *bytes,size_t n)
{
 struct os_mbuf om={0};memcpy(om.data,bytes,n);om.length=n;
 struct ble_gatt_access_ctxt c={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=&om};
 return rsc_cp_cb(1,11,&c,NULL);
}
'''
    stubs=stubs.replace('assert(c==1 && h==9)','assert(c==1 && (h==9||h==11))')
    checks=r'''
int main(void)
{
 (void)chr_read_buf;(void)g_broadcast_mask;(void)s_output;(void)expect;
 reset();g_control_async=true;
 const uint8_t power[]={5,200,0},reset_op[]={1},start[]={7};
 assert(write_bytes(power,3)==0&&calls==1&&responses==0&&g_ftms_pending);
 assert(write_bytes(reset_op,1)==0x80&&responses==0);
 ble_bikepower_control_complete(2,false,true);assert(responses==0);
 ble_bikepower_control_complete(1,false,true);assert(responses==1&&response[2]==1);
 g_ftms_pending=false;responses=0;calls=0;
 assert(write_bytes(reset_op,1)==0&&responses==0&&g_ftms_control);
 ble_bikepower_control_complete(1,false,true);
 assert(response[2]==1&&!g_ftms_control&&statuses==1);
 g_ftms_pending=false;responses=0;
 assert(write_bytes(power,3)==0&&response[2]==5);
 reset();g_control_async=true;
 assert(write_bytes(start,1)==0&&responses==1);
 reset();g_control_async=true;accepted=false;
 assert(write_bytes(reset_op,1)==0&&response[2]==4&&statuses==0);
 reset();g_control_async=true;
 assert(write_bytes(power,3)==0&&responses==0);
 g_ftms_subscribed=false;ble_bikepower_control_complete(1,false,true);assert(responses==0);
 g_conn_handle=0xffff;ble_bikepower_control_complete(1,false,true);assert(responses==0);
 reset();g_control_async=true;g_rsc_distance_cb=set_distance;
 const uint8_t distance[]={1,0xff,0xff,0xff,0xff},unknown[]={255};
 assert(rsc_write(distance,5)==0x81);
 g_rsc_subscribed=true;
 assert(rsc_write(distance,5)==0&&calls==1&&responses==0);
 assert(rsc_write(distance,5)==0x80);
 ble_bikepower_control_complete(1,true,true);
 assert(response[0]==0x10&&response[1]==1&&response[2]==1);
 g_rsc_pending=false;responses=0;
 assert(rsc_write(distance,4)==0&&response[2]==3);
 g_rsc_pending=false;responses=0;
 assert(rsc_write(unknown,1)==0&&response[2]==2);
 g_rsc_pending=false;responses=0;accepted=false;
 assert(rsc_write(distance,5)==0&&response[2]==4);
 puts("控制点新增路径：实际应用后确认、忙拒绝、失效票据、RSC全uint32与错误序列通过");
}
'''
    t.run(t.PREAMBLE+stubs+extra+funcs+checks)
    funcs=t.functions(source,[("client_find","static ble_client_t *"),("subscriptions_reset","static void"),("subscription_update","static void"),
        ("notification_subscribed","static bool"),("notify_one_nolock","static void")])
    stubs=r'''
typedef struct {uint16_t connection;atomic_uint_least16_t subscriptions[BLE_NOTIFY_SUBSCRIPTION_MAX];} ble_client_t;
static ble_client_t s_clients[BLE_DOWNSTREAM_MAX]={{.connection=1},{.connection=BLE_HS_CONN_HANDLE_NONE}};
static unsigned sends,allocations;
static struct os_mbuf *ble_hs_mbuf_from_flat(const uint8_t *b,size_t n)
{static struct os_mbuf om;memcpy(om.data,b,n);allocations++;return &om;}
static int ble_gatts_notify_custom(uint16_t c,uint16_t h,struct os_mbuf *om)
{assert(c==1&&h&&om);sends++;return 0;}
static void ble_telemetry_service_tx(int rc,bool oom){assert(!rc&&!oom);}
'''
    checks=r'''
int main(void){
 (void)chr_read_buf;(void)g_broadcast_mask;(void)s_output;(void)s_data_lock;(void)g_conn_handle;
 g_ready=true;uint8_t b[]={1,2};
 notify_one_nolock(5,b,2);assert(!sends&&!allocations);
 subscription_update(1,5,true);notify_one_nolock(5,b,2);assert(sends==1);
 notify_one_nolock(6,b,2);assert(sends==1);
 subscription_update(1,5,false);notify_one_nolock(5,b,2);assert(sends==1);
 subscription_update(1,5,true);subscriptions_reset();notify_one_nolock(5,b,2);assert(sends==1);
 subscription_update(1,5,true);g_stopping=true;notify_one_nolock(5,b,2);assert(sends==1);
 puts("CCCD新增路径：未订阅/其他特征/退订/重连清理均不分配不发送通过");
}
'''
    t.run(t.PREAMBLE+stubs+funcs+checks)

    app=(ROOT/'main/app_main.c').read_text()
    state=app[app.index('typedef struct {\n    bool pending, rsc;'):app.index('static bool queue_control')]
    funcs=t.functions(app,[("queue_control","static bool"),("on_erg_target","static bool"),
        ("on_erg_clear","static bool"),("on_run_control","static bool"),("on_rsc_distance","static bool"),
        ("process_control_mailbox","static void")])
    preamble=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "bikepower_config.h"
#include "power_engine.h"
#include "ble_service_stubs.h"
#include "ble_bikepower.h"
#include "app_events.h"
#define portDISABLE_INTERRUPTS() ((void)0)
#define portENABLE_INTERRUPTS() ((void)0)
static bool s_real_power_source,current=true;
static uint32_t ticket=1;
static power_engine_t s_engine;
static unsigned applied,completed;
static app_evt_t last;
uint32_t ble_bikepower_control_ticket(bool rsc){(void)rsc;return ticket;}
bool ble_bikepower_control_current(uint32_t t,bool rsc){(void)rsc;return current&&t==ticket;}
void ble_bikepower_control_complete(uint32_t t,bool rsc,bool ok){(void)rsc;assert(t==ticket&&ok);completed++;}
static void app_state_handle_event(const app_evt_t *e){applied++;last=*e;}
bool power_engine_set_run_speed(power_engine_t *e,int v){e->running.speed_x100_kmh=v;return true;}
'''
    checks=r'''
int main(void){
 assert(on_erg_target(200));assert(!on_run_control(BLE_RUN_CTRL_SPEED,1000));assert(!on_erg_clear(1));
 process_control_mailbox();assert(applied==1&&completed==1&&last.data==200);
 assert(on_erg_clear(1));process_control_mailbox();assert(applied==2&&completed==2);
 assert(on_rsc_distance(UINT32_MAX));process_control_mailbox();
 assert(s_engine.running.distance_x1000_m==(uint64_t)UINT32_MAX*100u);
 assert(s_engine.running.snapshot.total_distance_dm==UINT32_MAX&&completed==3);
 assert(on_rsc_distance(101));process_control_mailbox();assert(s_engine.running.distance_x1000_m==10100u);
 assert(on_erg_target(250));ticket=2;process_control_mailbox();assert(applied==2&&completed==4);
 assert(on_run_control(BLE_RUN_CTRL_RESET,0));process_control_mailbox();assert(last.type==APP_EVT_RUN_RESET);
 assert(!on_run_control(BLE_RUN_CTRL_SPEED,299));assert(!on_erg_target(2001));
 puts("生产mailbox：满槽拒绝、按序应用后确认、旧票据丢弃、RSC精度与uint64转换通过");
}
'''
    t.run(preamble+state+funcs+checks)

if __name__=='__main__':main()
