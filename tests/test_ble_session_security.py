#!/usr/bin/env python3
"""验证会话加密初始化及真实NimBLE配对前置分支。"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from check_wifi_done_contract import extract_function


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--idf-host-src", default="")
    args = parser.parse_args()
    source = (ROOT / "main/ble_bikepower.c").read_text(encoding="utf-8")
    startup = extract_function(source, "ble_bikepower_start")
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    assert 'MYNEWT_VAL_BLE_SM_BONDING=0' in cmake, "缺少协议栈编译期无绑定约束"
    assert startup.index("configure_session_security()") > startup.index("nimble_port_init()")
    assert startup.index("configure_session_security()") < startup.index("nimble_port_freertos_init(")
    assert source.count("BLE_GATT_CHR_F_WRITE_ENC") >= 2, "不得移除FTMS加密要求"
    definitions = []
    for name, signature in (("session_security_store_read", "static int"),
                            ("configure_session_security", "static void")):
        body = extract_function(source, name)
        assert body, f"缺少生产函数{name}"
        definitions.append(signature + " " + body)
    upstream = ""
    if args.idf_host_src:
        folder = Path(args.idf_host_src)
        for filename, name in (("ble_store.c", "ble_store_read"),
                               ("ble_sm.c", "ble_sm_chk_store_overflow_by_type"),
                               ("ble_sm.c", "ble_sm_chk_repeat_pairing")):
            content = (folder / filename).read_text(encoding="utf-8")
            body = extract_function(content, name)
            assert body
            upstream += "\nstatic int " + body
    preamble = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define BLE_HS_ENOENT 5
#define BLE_HS_ENOTSUP 8
#define BLE_HS_EINVAL 3
#define BLE_HS_EALREADY 2
#define BLE_HS_IO_NO_INPUT_OUTPUT 3
#define BLE_STORE_OBJ_TYPE_OUR_SEC 1
#define BLE_STORE_OBJ_TYPE_PEER_SEC 2
#define BLE_STORE_OBJ_TYPE_CCCD 3
#define BLE_STORE_OBJ_TYPE_PEER_DEV_REC 4
#define BLE_STORE_OBJ_TYPE_PEER_ADDR 6
#define BLE_STORE_OBJ_TYPE_LOCAL_IRK 7
#define BLE_STORE_OBJ_TYPE_CSFC 8
#define NIMBLE_BLE_CONNECT 1
#define MYNEWT_VAL_BLE_SM_SC 1
#define MYNEWT_VAL_BLE_SM_BONDING 0
#define MYNEWT_VAL_BLE_STORE_MAX_BONDS 0
#define MYNEWT_VAL(x) MYNEWT_VAL_##x
#define BLE_SM_PROC_F_AUTHENTICATED 1
#define BLE_SM_PROC_F_SC 2
#define BLE_SM_PROC_F_BONDING 4
#define BLE_GAP_REPEAT_PAIRING_RETRY 1
#define BLE_HS_LOG(...) ((void)0)
union ble_store_key { unsigned char data[40]; };
union ble_store_value { unsigned char data[80]; };
struct ble_store_value_sec { int key_size, authenticated, sc; };
struct ble_gap_repeat_pairing { uint16_t conn_handle; int cur_key_size,cur_authenticated,cur_sc,new_key_size,new_authenticated,new_sc,new_bonding; };
typedef int ble_sm_proc_flags;
static struct { int sm_io_cap,sm_sc,sm_bonding,sm_mitm;
 int (*store_read_cb)(int,const union ble_store_key*,union ble_store_value*);
} ble_hs_cfg;
static void ble_hs_lock(void) {}
static void ble_hs_unlock(void) {}
static int ble_store_util_count(int type,int *count) { (void)type; *count=0; return BLE_HS_ENOTSUP; }
static int ble_sm_num_procs(void) { return 0; }
static int ble_store_full_event(int type,uint16_t conn) { (void)type;(void)conn;return BLE_HS_ENOTSUP; }
static int ble_gap_repeat_pairing_event(struct ble_gap_repeat_pairing *event) { (void)event;return 0; }
'''
    checks = r'''
int main(void) {
 union ble_store_key key={0}; union ble_store_value value;
 memset(&value,0xa5,sizeof(value));
 configure_session_security();
 assert(ble_hs_cfg.sm_sc==1 && ble_hs_cfg.sm_bonding==0 && ble_hs_cfg.sm_mitm==0);
 assert(ble_hs_cfg.store_read_cb==session_security_store_read);
 for(int type=1;type<=8;type++) {
  int expected=type==5?BLE_HS_ENOTSUP:BLE_HS_ENOENT;
  assert(session_security_store_read(type,&key,&value)==expected);
 }
 for(unsigned i=0;i<sizeof(value);i++) assert(value.data[i]==0xa5);
 assert(session_security_store_read(1,NULL,&value)==BLE_HS_EINVAL);
 assert(session_security_store_read(1,&key,NULL)==BLE_HS_EINVAL);
 assert(session_security_store_read(99,&key,&value)==BLE_HS_ENOTSUP);
'''
    if upstream:
        # 无绑定编译分支仍编译尾部，测试不能用假容量返回值掩盖缺陷。
        helpers = r'''
static int ble_sm_read_bond(uint16_t conn,struct ble_store_value_sec *value) {
 (void)conn; (void)value; union ble_store_key key={0};union ble_store_value output;
 return ble_store_read(BLE_STORE_OBJ_TYPE_PEER_SEC,&key,&output);
}
'''
        first, remaining = upstream.split("\nstatic int ble_sm_chk_store_overflow_by_type", 1)
        upstream = first + helpers + "\nstatic int ble_sm_chk_store_overflow_by_type" + remaining
        checks += r'''
 assert(ble_sm_chk_store_overflow_by_type(BLE_STORE_OBJ_TYPE_PEER_SEC,1)==0);
 assert(ble_sm_chk_store_overflow_by_type(BLE_STORE_OBJ_TYPE_OUR_SEC,1)==0);
 assert(ble_sm_chk_repeat_pairing(1,0,16)==0);
 ble_hs_cfg.store_read_cb=NULL;
 assert(ble_sm_chk_repeat_pairing(1,0,16)==BLE_HS_ENOTSUP);
 configure_session_security();
 assert(ble_sm_chk_repeat_pairing(1,0,16)==0);
'''
    checks += 'puts("会话加密安全存储与配对前置契约通过，FTMS加密属性保留");return 0;}\n'
    compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
    assert compiler, "需要host C编译器，不允许跳过"
    with tempfile.TemporaryDirectory(prefix="bikepower-session-security-") as tmp:
        path = Path(tmp)
        c = path / "security.c"
        c.write_text(preamble + "\n" + "\n".join(definitions) + upstream + checks, encoding="utf-8")
        flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function"]
        if os.name != "nt": flags += ["-fsanitize=address,undefined"]
        exe = path / "security"
        subprocess.run([compiler, *flags, str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        if upstream:
            legacy_checks = checks.replace(
                'ble_sm_chk_store_overflow_by_type(BLE_STORE_OBJ_TYPE_PEER_SEC,1)==0',
                'ble_sm_chk_store_overflow_by_type(BLE_STORE_OBJ_TYPE_PEER_SEC,1)==BLE_HS_ENOTSUP',
            ).replace(
                'ble_sm_chk_store_overflow_by_type(BLE_STORE_OBJ_TYPE_OUR_SEC,1)==0',
                'ble_sm_chk_store_overflow_by_type(BLE_STORE_OBJ_TYPE_OUR_SEC,1)==BLE_HS_ENOTSUP',
            )
            legacy_preamble = preamble.replace(
                '#define MYNEWT_VAL_BLE_SM_BONDING 0',
                '#define MYNEWT_VAL_BLE_SM_BONDING 1',
            )
            legacy_c = path / "legacy_capacity.c"
            legacy_c.write_text(
                legacy_preamble + "\n" + "\n".join(definitions) + upstream + legacy_checks,
                encoding="utf-8",
            )
            legacy_exe = path / "legacy_capacity"
            subprocess.run([compiler, *flags, str(legacy_c), "-o", str(legacy_exe)], check=True)
            subprocess.run([str(legacy_exe)], check=True)
            print("真实绑定容量分支复现 ENOTSUP；当前无绑定分支通过")

if __name__ == "__main__":
    main()
