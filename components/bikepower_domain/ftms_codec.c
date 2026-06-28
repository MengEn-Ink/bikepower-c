/**
 * FTMS 协议编解码纯逻辑实现（纯 C，不依赖 ESP-IDF / NimBLE，可 host 编译）
 *
 * 从 main/ble_bikepower.c 抽取，便于 host 单元测试覆盖协议位级编码。
 */
#include "ftms_codec.h"

ftms_codec_response_t ftms_codec_decode_cp(const uint8_t *buf, size_t len,
                                           int power_min, int power_max,
                                           int *out_target_power)
{
    if (buf == NULL || len < 1) return FTMS_CODEC_RSP_UNSUPPORTED;
    uint8_t op = buf[0];
    switch (op) {
    case FTMS_CODEC_OP_REQUEST_CONTROL:
        return FTMS_CODEC_RSP_SUCCESS;
    case FTMS_CODEC_OP_SET_TARGET_POWER:
        if (len < 3) return FTMS_CODEC_RSP_INVALID_PARAM;
        int16_t target_s16 = (int16_t)(buf[1] | ((uint16_t)buf[2] << 8));
        int target = (int)target_s16;
        if (target < power_min || target > power_max) {
            return FTMS_CODEC_RSP_INVALID_PARAM;
        }
        if (out_target_power) *out_target_power = target;
        return FTMS_CODEC_RSP_SUCCESS;
    case FTMS_CODEC_OP_START_RESUME:
        return FTMS_CODEC_RSP_SUCCESS_START;
    case FTMS_CODEC_OP_STOP_PAUSE:
        return FTMS_CODEC_RSP_SUCCESS_STOP;
    case FTMS_CODEC_OP_RESET:
        return FTMS_CODEC_RSP_SUCCESS_RESET;
    default:
        return FTMS_CODEC_RSP_UNSUPPORTED;
    }
}

size_t ftms_codec_encode_response(uint8_t request_op, uint8_t result, uint8_t *out_buf)
{
    if (out_buf == NULL) return 0;
    out_buf[0] = FTMS_CODEC_OP_RESPONSE_CODE;
    out_buf[1] = request_op;
    out_buf[2] = result;
    return 3;
}

size_t ftms_codec_encode_cps(uint16_t power, uint16_t crank_rev, uint16_t crank_time_1024, uint8_t *out_buf)
{
    if (out_buf == NULL) return 0;
    out_buf[0] = 0x20; out_buf[1] = 0x00;  // flags=0x0020
    out_buf[2] = (uint8_t)(power & 0xFF);
    out_buf[3] = (uint8_t)((power >> 8) & 0xFF);
    out_buf[4] = (uint8_t)(crank_rev & 0xFF);
    out_buf[5] = (uint8_t)((crank_rev >> 8) & 0xFF);
    out_buf[6] = (uint8_t)(crank_time_1024 & 0xFF);
    out_buf[7] = (uint8_t)((crank_time_1024 >> 8) & 0xFF);
    return 8;
}

size_t ftms_codec_encode_hr(uint8_t bpm, uint8_t *out_buf)
{
    if (out_buf == NULL) return 0;
    out_buf[0] = 0x00;  // flags=0x00
    out_buf[1] = bpm;
    return 2;
}

size_t ftms_codec_encode_csc(uint16_t crank_rev, uint16_t crank_time_1024, uint8_t *out_buf)
{
    if (out_buf == NULL) return 0;
    out_buf[0] = 0x02;  // flags=0x02
    out_buf[1] = (uint8_t)(crank_rev & 0xFF);
    out_buf[2] = (uint8_t)((crank_rev >> 8) & 0xFF);
    out_buf[3] = (uint8_t)(crank_time_1024 & 0xFF);
    out_buf[4] = (uint8_t)((crank_time_1024 >> 8) & 0xFF);
    return 5;
}

size_t ftms_codec_encode_ftms_bike(uint16_t cadence, uint16_t power, uint8_t *out_buf)
{
    if (out_buf == NULL) return 0;
    uint16_t ftms_flags = 0x0044;
    uint16_t speed = cadence * 30;
    uint16_t ftms_cad = cadence * 2;
    out_buf[0] = (uint8_t)(ftms_flags & 0xFF);
    out_buf[1] = (uint8_t)((ftms_flags >> 8) & 0xFF);
    out_buf[2] = (uint8_t)(speed & 0xFF);
    out_buf[3] = (uint8_t)((speed >> 8) & 0xFF);
    out_buf[4] = (uint8_t)(ftms_cad & 0xFF);
    out_buf[5] = (uint8_t)((ftms_cad >> 8) & 0xFF);
    out_buf[6] = (uint8_t)(power & 0xFF);
    out_buf[7] = (uint8_t)((power >> 8) & 0xFF);
    return 8;
}

size_t ftms_codec_encode_training_status(uint8_t status, uint8_t *out_buf)
{
    if (out_buf == NULL) return 0;
    out_buf[0] = status;
    for (int i = 1; i < 7; i++) out_buf[i] = 0x00;
    return 7;
}
