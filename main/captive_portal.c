/**
 * Captive Portal 实现：DNS 劫持 + OS 探测端点重定向
 *
 * DNS 服务器：UDP 53 端口监听，所有 A 记录查询应答 192.168.4.1，使手机连上 SoftAP 后
 * 任意域名都指向配网入口。配合各 OS 的连通性探测端点（Android /generate_204、
 * iOS /hotspot-detect.html 等）触发系统自动弹出 Captive Portal 登录窗口。
 *
 * 参考方案：
 *   - ESP-IDF protocols/http_request 示例的 dns_server 组件
 *   - nordesems/esp-captive-portal 的探测端点覆盖列表
 *   - RFC 1035 DNS 报文格式
 */
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bikepower_config.h"
#include "captive_portal.h"

static const char *TAG = "CAPTIVE";

static int s_dns_sock = -1;
static TaskHandle_t s_dns_task = NULL;

/**
 * DNS 劫持任务：监听 UDP 53，把所有 A 记录查询应答为 SoftAP IP。
 * arg：未使用。
 * 返回值：无。
 * 特殊行为：socket 关闭后退出循环并自删任务；不处理非 A/IN 查询（静默丢弃）。
 */
static void dns_task(void *arg)
{
    (void)arg;
    uint8_t rx[DNS_PACKET_MAX_LEN];
    uint8_t tx[DNS_PACKET_MAX_LEN];
    struct sockaddr_in client;
    socklen_t clen = sizeof(client);

    while (1) {
        int n = recvfrom(s_dns_sock, rx, sizeof(rx), 0, (struct sockaddr *)&client, &clen);
        if (n < 0) {
            if (s_dns_sock < 0) break;  // 已主动关闭，退出
            ESP_LOGE(TAG, "DNS recvfrom 失败 errno=%d", errno);
            continue;
        }
        if (n < 12) continue;  // 不足 DNS header，丢弃

        // 只处理标准 Query（QR=0）且至少 1 个 Question
        uint16_t flags = (rx[2] << 8) | rx[3];
        uint16_t qdcount = (rx[4] << 8) | rx[5];
        if ((flags & 0x8000) != 0 || qdcount == 0) continue;  // 非 Query 或无 Question

        // 定位 Question 末尾：跳过 header + QNAME + QTYPE(2) + QCLASS(2)
        size_t off = 12;
        while (off < (size_t)n && rx[off] != 0) {
            off += rx[off] + 1;
        }
        if (off + 5 > (size_t)n) continue;  // QNAME 异常或越界，丢弃
        off += 1 + 4;  // 跳过 0 终止符 + QTYPE + QCLASS
        size_t qend = off;

        // 构造响应：复制 Query 并追加 1 个 Answer
        if (qend + 16 > sizeof(tx)) continue;  // 缓冲不足，丢弃
        memcpy(tx, rx, qend);
        tx[2] = 0x81; tx[3] = 0x80;  // FLAGS: QR=1, RD=1, RA=1, RCODE=0
        tx[7] = 1;                   // ANCOUNT=1（ANCOUNT 高字节已在 qdcount 同位置，低字节置 1）
        tx[6] = 0;                   // ANCOUNT 高字节清零
        // Answer: NAME 指针(2) + TYPE(2) + CLASS(2) + TTL(4) + RDLENGTH(2) + RDATA(4) = 16 字节
        tx[qend + 0] = 0xC0; tx[qend + 1] = 0x0C;  // NAME 指针指向 Question QNAME（偏移 12）
        tx[qend + 2] = 0x00; tx[qend + 3] = 0x01;  // TYPE = A
        tx[qend + 4] = 0x00; tx[qend + 5] = 0x01;  // CLASS = IN
        tx[qend + 6] = 0x00; tx[qend + 7] = 0x00;  // TTL 高
        tx[qend + 8] = 0x00; tx[qend + 9] = (uint8_t)DNS_ANSWER_TTL_SEC;  // TTL 低
        tx[qend + 10] = 0x00; tx[qend + 11] = 0x04;  // RDLENGTH = 4
        tx[qend + 12] = CAPTIVE_PORTAL_IP_A;
        tx[qend + 13] = CAPTIVE_PORTAL_IP_B;
        tx[qend + 14] = CAPTIVE_PORTAL_IP_C;
        tx[qend + 15] = CAPTIVE_PORTAL_IP_D;
        size_t txlen = qend + 16;

        int sent = sendto(s_dns_sock, tx, txlen, 0, (struct sockaddr *)&client, clen);
        if (sent < 0) {
            ESP_LOGW(TAG, "DNS sendto 失败 errno=%d", errno);
        }
    }

    s_dns_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t captive_portal_start_dns(void)
{
    if (s_dns_sock >= 0) {
        ESP_LOGW(TAG, "DNS 服务器已在运行");
        return ESP_OK;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket 创建失败 errno=%d", errno);
        return ESP_FAIL;
    }

    // SO_REUSEADDR：配网反复进出时，53 端口可能处于 TIME_WAIT，
    // 不加此选项会导致 bind 失败，DNS 劫持无法启动，手机无法自动弹出配网页
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        ESP_LOGW(TAG, "DNS SO_REUSEADDR 设置失败 errno=%d（继续尝试 bind）", errno);
    }

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DNS_SERVER_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind %d 失败 errno=%d", DNS_SERVER_PORT, errno);
        close(sock);
        return ESP_FAIL;
    }

    s_dns_sock = sock;
    if (xTaskCreate(dns_task, "dns_server", TASK_STACK_DNS, NULL, TASK_PRIO_NET, &s_dns_task) != pdPASS) {
        ESP_LOGE(TAG, "DNS 任务创建失败 堆=%lu", (unsigned long)esp_get_free_heap_size());
        close(s_dns_sock);
        s_dns_sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DNS 劫持服务器已启动 (UDP %d → %s) 堆=%lu",
             DNS_SERVER_PORT, CAPTIVE_PORTAL_HOST, (unsigned long)esp_get_free_heap_size());
    return ESP_OK;
}

void captive_portal_stop_dns(void)
{
    if (s_dns_sock >= 0) {
        int sock = s_dns_sock;
        s_dns_sock = -1;  // 先置 -1 让 dns_task 退出循环
        shutdown(sock, 0);
        close(sock);      // 唤醒阻塞在 recvfrom 的 dns_task
    }
    // dns_task 检测到 s_dns_sock<0 后自行 vTaskDelete，s_dns_task 置 NULL
    // 用循环等待确保旧任务真正退出，防止下次 start_dns 时任务泄漏导致
    // 重新进入配网后 DNS 劫持服务异常（手机无法自动弹出配网页）
    if (s_dns_task) {
        for (int i = 0; i < DNS_STOP_MAX_POLLS && s_dns_task != NULL; i++) {
            vTaskDelay(pdMS_TO_TICKS(DNS_STOP_POLL_MS));
        }
        if (s_dns_task != NULL) {
            ESP_LOGW(TAG, "DNS 任务退出超时（%dms），可能泄漏",
                     DNS_STOP_MAX_POLLS * DNS_STOP_POLL_MS);
        }
    }
    ESP_LOGI(TAG, "DNS 劫持服务器已停止");
}

esp_err_t captive_portal_detect_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Captive 探测重定向 uri=%s", req && req->uri ? req->uri : "?");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_sendstr(req, "Redirect to the captive portal");
    return ESP_OK;
}
