/**
 * JSON 最小解析（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 轻量 JSON 字段提取，供 config_store 与 wifi_portal 解析配置 JSON 请求体。
 * 简单字段面向工具生成格式，对象数组支持跳过转义及嵌套对象。
 */
#include "json_miniparse.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

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

static const char *skip_space(const char *p)
{
    while (p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char *find_array(const char *json, const char *key)
{
    if (!json || !key) return NULL;
    char pat[40];
    int written = snprintf(pat, sizeof(pat), "\"%s\"", key);
    if (written < 0 || (size_t)written >= sizeof(pat)) return NULL;
    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p = skip_space(p + strlen(pat));
    if (*p++ != ':') return (const char *)-1;
    p = skip_space(p);
    return *p == '[' ? p + 1 : (const char *)-1;
}

static const char *scan_object_end(const char *start)
{
    if (!start || *start != '{') return NULL;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (const char *p = start; *p; p++) {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (*p == '\\') {
                escaped = true;
            } else if (*p == '"') {
                in_string = false;
            }
            continue;
        }
        if (*p == '"') {
            in_string = true;
        } else if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            if (--depth == 0) return p + 1;
            if (depth < 0) return NULL;
        }
    }
    return NULL;
}

static int walk_object_array(const char *json, const char *key, int target,
                             const char **item_start, const char **item_end)
{
    const char *p = find_array(json, key);
    if (!p) return -1;
    if (p == (const char *)-1) return -2;
    p = skip_space(p);
    if (*p == ']') return 0;

    int count = 0;
    while (*p) {
        if (*p != '{') return -2;
        const char *end = scan_object_end(p);
        if (!end) return -2;
        if (count == target && item_start && item_end) {
            *item_start = p;
            *item_end = end;
        }
        count++;
        p = skip_space(end);
        if (*p == ']') return count;
        if (*p++ != ',') return -2;
        p = skip_space(p);
    }
    return -2;
}

int json_get_object_array_count(const char *json, const char *key)
{
    return walk_object_array(json, key, -1, NULL, NULL);
}

bool json_get_object_array_item(const char *json, const char *key, int index,
                                char *out, size_t out_size)
{
    if (index < 0 || !out || out_size == 0) return false;
    const char *start = NULL;
    const char *end = NULL;
    int count = walk_object_array(json, key, index, &start, &end);
    if (count < 0 || index >= count || !start || !end) return false;
    size_t length = (size_t)(end - start);
    if (length >= out_size) {
        out[0] = '\0';
        return false;
    }
    memcpy(out, start, length);
    out[length] = '\0';
    return true;
}
