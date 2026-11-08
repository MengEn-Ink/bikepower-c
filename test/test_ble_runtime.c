#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ble_runtime_under_test.c"

static int64_t s_now = 1000000;
int64_t esp_timer_get_time(void) { return s_now; }

int main(void)
{
    assert(output_snapshot().mask == BROADCAST_MASK_DEFAULT);
    ble_bikepower_set_broadcast_mask(BROADCAST_FIELD_ALL);
    ble_bikepower_set_data(222, 111, 88);
    ble_output_snapshot_t first = output_snapshot();
    ble_bikepower_set_data(444, 155, 99);
    pack_all(&first);
    assert(g_cps_buf[2] == 222 && g_cps_buf[3] == 0);
    assert(g_hr_buf[1] == 111);
    assert(g_ftms_bike_buf[4] == 176);
    bridge_snapshot_t bridge = { .valid = true, .power_w = 300, .raw_power_w = 300,
        .has_crank = true, .crank_revolutions = 34, .crank_event_time_1024 = 1000,
        .cadence_valid = true, .cadence_half_rpm = 180 };
    ble_bikepower_set_bridge_data(&bridge, 123);
    first = output_snapshot();
    ble_bikepower_set_data(555, 166, 77);
    pack_all(&first);
    assert(g_cps_buf[2] == 44 && g_cps_buf[3] == 1);
    assert(g_hr_buf[1] == 123);
    assert(g_csc_buf[1] == 34);
    assert(g_ftms_bike_len == 0);
    running_snapshot_t run = { .rsc_speed_1_256_mps = 512, .cadence_spm = 180,
        .stride_cm = 120, .total_distance_m = 100, .total_distance_dm = 1000, .elapsed_time_s = 30, .running = true };
    ble_bikepower_set_running_data(run, 140);
    first = output_snapshot();
    run.running = false;
    run.total_distance_m = 0;
    ble_bikepower_set_running_data(run, 100);
    pack_running(&first);
    assert(g_rsc_buf[0] == 7);
    assert(g_rsc_buf[1] == 0 && g_rsc_buf[2] == 2);
    assert(g_rsc_buf[6] == 232 && g_rsc_buf[7] == 3);
    assert(g_hr_buf[1] == 140);
    for(int cadence=120;cadence<=220;++cadence){
        first.running.cadence_spm=cadence;first.running.total_distance_dm=UINT32_MAX;
        pack_running(&first);assert(g_rsc_buf[3]==(cadence+1)/2);
        assert(g_rsc_buf[6]==255 && g_rsc_buf[9]==255);
    }
    ble_bikepower_set_broadcast_mask(0);
    assert(output_snapshot().mask == BROADCAST_MASK_DEFAULT);
    puts("BLE 通知快照：模拟/桥接/跑步均保持同轮字段与掩码一致");
    return 0;
}
