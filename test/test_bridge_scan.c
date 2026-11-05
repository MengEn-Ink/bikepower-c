#include <assert.h>
#include "bridge_stubs.h"
#include "../main/ble_scanner.c"

static gap_fn s_callback;
static void *s_arg;
int ble_hs_id_infer_auto(int privacy, uint8_t *type) { (void)privacy; *type = 0; return 0; }
int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *fields, const uint8_t *data, uint8_t length)
{
    static ble_uuid16_t uuid = BLE_UUID16_INIT(0x1818);
    uuid.value = data[0] ? 0x1818 : 0x180d;
    fields->num_uuids16 = 1;
    fields->uuids16 = &uuid;
    fields->name = (uint8_t *)data + 1;
    fields->name_len = length - 1;
    return 0;
}
int ble_gap_disc(uint8_t type, int32_t duration, const struct ble_gap_disc_params *params, gap_fn callback, void *arg)
{
    (void)type; (void)params;
    assert(duration > 0 && duration <= 30000);
    s_callback = callback; s_arg = arg; return 0;
}
int ble_gap_disc_cancel(void)
{
    struct ble_gap_event event = { .type = BLE_GAP_EVENT_DISC_COMPLETE };
    s_callback(&event, s_arg); return 0;
}
static void advertise(uint8_t address, bool cps, const char *name)
{
    uint8_t bytes[32] = { cps };
    size_t length = strlen(name);
    memcpy(bytes + 1, name, length);
    struct ble_gap_event event = { .type = BLE_GAP_EVENT_DISC,
        .disc = { .addr = { .type = 0, .val = { address } }, .rssi = -40,
            .data = bytes, .length_data = length + 1 } };
    s_callback(&event, s_arg);
}
int main(void)
{
    assert(ble_scanner_start(8000) == ESP_OK);
    advertise(1, true, "");
    advertise(2, false, "Heart");
    advertise(3, true, "Same");
    advertise(4, true, "Same");
    advertise(1, false, "Meter");
    char text[3072];
    assert(ble_scanner_serialize_results(text, sizeof(text)));
    assert(!strstr(text, "Heart") && strstr(text, "Meter"));
    int count = 0;
    bridge_device_t device;
    uint32_t candidate = 0;
    for (unsigned i = 0; i < 12; i++) {
        if (ble_scanner_candidate((s_generation << 4) | i, &device)) {
            count++;
            candidate = (s_generation << 4) | i;
        }
    }
    assert(count == 3);
    assert(ble_scanner_serialize_results(text, 10) == 0);
    assert(ble_scanner_stop() == ESP_OK);
    assert(ble_scanner_candidate(candidate, &device));
    assert(device.bound && device.addr[0] != 0);
    assert(ble_scanner_start(8000) == ESP_OK);
    assert(!ble_scanner_candidate(candidate, &device));
    advertise(8, true, "");
    assert(ble_scanner_serialize_results(text, sizeof(text)) && strstr(text, "\"name\":\"\""));
    ble_scanner_stop();
    puts("功率计扫描：CPS筛选/无名/同名隔离/扫描回应合并/过期候选通过");
    return 0;
}
