# WiFi 管理器模板

BikePower C 使用 `radio_ctrl` + `wifi_portal` 双模块架构：

```
radio_ctrl          wifi_portal
  └─ stop_ble() ──→ 释放射频
  └─ start_ap() ──→ SoftAP + HTTP
                     ├── 11 路由
                     ├── 表单解析
                     ├── 保存配置
                     └── esp_restart()
```

## 射频互斥规则

1. 先调 `radio_ctrl_stop_ble()` → NimBLE 停广播、断开连接
2. 后调 `wifi_portal_start()` → SoftAP + HTTP 启动
3. 任何失败 → `radio_ctrl_rollback()` → 启用 BLE
4. 超时 180s → `esp_restart()` → 系统重启恢复 BLE

## 参考

- `main/radio_ctrl.h` / `main/radio_ctrl.c`
- `main/wifi_portal.h` / `main/wifi_portal.c`
