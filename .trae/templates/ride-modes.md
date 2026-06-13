# 骑行模式模板

复制此模板到新的骑行模式实现。

```c
// === 模式名模式（替换 MODE_NAME） ===
static int _mode_name_power(power_engine_t *e) {
    // 生成功率逻辑
    return power;
}

static int _mode_name_cadence(power_engine_t *e) {
    // 生成踏频逻辑
    return cadence;
}

static int _mode_name_heartrate(power_engine_t *e, int power) {
    // 生成心率逻辑
    return heartrate;
}
```

## 模式注册

1. 在 `domain_config.h` `ride_mode_t` 中添加枚举值
2. 更新 `RIDE_MODE_COUNT`
3. 在 `power_engine_snapshot()` 的 switch-case 中添加分支
4. 在 `config_store_get_defaults()` 中添加默认值
5. 在 `test_domain.c` 中添加断言

## 模板引用

- 配置持久化：参考 `config-persist.md`
- BLE 集成：参考 `ble-service.md`
- WiFi 配网：参考 `wifi-handler.md` / `wifi-manager.md`
