#!/usr/bin/env python3
"""生产客户端槽/通知/响应队列多连接回归，传输使用host替身。"""
from pathlib import Path
import sys
import test_ble_standard_contract as t
ROOT = Path(__file__).resolve().parents[1]
source = (ROOT/'main/ble_bikepower.c').read_text(encoding='utf-8')
state = source.split('// BEGIN_INDICATION_STATE')[1].split('// END_INDICATION_STATE')[0]
pre = t.PREAMBLE.replace('static uint16_t g_conn_handle;', '') + r'''
#include "ble_config_codec.h"
#define BLE_HS_EDONE 14
#define BLE_HS_ENOMEM 6
#define BLE_HS_EBUSY 15
#define BLE_ERR_REM_USER_CONN_TERM 19
#define BLE_GAP_ROLE_SLAVE 1
#define pdTRUE 1
static uint16_t g_ftms_cp_handle=9,g_rsc_cp_handle=11;
static atomic_bool g_ftms_pending,g_ftms_control,g_rsc_pending;
static uint32_t g_rsc_response_token;
static uint16_t g_ftms_owner=65535,g_ftms_connection=65535,g_rsc_connection=65535;
static bool s_rebuild_pending;
static unsigned sends[4],notes[4],terminated[4];
static int send_rc;
static bool synchronous_ack;
static uint8_t payloads[4][20];
static struct os_mbuf *ble_hs_mbuf_from_flat(const uint8_t *b,size_t n){static struct os_mbuf om;memcpy(om.data,b,n);om.length=n;return &om;}
static void indication_complete(uint16_t,uint16_t,int);
static void indication_pump_nolock(void);
static void notify_schedule(void){indication_pump_nolock();}
struct ble_hs_conn {struct {uint16_t indicate_val_handle;} bhc_gatt_svr;};
static struct ble_hs_conn host_connections[4];
static void ble_hs_lock(void){}
static void ble_hs_unlock(void){}
static struct ble_hs_conn *ble_hs_conn_find(uint16_t c){return c<4?&host_connections[c]:NULL;}
static int ble_gatts_indicate_custom(uint16_t c,uint16_t h,struct os_mbuf *om){assert(c<4&&!s_lock_depth);sends[c]++;memcpy(payloads[c],om->data,om->length);if(send_rc)indication_complete(c,h,send_rc);else if(synchronous_ack)indication_complete(c,h,14);return send_rc;}
static int ble_gatts_notify_custom(uint16_t c,uint16_t h,struct os_mbuf *om){assert(c<4&&h==28&&om->length==5);notes[c]++;return 0;}
static int ble_gap_terminate(uint16_t c,int r){assert(c<4&&r==19);terminated[c]++;return 0;}
static void ble_telemetry_service_tx(int r,bool o){assert(!r&&!o);}
static void ble_telemetry_service_connect(uint16_t c){(void)c;}
static void ble_telemetry_service_subscribe(uint16_t c,uint16_t h,bool n){(void)c;(void)h;(void)n;}
static void ble_telemetry_service_disconnect(uint16_t c){(void)c;}
static void ble_config_service_connect(uint16_t c){(void)c;}
static void ble_config_service_disconnect(uint16_t c){(void)c;}
#define BLE_GAP_EVENT_CONNECT 1
#define BLE_GAP_EVENT_DISCONNECT 2
#define BLE_GAP_EVENT_ADV_COMPLETE 3
#define BLE_GAP_EVENT_SUBSCRIBE 4
#define BLE_GAP_EVENT_NOTIFY_TX 5
#define BLE_GAP_EVENT_ENC_CHANGE 6
#define BLE_ERR_UNSUPP_REM_FEATURE 26
struct ble_gap_conn_desc {uint8_t role;};
struct ble_gap_event {
 int type;
 struct {int status;uint16_t conn_handle;} connect;
 struct {struct {uint16_t conn_handle;} conn;} disconnect;
 struct {uint16_t conn_handle,attr_handle;bool cur_notify,cur_indicate;} subscribe;
 struct {uint16_t conn_handle,attr_handle;bool indication;int status;} notify_tx;
};
static bool host_exists=true;static uint8_t host_role=BLE_GAP_ROLE_SLAVE;
static unsigned ads;
static int start_advertising(void){ads++;return 0;}
static void ble_power_bridge_host_reset(void){}
static int ble_gap_conn_find(uint16_t c,struct ble_gap_conn_desc *d){if(!host_exists||c<1||c>3)return 1;d->role=host_role;return 0;}
int ble_bikepower_conn_count(void);
static bool indication_submit(uint32_t,const uint8_t*,size_t);
static void indication_reset(void);
'''
funcs=t.functions(source,[('client_find','static ble_client_t *'),('client_indicates','static bool'),
 ('subscriptions_reset','static void'),('subscription_update','static void'),('notification_subscribed','static bool'),
 ('indication_reset','static void'),('indication_reserve','static uint32_t'),('indication_subscription_revoked','static void'),
 ('indication_complete','static void'),('indication_pump_nolock','static void'),('indication_submit','static bool'),
 ('notify_one_nolock','static void'),('connection_accept','static bool'),('connection_remove','static void'),('ble_bikepower_conn_count','int'),('gap_event_cb','static int'),('on_reset','static void')])
