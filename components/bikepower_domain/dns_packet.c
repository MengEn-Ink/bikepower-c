/**
 * DNS 报文构造纯逻辑实现（纯 C，不依赖 ESP-IDF / lwip，可 host 编译）
 *
 * 从 main/captive_portal.c 抽取，便于 host 单元测试覆盖报文构造。
 */
#include "dns_packet.h"
#include <string.h>

bool dns_packet_build_a_response(const uint8_t *query, size_t query_len,
                                 uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3,
                                 uint8_t ttl,
                                 uint8_t *out_tx, size_t *out_tx_len)
{
    if (query == NULL || out_tx == NULL || out_tx_len == NULL) return false;
    if (query_len < 12) return false;  // 不足 DNS header
    if (query_len > DNS_PACKET_MAX_LEN) return false;

    // 只处理标准 Query（QR=0）且至少 1 个 Question
    uint16_t flags = (uint16_t)((query[2] << 8) | query[3]);
    uint16_t qdcount = (uint16_t)((query[4] << 8) | query[5]);
    if ((flags & 0x8000) != 0 || qdcount == 0) return false;

    // 定位 Question 末尾：跳过 header + QNAME + QTYPE(2) + QCLASS(2)
    size_t off = 12;
    while (off < query_len && query[off] != 0) {
        off += query[off] + 1;
    }
    if (off + 5 > query_len) return false;  // QNAME 异常或越界
    off += 1 + 4;  // 跳过 0 终止符 + QTYPE + QCLASS
    size_t qend = off;

    // 构造响应：复制 Query 并追加 1 个 Answer（16 字节）
    if (qend + 16 > DNS_PACKET_MAX_LEN) return false;
    memcpy(out_tx, query, qend);
    out_tx[2] = 0x81; out_tx[3] = 0x80;  // FLAGS: QR=1, RD=1, RA=1, RCODE=0
    out_tx[6] = 0;                        // ANCOUNT 高字节清零
    out_tx[7] = 1;                        // ANCOUNT=1
    // Answer: NAME 指针(2) + TYPE(2) + CLASS(2) + TTL(4) + RDLENGTH(2) + RDATA(4) = 16 字节
    out_tx[qend + 0] = 0xC0; out_tx[qend + 1] = 0x0C;  // NAME 指针指向 Question QNAME（偏移 12）
    out_tx[qend + 2] = 0x00; out_tx[qend + 3] = 0x01;  // TYPE = A
    out_tx[qend + 4] = 0x00; out_tx[qend + 5] = 0x01;  // CLASS = IN
    out_tx[qend + 6] = 0x00; out_tx[qend + 7] = 0x00;  // TTL 高
    out_tx[qend + 8] = 0x00; out_tx[qend + 9] = ttl;   // TTL 低
    out_tx[qend + 10] = 0x00; out_tx[qend + 11] = 0x04;  // RDLENGTH = 4
    out_tx[qend + 12] = ip0;
    out_tx[qend + 13] = ip1;
    out_tx[qend + 14] = ip2;
    out_tx[qend + 15] = ip3;
    *out_tx_len = qend + 16;
    return true;
}

uint16_t dns_packet_get_qtype(const uint8_t *query, size_t query_len)
{
    if (query == NULL || query_len < 12) return 0;
    uint16_t qdcount = (uint16_t)((query[4] << 8) | query[5]);
    if (qdcount == 0) return 0;

    size_t off = 12;
    while (off < query_len && query[off] != 0) {
        off += query[off] + 1;
    }
    if (off + 5 > query_len) return 0;
    // QTYPE 紧跟 QNAME 0 终止符
    return (uint16_t)((query[off + 1] << 8) | query[off + 2]);
}
