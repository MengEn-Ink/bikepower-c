#!/usr/bin/env python3
"""生产停止函数异常回滚、Host停用失败锁存及正常释放回归。"""
from pathlib import Path
import test_ble_standard_contract as t
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'main/ble_bikepower.c').read_text(encoding='utf-8')
pre=r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define BLE_DOWNSTREAM_MAX 2
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_ERR_REM_USER_CONN_TERM 19
#define BLE_STOP_LOCK_WAIT_MS 100
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define TAG "BLE"
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static bool g_stopping,g_ready=true,g_start_failed,s_rebuild_pending;
static void *g_notify_timer=(void*)1;
static int g_notify_lock=1;
static unsigned stopped,deleted,bridge_stopped,deinits,given;
static bool lock_ok;
static int stop_rc;
static struct {uint16_t connection;} s_clients[2]={{65535},{65535}};
static void ble_power_bridge_radio_stop(void){assert(g_stopping);bridge_stopped++;}
static int esp_timer_stop(void *p){assert(p);stopped++;return 0;}
static int esp_timer_delete(void *p){assert(p);deleted++;return 0;}
static int xSemaphoreTake(int l,int wait){(void)l;(void)wait;return lock_ok;}
static void xSemaphoreGive(int l){(void)l;given++;}
static void ble_telemetry_service_stop(void){}
static int ble_gap_terminate(uint16_t c,int r){(void)c;(void)r;return 0;}
static int ble_gap_adv_stop(void){return 0;}
static int nimble_port_stop(void){return stop_rc;}
static void ble_telemetry_service_deinit(void){}
static void nimble_port_deinit(void){deinits++;}
static void connection_remove(uint16_t c){(void)c;}
static void indication_reset(void){}
'''
fn=t.functions(source,[('ble_bikepower_stop','bool')])
t.run(pre+fn+r'''
int main(void){
 assert(!ble_bikepower_stop());
 assert(!g_stopping&&g_ready&&g_notify_timer==(void*)1&&!stopped&&!deleted&&!bridge_stopped&&!given);
 lock_ok=true;stop_rc=7;
 assert(!ble_bikepower_stop());
 assert(g_stopping&&!g_ready&&g_start_failed&&!g_notify_timer&&stopped==1&&deleted==1&&given==1&&!deinits);
 stop_rc=0;assert(ble_bikepower_stop());
 assert(!g_ready&&!g_start_failed&&deinits==1&&given==2);
 g_stopping=false;g_ready=true;g_notify_timer=(void*)1;
 assert(ble_bikepower_stop());assert(deinits==2&&!g_ready&&!g_start_failed&&deleted==2);
 puts("PASS stop：锁超时保持原运行态、Host停用失败锁存、重试清理和正常释放");
}
''')
app=(ROOT/'main/app_main.c').read_text(encoding='utf-8')
assert 'if (!ble_bikepower_stop()) {' in app
assert 'BLE 清理未完成，禁止重新初始化 Host' in app