checks=r'''
int main(void){
 (void)chr_read_buf;(void)g_profile;(void)g_broadcast_mask;(void)s_output;(void)s_data_lock;(void)client_indicates;
 g_ready=true;
 assert(connection_accept(1));assert(connection_accept(2));assert(ble_bikepower_conn_count()==2);
 assert(!connection_accept(3));
 subscription_update(1,28,true);subscription_update(2,28,true);
 uint8_t b[5]={2,1,0,1,0};notify_one_nolock(28,b,5);assert(notes[1]==1&&notes[2]==1);
 subscription_update(1,28,false);notify_one_nolock(28,b,5);assert(notes[1]==1&&notes[2]==2);
 subscription_update(1,28,true);
 uint32_t a=indication_reserve(1,9),second=indication_reserve(2,9);
 assert(a&&second&&a!=second);
 assert(indication_submit(a,b,3));assert(indication_submit(second,b,3));assert(sends[1]==1&&sends[2]==1);
 indication_complete(2,9,14);assert(client_find(1)->active&&!client_find(2)->active);
 uint32_t next=indication_reserve(2,9);assert(indication_submit(next,b,3));assert(sends[2]==2);
 indication_complete(2,9,14);
 uint32_t revoked=indication_reserve(2,11);assert(revoked);indication_subscription_revoked(2,11);assert(!indication_submit(revoked,b,3));
 indication_complete(1,9,99);indication_pump_nolock();assert(terminated[1]&&!terminated[2]);
 connection_remove(1);assert(ble_bikepower_conn_count()==1&&notification_subscribed(2,28));
 assert(!indication_submit(a,b,3));assert(connection_accept(1));assert(!notification_subscribed(1,28));
 assert(!indication_submit(a,b,3));
 for(int i=0;i<BLE_INDICATION_QUEUE_SIZE;i++)assert(indication_reserve(1,9));
 assert(!indication_reserve(1,9));assert(indication_reserve(2,9));
 connection_remove(1);connection_remove(2);assert(ble_bikepower_conn_count()==0);
 assert(connection_accept(1)&&connection_accept(2));
 host_connections[1].bhc_gatt_svr.indicate_val_handle=42;
 a=indication_reserve(1,9);second=indication_reserve(2,9);
 unsigned before_a=sends[1],before_b=sends[2];
 assert(indication_submit(a,b,3)&&indication_submit(second,b,3));
 assert(sends[1]==before_a&&sends[2]==before_b+1&&!client_find(1)->active);
 indication_complete(1,42,14);assert(client_find(1)->count==1);
 host_connections[1].bhc_gatt_svr.indicate_val_handle=0;indication_pump_nolock();
 assert(sends[1]==before_a+1&&client_find(1)->active);
 connection_remove(1);connection_remove(2);
 s_rebuild_pending=true;assert(!connection_accept(1));s_rebuild_pending=false;assert(connection_accept(1));
 indication_reset();synchronous_ack=true;a=indication_reserve(1,9);assert(indication_submit(a,b,3));assert(!client_find(1)->active&&client_find(1)->count==0);
 synchronous_ack=false;send_rc=BLE_HS_EBUSY;a=indication_reserve(1,9);assert(indication_submit(a,b,3));assert(!client_find(1)->active&&!client_find(1)->failed);send_rc=0;indication_pump_nolock();assert(client_find(1)->active);
 connection_remove(1);host_exists=false;assert(!connection_accept(1));host_exists=true;host_role=0;assert(!connection_accept(1));host_role=1;
 struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={.status=26,.conn_handle=1}};
 gap_event_cb(&e,NULL);assert(client_find(1));
 e.type=BLE_GAP_EVENT_SUBSCRIBE;e.subscribe.conn_handle=1;e.subscribe.attr_handle=28;e.subscribe.cur_notify=true;gap_event_cb(&e,NULL);
 e.type=BLE_GAP_EVENT_CONNECT;gap_event_cb(&e,NULL);assert(notification_subscribed(1,28));
 e.type=BLE_GAP_EVENT_DISCONNECT;e.disconnect.conn.conn_handle=2;gap_event_cb(&e,NULL);assert(notification_subscribed(1,28));
 on_reset(3);assert(!ble_bikepower_conn_count()&&!notification_subscribed(1,28));
 assert(s_lock_depth==0);
 puts("多客户端生产回归：容量、独立订阅/响应、队列公平、失败隔离、迟到token、句柄重用、重建冻结及GAP状态26/角色/复位通过");
}
'''
t.run(pre+state+funcs+checks)

scheduler_pre = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#define pdTRUE 1
static int g_notify_lock=1;
static atomic_bool g_ready=true,g_stopping=false,s_data_notify_pending;
static bool lock_ok=true;
static unsigned enqueued,given;
struct ble_npl_event {int unused;};
static struct ble_npl_event s_notify_event;
static int xSemaphoreTake(int l,int t){(void)l;(void)t;return lock_ok;}
static void xSemaphoreGive(int l){(void)l;given++;}
static void *nimble_port_get_dflt_eventq(void){return (void*)1;}
static void ble_npl_eventq_put(void *q,struct ble_npl_event *e){assert(q==(void*)1&&e==&s_notify_event);enqueued++;}
'''
scheduler = t.functions(source,[('notify_schedule','static void'),('notify_tick','static void')])
t.run(scheduler_pre+scheduler+r'''
int main(void){
 notify_schedule();assert(enqueued==1&&given==1&&!s_data_notify_pending);
 notify_tick(0);assert(enqueued==2&&s_data_notify_pending);
 g_stopping=true;notify_schedule();assert(enqueued==2);
 g_stopping=false;g_ready=false;notify_schedule();assert(enqueued==2);
 g_ready=true;lock_ok=false;notify_schedule();assert(enqueued==2&&given==4);
}
''')
