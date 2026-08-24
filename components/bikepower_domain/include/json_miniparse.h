/**
 * JSON 最小解析（纯 C，不依赖 ESP-IDF，可 host 编译）
 *
 * 轻量 JSON 字段提取（纯 C，不依赖 ESP-IDF，可 host 编译），
 * 供 config_store 与 wifi_portal 解析配置 JSON 请求体。
 * 仅支持工具生成的无转义扁平 JSON。
 */
#pragma once
#include <stddef.h>
#include <stdbool.h>

bool json_get_str(const char *json, const char *key, char *out, size_t out_sz);

int json_get_int(const char *json, const char *key);
