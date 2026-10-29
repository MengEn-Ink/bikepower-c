/**
 * DIS 设备身份覆盖编解码实现，见 device_identity.h。
 */
#include "device_identity.h"

#include <string.h>

void device_identity_defaults(device_identity_t *id)
{
    if (!id) return;
    memset(id, 0, sizeof(*id));
}

bool device_identity_has_override(const device_identity_t *id)
{
    if (!id) return false;
    for (int i = 0; i < DEVICE_IDENTITY_FIELD_COUNT; i++) {
        if (id->fields[i][0] != '\0') return true;
    }
    return false;
}

const char *device_identity_effective(const device_identity_t *id,
                                      device_identity_field_t field,
                                      const char *fallback)
{
    if (!id || field < 0 || field >= DEVICE_IDENTITY_FIELD_COUNT) return fallback;
    return id->fields[field][0] ? id->fields[field] : fallback;
}

bool device_identity_text_valid(const uint8_t *text, size_t len)
{
    if (len >= DEVICE_IDENTITY_TEXT_MAX) return false;
    if (len > 0 && !text) return false;
    size_t i = 0;
    while (i < len) {
        uint8_t c = text[i];
        if (c < 0x20 || c == 0x7F) return false;          // 控制字符
        if (c < 0x80) { i++; continue; }
        size_t extra;
        if ((c & 0xE0) == 0xC0 && c >= 0xC2) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4) extra = 3;
        else return false;
        if (i + extra >= len) return false;                // 多字节序列被截断
        for (size_t k = 1; k <= extra; k++) {
            if ((text[i + k] & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

size_t device_identity_encode(const device_identity_t *id, uint8_t *out, size_t out_size)
{
    if (!id || !out) return 0;
    size_t need = 2;
    for (int i = 0; i < DEVICE_IDENTITY_FIELD_COUNT; i++) {
        need += 1 + strnlen(id->fields[i], DEVICE_IDENTITY_TEXT_MAX - 1);
    }
    if (out_size < need) return 0;
    size_t off = 0;
    out[off++] = DEVICE_IDENTITY_PROTOCOL_VERSION;
    out[off++] = DEVICE_IDENTITY_FIELD_COUNT;
    for (int i = 0; i < DEVICE_IDENTITY_FIELD_COUNT; i++) {
        size_t n = strnlen(id->fields[i], DEVICE_IDENTITY_TEXT_MAX - 1);
        out[off++] = (uint8_t)n;
        memcpy(out + off, id->fields[i], n);
        off += n;
    }
    return off;
}

bool device_identity_decode(const uint8_t *data, size_t len, device_identity_t *out)
{
    if (!data || !out || len < 2) return false;
    if (data[0] != DEVICE_IDENTITY_PROTOCOL_VERSION) return false;
    uint8_t count = data[1];
    if (count == 0 || count > DEVICE_IDENTITY_FIELD_COUNT) return false;

    device_identity_t tmp;
    device_identity_defaults(&tmp);
    size_t off = 2;
    for (uint8_t i = 0; i < count; i++) {
        if (off >= len) return false;
        uint8_t n = data[off++];
        if (n >= DEVICE_IDENTITY_TEXT_MAX || off + n > len) return false;
        if (!device_identity_text_valid(data + off, n)) return false;
        memcpy(tmp.fields[i], data + off, n);
        tmp.fields[i][n] = '\0';
        off += n;
    }
    if (off != len) return false;
    *out = tmp;
    return true;
}
