/**
 * DIS 设备身份覆盖（纯 C，可 host 单测）。
 *
 * 用途：允许网页把「设备比对 → 复制为模板」得到的厂商、型号、序列号、
 * 硬件/固件/软件版本写入开发板，开发板随后通过标准 Device Information
 * Service (0x180A) 对外报告这些值。某字段为空字符串表示「使用固件内置值」。
 *
 * 线格式（BLE 特征 7a5a0008-… 读写共用，所有长度均为字节）：
 *   [0] 协议版本 = DEVICE_IDENTITY_PROTOCOL_VERSION
 *   [1] 字段数量 N（1..DEVICE_IDENTITY_FIELD_COUNT）
 *   之后按字段顺序重复 N 次：[len][utf8 bytes...]，len ≤ DEVICE_IDENTITY_TEXT_MAX-1
 * 字段顺序固定为 device_identity_field_t；N 小于字段总数时其余字段视为空。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_IDENTITY_PROTOCOL_VERSION 1
#define DEVICE_IDENTITY_TEXT_MAX         32   // 含结尾 '\0'，有效 UTF-8 最多 31 字节

typedef enum {
    DEVICE_IDENTITY_MANUFACTURER = 0, // 0x2A29 Manufacturer Name String
    DEVICE_IDENTITY_MODEL,            // 0x2A24 Model Number String
    DEVICE_IDENTITY_SERIAL,           // 0x2A25 Serial Number String
    DEVICE_IDENTITY_HARDWARE,         // 0x2A27 Hardware Revision String
    DEVICE_IDENTITY_FIRMWARE,         // 0x2A26 Firmware Revision String
    DEVICE_IDENTITY_SOFTWARE,         // 0x2A28 Software Revision String
    DEVICE_IDENTITY_FIELD_COUNT,
} device_identity_field_t;

/* 编码后最大长度：版本 + 数量 + 每字段(1 字节长度 + 31 字节内容)。 */
#define DEVICE_IDENTITY_WIRE_MAX \
    (2 + DEVICE_IDENTITY_FIELD_COUNT * DEVICE_IDENTITY_TEXT_MAX)

typedef struct {
    char fields[DEVICE_IDENTITY_FIELD_COUNT][DEVICE_IDENTITY_TEXT_MAX];
} device_identity_t;

/* 清空全部覆盖值（全部回退到固件内置值）。 */
void device_identity_defaults(device_identity_t *id);

/* 是否存在任一非空覆盖字段。 */
bool device_identity_has_override(const device_identity_t *id);

/**
 * 读取某字段的生效值：覆盖值非空时返回覆盖值，否则返回 fallback。
 * id 为 NULL 或字段越界时返回 fallback。
 */
const char *device_identity_effective(const device_identity_t *id,
                                      device_identity_field_t field,
                                      const char *fallback);

/**
 * 校验单个文本字段：长度 ≤ 31 字节、无 ASCII 控制字符、为合法 UTF-8。
 */
bool device_identity_text_valid(const uint8_t *text, size_t len);

/**
 * 编码为线格式（总是写出全部字段）。
 * 返回写入字节数；参数非法或缓冲区不足返回 0。
 */
size_t device_identity_encode(const device_identity_t *id, uint8_t *out, size_t out_size);

/**
 * 解码线格式。成功时完整覆盖 *out；失败时 *out 保持不变。
 * 拒绝：版本不符、字段数为 0 或超过总数、长度越界、尾部多余字节、非法文本。
 */
bool device_identity_decode(const uint8_t *data, size_t len, device_identity_t *out);
