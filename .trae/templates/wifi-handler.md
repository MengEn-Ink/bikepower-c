# WiFi 处理路由模板

```c
// wifi_portal.c — 添加新路由
static esp_err_t new_route_handler(httpd_req_t *req) {
    // 处理请求
    httpd_resp_set_hdr(req, "Content-Type", "text/html; charset=utf-8");
    httpd_resp_sendstr(req, html);
    return ESP_OK;
}

// 注册路由（wifi_portal_init 中）
httpd_uri_t new_route = {
    .uri       = "/new-route",
    .method    = HTTP_GET,
    .handler   = new_route_handler,
    .user_ctx  = NULL
};
httpd_register_uri_handler(server, &new_route);
```

## 安全规则

- `/success` 必须有直访守卫（302 重定向到首页当未提交配置时）
- `/favicon.ico` 返回 204
- POST 表单参数必须做长度和范围校验
- 配网期间关闭 BLE（radio_ctrl 处理）
