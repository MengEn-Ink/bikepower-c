#!/usr/bin/env python3
"""执行生产骑行 GATT 表和标准 Feature 回调，验证客户端发现及读取契约。"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from check_wifi_done_contract import extract_function


def main():
    source = (ROOT / "main/ble_bikepower.c").read_text()
    names = ["cps_feature_cb", "csc_feature_cb", "sensor_location_cb"]
    callbacks = []
    for name in names:
        body = extract_function(source, name)
        assert body, f"缺少标准特征读取回调：{name}"
        callbacks.append("static int " + body)
    buffers = []
    for name in ["g_cps_feature_buf", "g_csc_feature_buf", "g_sensor_location"]:
        match = re.search(r"static (?:const )?uint8_t\s+" + name + r"[^;]+;", source)
        assert match, f"缺少标准特征固定值：{name}"
        buffers.append(match.group(0))
    table = source[source.index("static const struct ble_gatt_svc_def cycling_gatt_svcs[]"):
                   source.index("// 按当前 g_broadcast_mask")]
    stub_names = set(re.findall(r"\.access_cb\s*=\s*(\w+)", table)) - set(names)
    stubs = "\n".join(
        f"static int {name}(uint16_t c, uint16_t a, struct ble_gatt_access_ctxt *x, void *v)"
        "{ (void)c; (void)a; (void)x; (void)v; return -1; }"
        for name in sorted(stub_names)
    )
    preamble = r"""
#include <assert.h>
#include <stdio.h>
#include "ble_service_stubs.h"
#include "bikepower_config.h"
#define BLE_GATT_CHR_F_WRITE_ENC 32
#define BLE_GATT_CHR_F_NOTIFY 8
#define BLE_GATT_CHR_F_WRITE_NO_RSP 16
typedef struct { ble_uuid_t u; uint16_t value; } test_uuid_t;
#define BLE_UUID16_DECLARE(n) ((const ble_uuid_t *)&(const test_uuid_t){.value=(n)})
static uint16_t g_cps_val_handle, g_hr_val_handle, g_csc_val_handle;
static uint16_t g_ftms_bike_handle, g_ftms_cp_handle, g_ftms_status_handle;
static uint16_t g_rsc_val_handle, g_ftms_treadmill_handle, g_rsc_cp_handle;
static const struct ble_gatt_svc_def *table_under_test;
static int g_profile;
static uint8_t g_broadcast_mask;
static bool g_bridge_mode;
static struct ble_gatt_svc_def g_active_svcs[7];
static const struct ble_gatt_svc_def *ble_config_service_definition(void)
{ static const struct ble_gatt_svc_def svc={0}; return &svc; }
static int chr_read_buf(struct ble_gatt_access_ctxt *ctxt, const uint8_t *buf, size_t len)
{ return os_mbuf_append(ctxt->om, buf, len); }
static uint16_t uuid16(const ble_uuid_t *uuid) { return ((const test_uuid_t *)uuid)->value; }
"""
    checks = r"""
