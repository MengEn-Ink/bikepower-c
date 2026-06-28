/**
 * DNS 报文构造纯逻辑（纯 C，不依赖 ESP-IDF / lwip，可 host 编译）
 *
 * 从 main/captive_portal.c 抽取 DNS A 记录响应构造逻辑，
 * 便于 host 单元测试覆盖报文长度校验、FLAGS 设置、Answer 段编码。
 *
 * 设计：纯函数，输入查询报文字节 + SoftAP IP，输出响应报文。
 *       不依赖任何 socket / lwip 头文件。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DNS 响应缓冲区最大长度（DNS over UDP 标准 512 字节）。
 */
#define DNS_PACKET_MAX_LEN 512

/**
 * 默认 DNS Answer TTL（秒）。
 */
#define DNS_PACKET_DEFAULT_TTL 60

/**
 * 构造 DNS A 记录劫持响应。
 * query：DNS 查询报文（≥12 字节 header + Question）。
 * query_len：查询报文长度。
 * ip0/ip1/ip2/ip3：SoftAP IP 四段（如 192/168/4/1）。
 * ttl：Answer TTL（秒），通常 60。
 * out_tx：输出响应缓冲区，容量至少 DNS_PACKET_MAX_LEN。
 * out_tx_len：输出响应实际长度。
 * 返回值：true 表示成功构造响应；false 表示查询非法（长度不足、非 Query、QNAME 异常）。
 * 特殊行为：仅在标准 Query（QR=0, QDCOUNT≥1）时构造响应；
 *           响应 FLAGS=0x8180（QR=1, RD=1, RA=1, RCODE=0），ANCOUNT=1。
 */
bool dns_packet_build_a_response(const uint8_t *query, size_t query_len,
                                 uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3,
                                 uint8_t ttl,
                                 uint8_t *out_tx, size_t *out_tx_len);

/**
 * 解析 DNS 查询的 QTYPE 字段（用于调试/校验）。
 * query：DNS 查询报文。
 * query_len：查询报文长度。
 * 返回值：QTYPE 值（1=A, 28=AAAA 等），非法查询返回 0。
 */
uint16_t dns_packet_get_qtype(const uint8_t *query, size_t query_len);

#ifdef __cplusplus
}
#endif
