#pragma once
#include "ble_service_stubs.h"
#define BLE_GATT_CHR_F_NOTIFY 8
#define BLE_ATT_ERR_READ_NOT_PERMITTED 6
#define BLE_HS_CONN_HANDLE_NONE 65535
#define BLE_HS_ENOMEM 7
struct ble_npl_event { int unused; };
struct ble_npl_callout { void (*callback)(struct ble_npl_event *); };
struct ble_gap_conn_desc { uint16_t conn_itvl, conn_latency, supervision_timeout; };
static unsigned s_queries, s_callout_resets, s_deinits;
static int8_t s_test_rssi = -65;
static int s_test_rssi_error;
static inline void *nimble_port_get_dflt_eventq(void) { return NULL; }
static inline int ble_npl_callout_init(struct ble_npl_callout *c, void *q,
        void (*cb)(struct ble_npl_event *), void *arg)
{ (void)q; (void)arg; c->callback=cb; return 0; }
static inline int ble_npl_callout_reset(struct ble_npl_callout *c, uint32_t ticks)
{ (void)c; assert(ticks==1000); ++s_callout_resets; return 0; }
static inline void ble_npl_callout_stop(struct ble_npl_callout *c) { (void)c; }
static inline void ble_npl_callout_deinit(struct ble_npl_callout *c) { (void)c; ++s_deinits; }
static inline uint32_t ble_npl_time_ms_to_ticks32(uint32_t t) { return t; }
static inline int ble_gap_conn_find(uint16_t c, struct ble_gap_conn_desc *d)
{ assert(!s_lock_depth); if(c==65535)return 1; *d=(struct ble_gap_conn_desc){24,0,400};return 0; }
static inline uint16_t ble_att_mtu(uint16_t c) { (void)c; return 23; }
static inline int ble_gap_conn_rssi(uint16_t c, int8_t *r)
{ assert(!s_lock_depth);(void)c;++s_queries;*r=s_test_rssi;return s_test_rssi_error; }
static inline int ble_gatts_count_cfg(const struct ble_gatt_svc_def *s)
{ (void)s;return 0; }
static inline int ble_gatts_add_svcs(const struct ble_gatt_svc_def *s)
{
    assert(!s_lock_depth);
    for(unsigned i=0;i<5;i++)if(s[0].characteristics[i].val_handle)
        *s[0].characteristics[i].val_handle=(uint16_t)(10+i);
    return 0;
}
