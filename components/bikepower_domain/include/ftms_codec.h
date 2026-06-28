/**
 * FTMS 协议编解码纯逻辑（纯 C，不依赖 ESP-IDF / NimBLE，可 host 编译）
 *
 * 从 main/ble_bikepower.c 抽取：
 *   1. Control Point OpCode 决策（哪些 op 合法、返回什么 result）。
 *   2. CPS / HR / CSC / FTMS IndoorBike 通知帧编码。
 *
 * 设计：协议常量在本头文件独立定义，与 main/bikepower_config.h 的 FTMS_OP_* 保持值一致，
 *       便于 host 单测在不依赖 ESP-IDF 的环境验证位级编码正确性。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===== FTMS OpCode（值与 main/bikepower_config.h 一致） =====
#define FTMS_CODEC_OP_REQUEST_CONTROL   0x00
#define FTMS_CODEC_OP_RESET             0x01
#define FTMS_CODEC_OP_SET_TARGET_POWER  0x05
#define FTMS_CODEC_OP_START_RESUME      0x07
#define FTMS_CODEC_OP_STOP_PAUSE        0x08
#define FTMS_CODEC_OP_RESPONSE_CODE     0x80

// ===== FTMS Result Code（值与 main/bikepower_config.h 一致） =====
#define FTMS_CODEC_RESULT_SUCCESS        0x01
#define FTMS_CODEC_RESULT_NOT_SUPPORTED  0x02
#define FTMS_CODEC_RESULT_INVALID_PARAM  0x03

// ===== FTMS 控制点决策结果 =====
typedef enum {
    FTMS_CODEC_RSP_UNSUPPORTED = 0,    // 未知 OpCode 或负载长度不足
    FTMS_CODEC_RSP_SUCCESS,            // 请求成功，无副作用标记
    FTMS_CODEC_RSP_INVALID_PARAM,      // SET_TARGET_POWER 超范围
    FTMS_CODEC_RSP_SUCCESS_START,      // START_RESUME：成功并通知 status=0x01
    FTMS_CODEC_RSP_SUCCESS_STOP,       // STOP_PAUSE：成功并通知 status=0x02
    FTMS_CODEC_RSP_SUCCESS_RESET,      // RESET：成功并通知 status=0x00
} ftms_codec_response_t;

/**
 * 解析 FTMS Control Point 写入请求，决定响应码。
 * buf：写入数据缓冲区。
 * len：写入数据长度（≥1）。
 * power_min/power_max：合法 ERG 目标功率范围（含）。
 * out_target_power：当 op=SET_TARGET_POWER 且合法时输出目标功率。
 * 返回值：响应分类枚举。
 * 特殊行为：SET_TARGET_POWER 长度<3 或目标超范围返回 INVALID_PARAM；
 *           未知 OpCode 返回 NOT_SUPPORTED（调用方据此发送 Response Code）。
 */
ftms_codec_response_t ftms_codec_decode_cp(const uint8_t *buf, size_t len,
                                           int power_min, int power_max,
                                           int *out_target_power);

/**
 * 编码 FTMS Response Code 通知（3 字节）。
 * request_op：原始请求 OpCode。
 * result：FTMS 结果码（FTMS_CODEC_RESULT_*）。
 * out_buf：输出缓冲区，容量≥3。
 * 返回值：编码后长度（3），失败返回 0。
 */
size_t ftms_codec_encode_response(uint8_t request_op, uint8_t result, uint8_t *out_buf);

/**
 * 编码 CPS Measurement 通知帧（8 字节）。
 * flags 固定 0x0020，含 power + crankRev + crankTime1024。
 * power：当前功率（W）。
 * crank_rev：曲柄累计圈数。
 * crank_time_1024：曲柄累计时间（1/1024 秒）。
 * out_buf：输出缓冲区，容量≥8。
 * 返回值：编码后长度（8），失败返回 0。
 */
size_t ftms_codec_encode_cps(uint16_t power, uint16_t crank_rev, uint16_t crank_time_1024, uint8_t *out_buf);

/**
 * 编码 HR Measurement 通知帧（2 字节）。
 * flags 固定 0x00，仅含 bpm。
 * bpm：当前心率。
 * out_buf：输出缓冲区，容量≥2。
 * 返回值：编码后长度（2），失败返回 0。
 */
size_t ftms_codec_encode_hr(uint8_t bpm, uint8_t *out_buf);

/**
 * 编码 CSC Measurement 通知帧（5 字节）。
 * flags 固定 0x02，含 crankRev + crankTime1024。
 * crank_rev/crank_time_1024：同 CPS。
 * out_buf：输出缓冲区，容量≥5。
 * 返回值：编码后长度（5），失败返回 0。
 */
size_t ftms_codec_encode_csc(uint16_t crank_rev, uint16_t crank_time_1024, uint8_t *out_buf);

/**
 * 编码 FTMS IndoorBike 通知帧（8 字节）。
 * flags 固定 0x0044，含 speed + cadence + power。
 * cadence：踏频（RPM），speed=cad*30, ftms_cad=cad*2。
 * power：当前功率（W）。
 * out_buf：输出缓冲区，容量≥8。
 * 返回值：编码后长度（8），失败返回 0。
 */
size_t ftms_codec_encode_ftms_bike(uint16_t cadence, uint16_t power, uint8_t *out_buf);

/**
 * 编码 FTMS Training Status 通知帧（7 字节，首字节为 status）。
 * status：训练状态（0x00 Idle / 0x01 Started / 0x02 Stopped）。
 * out_buf：输出缓冲区，容量≥7。
 * 返回值：编码后长度（7），失败返回 0。
 */
size_t ftms_codec_encode_training_status(uint8_t status, uint8_t *out_buf);

#ifdef __cplusplus
}
#endif
