#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_INVALID_ARG 2
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
extern unsigned s_lock_depth;
#define portENTER_CRITICAL(lock) ((void)(lock), ++s_lock_depth)
#define portEXIT_CRITICAL(lock) ((void)(lock), --s_lock_depth)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define HTTPD_SOCK_ERR_TIMEOUT -10
#define HTTP_GET 1
#define HTTP_POST 2
#define HTTPD_404_NOT_FOUND 404
typedef int httpd_err_code_t;
typedef void *httpd_handle_t;
typedef struct { size_t content_len; int method; } httpd_req_t;
typedef esp_err_t (*http_handler_t)(httpd_req_t *);
typedef struct { const char *uri; int method; http_handler_t handler; } httpd_uri_t;
typedef struct {
    int server_port, max_uri_handlers, stack_size, max_open_sockets;
    bool lru_purge_enable, keep_alive_enable;
} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
typedef void *esp_timer_handle_t;
typedef struct { void (*callback)(void *); const char *name; } esp_timer_create_args_t;
int64_t esp_timer_get_time(void);
int esp_timer_create(const esp_timer_create_args_t *, esp_timer_handle_t *);
int esp_timer_start_periodic(esp_timer_handle_t, uint64_t);
int esp_timer_stop(esp_timer_handle_t);
int esp_timer_delete(esp_timer_handle_t);
esp_err_t httpd_resp_set_hdr(httpd_req_t *, const char *, const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *, const char *);
esp_err_t httpd_resp_set_status(httpd_req_t *, const char *);
esp_err_t httpd_resp_sendstr(httpd_req_t *, const char *);
esp_err_t httpd_resp_send(httpd_req_t *, const char *, int);
int httpd_req_recv(httpd_req_t *, char *, size_t);
esp_err_t httpd_start(httpd_handle_t *, const httpd_config_t *);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t *);
esp_err_t httpd_register_err_handler(httpd_handle_t, int, esp_err_t (*)(httpd_req_t *, httpd_err_code_t));
uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);
