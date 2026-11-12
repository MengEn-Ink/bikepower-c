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
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include "domain_config.h"

static bool json_space(unsigned char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static const char *string_end(const char *p)
{
    if (!p || *p++ != '"') return NULL;
    while (*p) {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '"') return p;
        if (ch < 0x20) return NULL;
        if (ch != '\\') continue;
        ch = (unsigned char)*p++;
        if (!ch) return NULL;
        if (ch == 'u') {
            for (unsigned i = 0; i < 4; i++) {
                if (!isxdigit((unsigned char)*p)) return NULL;
                p++;
            }
        } else if (!strchr("\"\\/bfnrt", ch)) return NULL;
    }
    return NULL;
}

static const char *number_end(const char *p)
{
    if (*p == '-') p++;
    if (*p == '0') p++;
    else {
        if (*p < '1' || *p > '9') return NULL;
        do { p++; } while (*p >= '0' && *p <= '9');
    }
    if (*p == '.') {
        p++;
        if (*p < '0' || *p > '9') return NULL;
        do { p++; } while (*p >= '0' && *p <= '9');
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (*p < '0' || *p > '9') return NULL;
        do { p++; } while (*p >= '0' && *p <= '9');
    }
    return p;
}

static const char *value_end(const char *p, unsigned depth);

/* 前面的成员已经校验过；只跳过其值，不递归重验嵌套对象。 */
static const char *validated_value_end(const char *p)
{
    unsigned nesting = 0;
    for (; *p; p++) {
        if (*p == '"') {
            p = string_end(p);
            if (!p) return NULL;
            p--;
        } else if (*p == '{' || *p == '[') {
            nesting++;
        } else if (*p == '}' || *p == ']') {
            if (!nesting) return p;
            if (--nesting == 0) return p + 1;
        } else if (*p == ',' && !nesting) {
            return p;
        }
    }
    return p;
}

static const char *object_end(const char *p, unsigned depth)
{
    const char *start = p++;
    if (*p == '}') return p + 1;
    while (*p) {
        const char *key = p;
        const char *end = string_end(p);
        if (!end || *end != ':' || memchr(key, '\\', (size_t)(end - key))) return NULL;
        const char *previous = start + 1;
        while (previous < key) {
            const char *previous_end = string_end(previous);
            if (!previous_end) return NULL;
            if ((size_t)(previous_end - previous) == (size_t)(end - key)
                && memcmp(previous, key, (size_t)(end - key)) == 0) return NULL;
            previous = validated_value_end(previous_end + 1);
            if (!previous) return NULL;
            if (*previous == ',') previous++;
        }
        p = value_end(end + 1, depth + 1);
        if (!p) return NULL;
        if (*p == '}') return p + 1;
        if (*p++ != ',') return NULL;
    }
    return NULL;
}

static const char *value_end(const char *p, unsigned depth)
{
    if (!p || depth > JSON_IMPORT_MAX_DEPTH) return NULL;
    if (*p == '"') return string_end(p);
    if (*p == '{') return object_end(p, depth);
    if (*p == '[') {
        p++;
        if (*p == ']') return p + 1;
        while (*p) {
            p = value_end(p, depth + 1);
            if (!p) return NULL;
            if (*p == ']') return p + 1;
            if (*p++ != ',') return NULL;
        }
        return NULL;
    }
    for (unsigned i = 0; i < 3; i++) {
        const char *literal = i == 0 ? "true" : i == 1 ? "false" : "null";
        size_t length = strlen(literal);
        if (strncmp(p, literal, length) == 0) return p + length;
    }
    return number_end(p);
}

bool json_normalize_object(const char *json, char *out, size_t capacity)
{
    if (!json || !out || capacity == 0 || strnlen(json, capacity) >= capacity) return false;
    size_t n = 0;
    bool quoted = false, escaped = false;
    for (const char *p = json; *p; p++) {
        if (!quoted && json_space((unsigned char)*p)) {
            const char *next = p;
            while (json_space((unsigned char)*next)) next++;
            if (n && *next && !strchr("{}[],:", out[n - 1])
                && !strchr("{}[],:", *next)) return false;
            continue;
        }
        if (n + 1 >= capacity) return false;
        out[n++] = *p;
        if (quoted) {
            if (escaped) escaped = false;
            else if (*p == '\\') escaped = true;
            else if (*p == '"') quoted = false;
        } else if (*p == '"') quoted = true;
    }
    out[n] = 0;
    if (*out != '{') return false;
    const char *end = value_end(out, 0);
    return end && *end == 0;
}

const char *json_member_value(const char *json, const char *key)
{
    if (!json || !key || *json != '{') return NULL;
    const char *p = json + 1;
    size_t length = strlen(key);
    while (*p && *p != '}') {
        const char *end = string_end(p);
        if (!end || *end != ':') return NULL;
        if ((size_t)(end - p) == length + 2 && memcmp(p + 1, key, length) == 0)
            return end + 1;
        p = value_end(end + 1, 1);
        if (!p) return NULL;
        if (*p == ',') p++;
        else if (*p != '}') return NULL;
    }
    return NULL;
}

bool json_get_int_checked(const char *json, const char *key, int *value)
{
    const char *p = json_member_value(json, key);
    if (!p || !value || (*p != '-' && (*p < '0' || *p > '9'))) return false;
    errno = 0;
    char *end = NULL;
    long parsed = strtol(p, &end, 10);
    if (p == end || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX
        || number_end(p) != end || (*end != ',' && *end != '}')) return false;
    *value = (int)parsed;
    return true;
}

bool json_get_str(const char *json, const char *key, char *out, size_t out_sz)
{
    if (!json || !key || !out || out_sz == 0) return false;
    const char *value = json_member_value(json, key);
    if (!value || *value != '"') return false;
    const char *p = value + 1;
    const char *end = strchr(p, '"');
    if (!end || memchr(p, '\\', (size_t)(end - p))) return false;
    size_t len = (size_t)(end - p);
    if (len >= out_sz) return false;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

int json_get_int(const char *json, const char *key)
{
    int value = 0;
    return json_get_int_checked(json, key, &value) ? value : 0;
}

static const char *skip_space(const char *p)
{
    while (p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char *find_array(const char *json, const char *key)
{
    const char *p = json_member_value(json, key);
    if (!p) return NULL;
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
