# 源码事实摘要

> 由 `python3 scripts/tools/doc_facts.py --write` 生成；请修改源码后重新生成，不手工编辑。

- 固件版本：v4.2.1
- 配置 schema：14
- 配网超时：300 秒，仅显式续期
- BLE 通知周期：1000 毫秒
- 已注册 HTTP 路由：23 个（按 method + URI 计数）
- 板级生产模块：15 个
- 领域生产模块：17 个

## HTTP 路由

- `GET /`
- `GET /favicon.ico`
- `GET /.well-known/captive-portal`
- `GET /api/state`
- `GET /api/runtime`
- `POST /api/config`
- `POST /api/close-wifi`
- `POST /api/bridge/scan`
- `GET /api/bridge/scan`
- `POST /api/bridge/control`
- `GET /api/time`
- `POST /api/renew`
- `GET /api/mem_stats`
- `GET /generate_204`
- `GET /gen_204`
- `GET /hotspot-detect.html`
- `GET /library/test/success.html`
- `GET /ncsi.txt`
- `GET /connecttest.txt`
- `GET /canonical.html`
- `GET /success.txt`
- `GET /redirect`
- `GET /kindle-wifi/wifistub.html`

## 生产模块

- `main/app_main.c`
- `main/ble_bikepower.c`
- `main/ble_config_service.c`
- `main/ble_scanner.c`
- `main/ble_power_bridge.c`
- `main/bridge_runtime.c`
- `main/app_state.c`
- `main/button.c`
- `main/led.c`
- `main/board_detect.c`
- `main/nvs_store.c`
- `main/device_identity_store.c`
- `main/radio_ctrl.c`
- `main/wifi_portal.c`
- `main/captive_portal.c`
- `components/bikepower_domain/ble_config_codec.c`
- `components/bikepower_domain/ble_payload_codec.c`
- `components/bikepower_domain/bridge_failover.c`
- `components/bikepower_domain/button_debounce.c`
- `components/bikepower_domain/button_logic.c`
- `components/bikepower_domain/config_store.c`
- `components/bikepower_domain/cps_input_codec.c`
- `components/bikepower_domain/device_analysis.c`
- `components/bikepower_domain/device_identity.c`
- `components/bikepower_domain/domain_rng.c`
- `components/bikepower_domain/hr_model.c`
- `components/bikepower_domain/json_miniparse.c`
- `components/bikepower_domain/led_pattern.c`
- `components/bikepower_domain/power_bridge.c`
- `components/bikepower_domain/power_engine.c`
- `components/bikepower_domain/profile.c`
- `components/bikepower_domain/running_engine.c`

## 验收边界

断言数与 PASS 数量来自具体测试运行，不能由文件数量推断。本摘要不代表构建或真机测试通过；验收结果见 PROGRESS 与对应镜像的证据。
