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

/**
 * 从 JSON 字符串中提取指定键的字符串值。
 * 查找 "key":"value" 模式，返回 value 部分（不解析转义）。
 * json：完整 JSON 字符串。
 * key：待提取的键名（不带引号）。
 * out：输出缓冲区。
 * out_sz：输出缓冲区大小。
 * 返回值：true 表示找到并写入，false 表示未找到。
 * 特殊行为：遇到第一个闭合引号即结束，不处理转义（latest.json 由工具生成，无转义）。
 */
bool json_get_str(const char *json, const char *key, char *out, size_t out_sz);

/**
 * 从 JSON 字符串中提取指定键的整数值。
 * 查找 "key":数字 模式，解析为 int。
 * json：完整 JSON 字符串。
 * key：待提取的键名。
 * 返回值：解析后的整数值，未找到返回 0。
 */
int json_get_int(const char *json, const char *key);
