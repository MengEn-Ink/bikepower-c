# WiFi JSON 路由模板

```c
static esp_err_t h_api_example(httpd_req_t *req)
{
    send_json(req, "{\"ok\":true}");
    return ESP_OK;
}

// 在 start_httpd() 的集中路由表中注册。
{ .uri = "/api/example", .method = HTTP_GET, .handler = h_api_example },
```

## 安全规则

- 业务接口统一使用 `/api/xxx` JSON；HTML/CSS/JS 只维护在 `main/web/` 并由构建脚本内联
- `/favicon.ico` 返回 204
- POST JSON 必须限制 body 长度、校验字段范围并正确转义字符串
- 新增/删除路由必须同步 `tests/check_portal_structure.py`、`tests/mock_server.py`、技术参考和 WiFi 规则
- 普通配网必须经 `radio_ctrl` 走 coex 共存，BLE 保持在线；仅共存初始化失败时才由 `radio_ctrl` 停 BLE 兜底
