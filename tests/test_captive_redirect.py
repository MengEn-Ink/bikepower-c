#!/usr/bin/env python3
"""编译实际 Captive 探测处理函数，检查固定重定向与请求域名隔离。"""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "main/captive_portal.c").read_text()
handler = source[source.index("esp_err_t captive_portal_detect_handler("):]
config = (ROOT / "main/bikepower_config.h").read_text()
host = re.search(r'^#define CAPTIVE_PORTAL_HOST\s+("[^"]+")', config, re.M).group(1)
stub = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
typedef struct { const char *uri; } httpd_req_t;
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
static char location[64], status[32], cache[80], connection[16], body[128];
static int httpd_resp_set_hdr(httpd_req_t *req, const char *key, const char *value) {
    (void)req;
    if (!strcmp(key,"Location")) snprintf(location,sizeof(location),"%s",value);
    if (!strcmp(key,"Cache-Control")) snprintf(cache,sizeof(cache),"%s",value);
    if (!strcmp(key,"Connection")) snprintf(connection,sizeof(connection),"%s",value);
    return 0;
}
static int httpd_resp_set_status(httpd_req_t *req, const char *value) {
    (void)req; snprintf(status,sizeof(status),"%s",value); return 0;
}
static int httpd_resp_set_type(httpd_req_t *req, const char *value) { (void)req; (void)value; return 0; }
static int httpd_resp_sendstr(httpd_req_t *req, const char *value) {
    (void)req; snprintf(body,sizeof(body),"%s",value); return 0;
}
'''
test = r'''
int main(void) {
    const char *paths[] = {"/generate_204", "/gen_204", "/hotspot-detect.html", "/unknown?next=http://evil.example"};
    for (unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);i++) {
        httpd_req_t req = {.uri=paths[i]};
        assert(captive_portal_detect_handler(&req)==ESP_OK);
        assert(!strcmp(location,"http://192.168.4.1/"));
        assert(!strcmp(status,"303 See Other"));
        assert(strstr(cache,"no-store"));
        assert(!strcmp(connection,"close"));
        assert(strlen(body)>0);
    }
    puts("PASS captive actual handler: 4 paths, fixed IP, no request/query reflection, 303/body/cache/close");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="bikepower-captive-test-") as tmp:
    folder = Path(tmp)
    unit = folder / "test.c"
    unit.write_text(stub + "\n#define CAPTIVE_PORTAL_HOST " + host + "\n" + handler + test)
    binary = folder / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", str(unit), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
