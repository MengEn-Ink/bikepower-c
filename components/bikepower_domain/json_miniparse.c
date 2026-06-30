/**
 * JSON 最小解析（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 轻量 JSON 字段提取，供 config_store 与 wifi_portal 解析配置 JSON 请求体。
 * 仅支持工具生成的无转义扁平 JSON。
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
