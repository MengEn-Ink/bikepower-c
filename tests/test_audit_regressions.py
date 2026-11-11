#!/usr/bin/env python3
"""审查缺陷的固定输入回归：生产代码、无真实硬件写入。"""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
import test_ble_standard_contract as standard

DOMAIN_CHECKS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "cps_input_codec.h"
#include "running_engine.h"
#include "ble_payload_codec.h"
int main(void)
{
    cps_input_t sample = {0};
    const uint8_t rfu[] = {0x20,0xe0,100,0,42,0,0,4,0xaa};
    assert(cps_input_decode(rfu,sizeof(rfu),&sample));
    assert(sample.power_w==100 && sample.crank_revolutions==42);
    cps_input_t saved=sample;
    assert(!cps_input_decode(rfu,7,&sample));
    assert(sample.power_w==saved.power_w && sample.crank_revolutions==saved.crank_revolutions);
    running_engine_t e;
    running_engine_init(&e);
    running_engine_set_pace_float(&e,0);
    running_engine_set_cadence_float(&e,0);
    e.distance_x1000_m=UINT32_MAX-1000u;
    e.elapsed_ms=UINT32_MAX-500u;
    running_snapshot_t s=running_engine_snapshot(&e,1000);
    assert(s.total_distance_m>=4294966u && s.elapsed_time_s>=4294966u);
    assert(s.cadence_spm==170 && s.stride_cm==196);
    e.distance_x1000_m=UINT64_MAX-100;
    e.elapsed_ms=UINT64_MAX-100;
    s=running_engine_snapshot(&e,UINT32_MAX);
    assert(e.distance_x1000_m==UINT64_MAX && e.elapsed_ms==UINT64_MAX);
    assert(s.total_distance_m==UINT32_MAX && s.elapsed_time_s==UINT32_MAX);
    running_engine_set_running(&e,false);
    s=running_engine_snapshot(&e,1000);
    assert(s.total_distance_m==UINT32_MAX && s.elapsed_time_s==UINT32_MAX);
    running_engine_reset_session(&e);
    assert(e.distance_x1000_m==0 && e.elapsed_ms==0);
    uint8_t b[10];
    assert(ble_payload_encode_ftms_bike(b,sizeof(b),false,true,3000,180,250)==6);
    assert(b[0]==0x40 && b[1]==0 && b[2]==0xb8 && b[3]==0x0b && b[4]==250);
    assert(ble_payload_encode_ftms_treadmill(b,sizeof(b),1000,UINT32_MAX,150,65535)==10);
    assert(b[4]==255 && b[5]==255 && b[6]==255);
    puts("审查领域回归：CPS扩展/截断、64位累加饱和、同脚步幅、FTMS完整记录通过");
}
'''

CONTROL_CHECKS = r'''
int main(void)
{
    (void)chr_read_buf;(void)g_broadcast_mask;(void)s_output;(void)expect;
    const uint8_t request[]={0}, reset_op[]={1}, power[]={5,200,0};
    reset();g_ftms_control=false;
    assert(write_bytes(request,1)==0 && response[2]==1);
    g_ftms_pending=false;
    assert(write_bytes(reset_op,1)==0 && response[2]==1 && !g_ftms_control);
    g_ftms_pending=false;
    assert(write_bytes(power,3)==0 && response[2]==5);
    g_ftms_pending=false;
    assert(write_bytes(request,1)==0 && response[2]==1);
    g_ftms_pending=false;
    assert(write_bytes(power,3)==0 && response[2]==1);
    puts("FTMS连续过程：Reset撤销控制权、拒绝旧权限、重新申请后恢复通过");
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix="bikepower-audit-regression-") as temp:
        folder=Path(temp)
        file=folder/"domain.c"
        file.write_text(DOMAIN_CHECKS,encoding="utf-8")
        subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror",
                        "-fsanitize=address,undefined","-I",str(ROOT/"components/bikepower_domain/include"),
                        str(file),*[str(ROOT/"components/bikepower_domain"/name) for name in
                        ["cps_input_codec.c","running_engine.c","domain_rng.c","ble_payload_codec.c"]],
                        "-lm","-o",str(folder/"domain")],check=True)
        subprocess.run([str(folder/"domain")],check=True)
    source=(ROOT/"main/ble_bikepower.c").read_text(encoding="utf-8")
    callbacks=standard.functions(source,[("ftms_release_owner","static void"),("ftms_send_response","static void"),
        ("ftms_send_status","static void"),("ftms_operation_accepted","static void"),("ftms_cp_cb","static int")])
    stubs=standard.CP_STUBS.replace("static int write_bytes",
        "static int ftms_cp_cb(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);\nstatic int write_bytes")
    standard.run(standard.PREAMBLE+stubs+callbacks+CONTROL_CHECKS)
    app=(ROOT/"main/app_main.c").read_text(encoding="utf-8")
    assert "nvs_flash_erase" not in app, "启动异常不得自动擦除分区"

if __name__=="__main__":
    main()