static const struct ble_gatt_chr_def *find(uint16_t svc, uint16_t chr)
{
    for (const struct ble_gatt_svc_def *s = table_under_test; s->uuid; ++s)
        if (uuid16(s->uuid) == svc)
            for (const struct ble_gatt_chr_def *c = s->characteristics; c->uuid; ++c)
                if (uuid16(c->uuid) == chr) return c;
    return NULL;
}
static void check_read(uint16_t svc, uint16_t chr, const uint8_t *expected, size_t len)
{
    const struct ble_gatt_chr_def *c = find(svc, chr);
    assert(c && c->flags == BLE_GATT_CHR_F_READ);
    struct os_mbuf om = {0};
    struct ble_gatt_access_ctxt ctxt = { .op = BLE_GATT_ACCESS_OP_READ_CHR, .om = &om };
    assert(c->access_cb(0, 0, &ctxt, NULL) == 0);
    assert(om.length == len && memcmp(om.data, expected, len) == 0);
    ctxt.op = BLE_GATT_ACCESS_OP_WRITE_CHR;
    assert(c->access_cb(0, 0, &ctxt, NULL) == BLE_ATT_ERR_UNLIKELY);
}
int main(void)
{
    table_under_test = cycling_gatt_svcs;
    const uint8_t cps[] = {8, 0, 16, 0}, csc[] = {2, 0}, location[] = {5};
    check_read(0x1818, 0x2a65, cps, sizeof(cps));
    check_read(0x1818, 0x2a5d, location, sizeof(location));
    check_read(0x1816, 0x2a5c, csc, sizeof(csc));
    assert(find(0x1818, 0x2a63)->flags == BLE_GATT_CHR_F_NOTIFY);
    assert(find(0x1816, 0x2a5b)->flags == BLE_GATT_CHR_F_NOTIFY);
    assert(find(0x180d, 0x2a37)->flags == BLE_GATT_CHR_F_NOTIFY);
    assert(find(0x1826, 0x2ad2)->flags == BLE_GATT_CHR_F_NOTIFY);
    assert(find(0x1826, 0x2ada)->flags == BLE_GATT_CHR_F_NOTIFY);
    assert(find(0x1826, 0x2ad9)->flags == (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_WRITE_ENC));
    assert(find(0x1826, 0x2ad8) && find(0x1826, 0x2ad8)->flags == BLE_GATT_CHR_F_READ);
    table_under_test = running_gatt_svcs;
    assert(find(0x1814, 0x2a53)->flags == BLE_GATT_CHR_F_NOTIFY);
    assert(find(0x1814, 0x2a55)->flags == (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE));
    assert(find(0x180d, 0x2a37)->flags == BLE_GATT_CHR_F_NOTIFY);
    assert(find(0x1826, 0x2acd)->flags == BLE_GATT_CHR_F_NOTIFY);
    assert(find(0x1826, 0x2ada)->flags == BLE_GATT_CHR_F_NOTIFY);
    assert(find(0x1826, 0x2ad9)->flags == (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE | BLE_GATT_CHR_F_WRITE_ENC));
    assert(find(0x1826, 0x2ad4) && find(0x1826, 0x2ad4)->flags == BLE_GATT_CHR_F_READ);
    g_profile=SPORT_PROFILE_CYCLING;g_bridge_mode=false;g_broadcast_mask=BROADCAST_MASK_DEFAULT;
    assert(g_broadcast_mask == (BROADCAST_FIELD_ALL | BROADCAST_FIELD_CADENCE_WITH_POWER));
    build_active_gatt_svcs();table_under_test=g_active_svcs;
    assert(find(POWER_SERVICE_UUID,POWER_CHAR_UUID));
    assert(!find(CSC_SERVICE_UUID,CSC_CHAR_UUID));
    assert(find(HR_SERVICE_UUID,HR_CHAR_UUID));
    assert(find(FTMS_SERVICE_UUID,FTMS_POWER_CHAR_UUID));
    for(int profile=0;profile<2;++profile)for(int real=0;real<2;++real)for(int mask=1;mask<16;++mask){
        if(!(mask&7)) continue;
        g_profile=profile;g_bridge_mode=real;g_broadcast_mask=mask;build_active_gatt_svcs();
        table_under_test=g_active_svcs;
        assert((find(0x1826,profile?0x2acd:0x2ad2)!=NULL)==((mask&3)&& (profile||!real)));
        if(!profile){
            assert((find(0x1818,0x2a63)!=NULL)==!!(mask&1));
            /* 兼容位只在功率同时开启时移除独立 CSC，踏频仍通过 CPS 曲柄字段提供。 */
            assert((find(0x1816,0x2a5b)!=NULL)==((mask&2) && !((mask&1)&&(mask&8))));
        }
    }
    puts("骑行标准服务：Feature / Sensor Location 字节、只读与 Notify 属性通过");
    return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix="bikepower-cycling-services-") as temp:
        folder = Path(temp)
        generated = folder / "gatt.c"
        generated.write_text(preamble + "\n" + "\n".join(buffers + callbacks)
                             + "\n" + stubs + "\n" + table + "\nstatic void " + extract_function(source, "build_active_gatt_svcs") + "\n" + checks)
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-I", str(ROOT / "test"),
            "-I", str(ROOT / "main"), "-I", str(ROOT / "components/bikepower_domain/include"),
            str(generated), "-o", str(folder / "gatt"),
        ], check=True)
        subprocess.run([str(folder / "gatt")], check=True)


if __name__ == "__main__":
    main()
