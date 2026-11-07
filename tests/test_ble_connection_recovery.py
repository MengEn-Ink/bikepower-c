#!/usr/bin/env python3
"""生产连接确认、真实订阅与通知守卫回归，不连接实物。"""
from pathlib import Path
import test_ble_standard_contract as contract

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'main/ble_bikepower.c').read_text(encoding='utf-8')
stubs=contract.GAP_STUBS
start=stubs.index('static int ble_gap_conn_find(')
end=stubs.index('struct ble_gap_event',start)
stubs=stubs[:start]+r'''
static bool host_exists=true;
static uint8_t host_role=BLE_GAP_ROLE_SLAVE;
static int host_reads,connected_calls,reset_calls,send_calls;
static uint16_t host_connection=1;
static int ble_gap_conn_find(uint16_t h,struct ble_gap_conn_desc *d)
{host_reads++;if(!host_exists||h!=host_connection)return 7;d->conn_handle=h;d->role=host_role;return 0;}
'''+stubs[end:]
stubs=stubs.replace('static void subscription_update(uint16_t h,bool e) {(void)h;(void)e;}','')
stubs=stubs.replace('static void indication_reset(void) {}','static void subscriptions_reset(void);\nstatic void indication_reset(void) {reset_calls++;subscriptions_reset();}')
stubs=stubs.replace('static void ble_telemetry_service_connect(uint16_t connection) {(void)connection;}','static void ble_telemetry_service_connect(uint16_t connection) {assert(connection==1);connected_calls++;}')
stubs+=r'''
#define BLE_HS_ENOMEM 6
static uint16_t g_cps_val_handle=16,g_csc_val_handle=28;
static atomic_uint_least16_t g_notify_subscriptions[BLE_NOTIFY_SUBSCRIPTION_MAX];
static struct os_mbuf *ble_hs_mbuf_from_flat(const uint8_t *b,size_t n)
{static struct os_mbuf v;memcpy(v.data,b,n);v.length=n;return &v;}
static int ble_gatts_notify_custom(uint16_t c,uint16_t h,struct os_mbuf *om)
{assert(c==1 && h==28 && om->length==5 && s_lock_depth==0);send_calls++;return 0;}
static void ble_telemetry_service_tx(int r,bool a) {(void)r;(void)a;}
'''
signatures=[('subscriptions_reset','static void'),('subscription_update','static void'),
 ('notification_subscribed','static bool'),('notify_one_nolock','static void')]
if 'static bool connection_accept(' in source:
    signatures.append(('connection_accept','static bool'))
signatures += [('gap_event_cb','static int'),('on_reset','static void')]
callbacks=contract.functions(source,signatures)
checks=r'''
static void reset(void) {
    g_conn_handle=BLE_HS_CONN_HANDLE_NONE;g_ready=true;g_stopping=false;
    host_exists=true;host_connection=1;host_role=BLE_GAP_ROLE_SLAVE;
    ads=closed=host_reads=connected_calls=reset_calls=send_calls=0;
    g_ftms_pending=g_ftms_control=g_ftms_subscribed=false;
    subscriptions_reset();
}
static void connect(int status,uint16_t c) {
    struct ble_gap_event e={.type=BLE_GAP_EVENT_CONNECT,.connect={.status=status,.conn_handle=c}};
    gap_event_cb(&e,NULL);
}
static void subscribe(uint16_t c,bool enabled) {
    struct ble_gap_event e={.type=BLE_GAP_EVENT_SUBSCRIBE,.subscribe={.conn_handle=c,.attr_handle=28,.cur_notify=enabled}};
    gap_event_cb(&e,NULL);
}
static void disconnect(uint16_t c) {
    struct ble_gap_event e={.type=BLE_GAP_EVENT_DISCONNECT,.disconnect={.conn={.conn_handle=c}}};
    gap_event_cb(&e,NULL);
}
static void send(void) {uint8_t b[5]={2,1,0,1,0};notify_one_nolock(28,b,5);}
int main(void) {
(void)chr_read_buf;(void)g_broadcast_mask;(void)s_data_lock;(void)s_output;(void)g_profile;
(void)g_cps_val_handle;(void)g_csc_val_handle;(void)ble_gap_conn_find;
reset();connect(26,1);assert(g_conn_handle==1 && connected_calls==1 && ads==0);
send();assert(send_calls==0);subscribe(1,true);assert(notification_subscribed(28));send();assert(send_calls==1);
assert(!notification_subscribed(16));
g_ftms_pending=g_ftms_control=g_ftms_subscribed=true;
connect(0,1);connect(26,1);assert(connected_calls==1 && reset_calls==1);
assert(notification_subscribed(28) && g_ftms_pending && g_ftms_control);
subscribe(1,false);send();assert(send_calls==1 && !notification_subscribed(28));
subscribe(1,true);disconnect(2);assert(g_conn_handle==1 && notification_subscribed(28) && closed==0);
connect(0,2);assert(g_conn_handle==1 && notification_subscribed(28));
struct ble_gap_event adv={.type=BLE_GAP_EVENT_ADV_COMPLETE};gap_event_cb(&adv,NULL);assert(ads==0);
disconnect(1);assert(g_conn_handle==BLE_HS_CONN_HANDLE_NONE && !notification_subscribed(28) && closed==1 && ads==1);
host_exists=false;subscribe(1,true);send();assert(g_conn_handle==BLE_HS_CONN_HANDLE_NONE && send_calls==1);
for(int status=0;status<=26;status+=26) {
 reset();host_exists=false;connect(status,1);assert(g_conn_handle==BLE_HS_CONN_HANDLE_NONE && connected_calls==0 && ads==1);
 reset();connect(status,BLE_HS_CONN_HANDLE_NONE);assert(g_conn_handle==BLE_HS_CONN_HANDLE_NONE && connected_calls==0);
 reset();host_role=0;connect(status,1);assert(g_conn_handle==BLE_HS_CONN_HANDLE_NONE && connected_calls==0);
}
reset();connect(62,1);assert(g_conn_handle==BLE_HS_CONN_HANDLE_NONE && connected_calls==0);
reset();host_exists=false;connect(62,1);assert(g_conn_handle==BLE_HS_CONN_HANDLE_NONE && ads==1);
reset();g_stopping=true;connect(26,1);subscribe(1,true);assert(connected_calls==0 && host_reads==0 && ads==0);
reset();g_ready=false;subscribe(1,true);assert(connected_calls==0 && host_reads==0);
reset();subscribe(1,false);assert(connected_calls==0 && host_reads==0);
reset();subscribe(1,true);assert(g_conn_handle==1 && notification_subscribed(28) && connected_calls==1);
connect(26,1);assert(notification_subscribed(28) && reset_calls==1);send();assert(send_calls==1);
subscribe(2,true);assert(g_conn_handle==1 && connected_calls==1);
on_reset(3);assert(g_conn_handle==BLE_HS_CONN_HANDLE_NONE && !notification_subscribed(28));
assert(s_lock_depth==0);
puts("连接恢复：Host验证、角色、状态26、真实失败、早到订阅、重复确认、退订、旧句柄及断开/复位通过");
}
'''
contract.run(contract.PREAMBLE+stubs+callbacks+checks)
