/**
 * 受限 JSON 对象解析（纯 C，无动态分配）。
 * 配置导入先规范化并校验，member/checked helper 只消费规范化对象。
 */
#pragma once
#include <stddef.h>
#include <stdbool.h>

/* 删除字符串外合法空白并验证完整对象；不支持转义键名，重复键拒绝。
 * 长度和深度有界。失败输出可能不完整，调用方不能使用。 */
bool json_normalize_object(const char *json, char *out, size_t capacity);

/* 查当前对象直接成员，不搜索嵌套对象；不存在返回 NULL。 */
const char *json_member_value(const char *json, const char *key);

/* 仅接受完整十进制整数 token，int 溢出/错误类型拒绝，失败保持输出。 */
bool json_get_int_checked(const char *json, const char *key, int *value);

/* 简单未转义字符串兼容入口；不足/转义/错误时拒绝且保持输出。
 * 不是配置 Unicode 导入接口。 */
bool json_get_str(const char *json, const char *key, char *out, size_t out_sz);

/* 兼容入口，缺失或非法返回 0；需要区分错误时使用 checked 接口。 */
int json_get_int(const char *json, const char *key);

/* 对象数组项数；缺失 -1，错误 -2。 */
int json_get_object_array_count(const char *json, const char *key);

/* 提取带花括号的对象数组项；容量/索引/语法错误返回 false。 */
bool json_get_object_array_item(const char *json, const char *key, int index,
                                char *out, size_t out_size);
