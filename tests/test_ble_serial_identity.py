#!/usr/bin/env python3
"""验证生产默认序列号初始化、稳定性、身份覆盖和读取错误路径。"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from check_wifi_done_contract import extract_function


def main():
    source = (ROOT / "main/ble_bikepower.c").read_text(encoding="utf-8")
    definitions = []
    for name, signature in [("prepare_default_serial", "static esp_err_t"),
                            ("dis_read_field", "static int"), ("dis_serial_cb", "static int")]:
        body = extract_function(source, name)
        assert body, f"缺少生产函数：{name}"
        definitions.append(signature + " " + body)
    startup = extract_function(source, "ble_bikepower_start")
    assert startup.index("prepare_default_serial()") < startup.index("nimble_port_init()")
    preamble = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "ble_service_stubs.h"
#include "bikepower_config.h"
#include "device_identity.h"
#define ESP_ERR_INVALID_SIZE 7
unsigned s_lock_depth;
static portMUX_TYPE g_identity_lock;
static device_identity_t g_identity;
static char s_default_serial[DEVICE_IDENTITY_TEXT_MAX];
static unsigned mac_reads;
static int mac_error;
static const uint8_t factory_mac[DEVICE_SERIAL_MAC_LEN] = {0x10,0x20,0xBA,0xCC,0x12,0x34};
static int esp_efuse_mac_get_default(uint8_t *out)
{ ++mac_reads; if(mac_error) return mac_error; memcpy(out,factory_mac,sizeof(factory_mac)); return 0; }
static int chr_read_buf(struct ble_gatt_access_ctxt *ctxt,const uint8_t *buf,size_t len)
{ return os_mbuf_append(ctxt->om,buf,len); }
'''
    checks = r'''
static void check_serial(const char *expected)
{
    struct os_mbuf om={0};
    struct ble_gatt_access_ctxt ctxt={.op=BLE_GATT_ACCESS_OP_READ_CHR,.om=&om};
    assert(dis_serial_cb(0,0,&ctxt,NULL)==0);
    assert(om.length==strlen(expected));
    assert(memcmp(om.data,expected,om.length)==0);
    assert(s_lock_depth==0);
    ctxt.op=BLE_GATT_ACCESS_OP_WRITE_CHR;
    assert(dis_serial_cb(0,0,&ctxt,NULL)==BLE_ATT_ERR_UNLIKELY);
    assert(s_lock_depth==0);
    ctxt.op=BLE_GATT_ACCESS_OP_READ_CHR;
    om.length=sizeof(om.data);
    assert(dis_serial_cb(0,0,&ctxt,NULL)!=0);
    assert(s_lock_depth==0);
}
int main(void)
{
    device_identity_defaults(&g_identity);
    mac_error=9;
    assert(prepare_default_serial()==9);
    assert(s_default_serial[0]=='\0');
    mac_error=0;
    assert(prepare_default_serial()==ESP_OK);
    assert(mac_reads==2);
    check_serial("BP-1020BACC1234");
    for(int i=0;i<1000;++i) assert(prepare_default_serial()==ESP_OK);
    assert(mac_reads==2);
    strcpy(g_identity.fields[DEVICE_IDENTITY_SERIAL],"USER-SERIAL-KEEP");
    check_serial("USER-SERIAL-KEEP");
    g_identity.fields[DEVICE_IDENTITY_SERIAL][0]='\0';
    check_serial("BP-1020BACC1234");
    memset(g_identity.fields[DEVICE_IDENTITY_SERIAL],'X',DEVICE_IDENTITY_TEXT_MAX-1);
    g_identity.fields[DEVICE_IDENTITY_SERIAL][DEVICE_IDENTITY_TEXT_MAX-1]='\0';
    check_serial("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
    device_identity_defaults(&g_identity);
    memset(s_default_serial,0,sizeof(s_default_serial));
    assert(prepare_default_serial()==ESP_OK);
    check_serial("BP-1020BACC1234");
    assert(mac_reads==3);
    puts("默认序列号：非空、初始化失败重试、重复启动/重启稳定、覆盖保留、清空回退、最长字段与读取错误通过");
    return 0;
}
'''
    compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        raise RuntimeError("需要可用的 host C 编译器，禁止跳过测试")
    with tempfile.TemporaryDirectory(prefix="bikepower-serial-identity-") as tmp:
        folder = Path(tmp)
        (folder / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n", encoding="utf-8")
        generated = folder / "serial.c"
        generated.write_text(preamble + "\n" + "\n".join(definitions) + "\n" + checks, encoding="utf-8")
        executable = folder / ("serial.exe" if os.name == "nt" else "serial")
        flags = ["-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror"]
        if os.name != "nt":
            flags += ["-fsanitize=address,undefined"]
        subprocess.run([compiler, *flags, "-I", str(folder), "-I", str(ROOT / "test"),
                        "-I", str(ROOT / "main"), "-I", str(ROOT / "components/bikepower_domain/include"),
                        str(generated), str(ROOT / "components/bikepower_domain/device_identity.c"),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
