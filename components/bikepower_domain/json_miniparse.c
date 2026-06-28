/**
 * JSON 最小解析（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 从 main/ota_downloader.c 抽取的 static 函数，便于 host 单元测试。
 * 仅支持 latest.json 这类工具生成的无转义 JSON。
 */
#include "json_miniparse.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

bool json_get_str(const char *json, const char *key, char *out, size_t out_sz)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    const char *end = strchr(p, '"');
    if (!end) return false;
    size_t len = (size_t)(end - p);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

int json_get_int(const char *json, const char *key)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return 0;
    p += strlen(pat);
    return (int)strtol(p, NULL, 10);
}
