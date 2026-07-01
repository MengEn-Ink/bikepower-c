# WiFi 配网领域规则（BikePower C）

<!-- stability: tier-2 (稳定，随配网方案调整) -->
<!-- rule-id-prefix: WIFI -->
<!-- applies-to: main/radio_ctrl.c, main/wifi_portal.c, main/app_state.c, tests/stage5_portal_test.py -->

> 本文件只定义 BikePower C 的 WiFi 配网实现规则。射频互斥的硬约束以 `wifi-ble-mutex.md` 为准。

## 角色与触发

- `WIFI-1`：WiFi 仅用于短时本地配网（SoftAP），不承担常驻后台联网职责。v4.0.0 起已移除在线 OTA 与家庭网络配置。
- `WIFI-2`：配网模式只能由按钮长按进入确认窗口后，再次确认触发。
- `WIFI-3`：禁止在开机流程、异常恢复路径或普通 BLE 工作流中自动启动 WiFi。

## SoftAP 固定配置

- `WIFI-4`：SoftAP SSID 固定为 `BikePower`。
- `WIFI-5`：SoftAP 密码固定为 `12345678`。
- `WIFI-6`：SoftAP 信道固定为 `1`。
- `WIFI-7`：HTTP 服务器端口固定为 `80`。
- `WIFI-8`：热点参数统一从 `main/bikepower_config.h` 读取，禁止在 `wifi_portal.c` 内散落硬编码副本。

## BLE/WiFi 射频共用

- `WIFI-9`：配网 SoftAP 默认使用 coex 共存模式，BLE 保持在线继续通知。
- `WIFI-10`：唯一允许启停 WiFi、切 AP 或停 BLE 兜底的入口是 `radio_ctrl.c`。
- `WIFI-11`：`wifi_portal.c` 不得直接调用 `esp_wifi_*` 或自行决定 BLE 停止顺序。

## HTTP 服务与路由

- `WIFI-12`：配网门户使用 `esp_http_server`。
- `WIFI-13`：当前 HTTP 服务注册 8 个业务路由 + 10 个 Captive Portal 探测路由，新增或删除路由都必须同步更新规则、自测脚本和技术文档。
- v4.0.0 起路由契约（前后端分离，全部走 `/api/xxx` JSON；已移除 OTA 与家庭网络路由）：
- `GET /`（返回内联 HTML 整页，CSS/JS 由 `scripts/inline_static.py` 在编译时内联到 `index.html`）
- `GET /favicon.ico`（204）
- `GET /api/state`（设备全状态：device_name/firmware_version/power/cadence/heartrate/mode/profile_*/pacers/remaining_sec/closing）
- `GET /api/time`（纯数字剩余秒数）
- `POST /api/renew`（续期，返回新的剩余秒数）
- `GET /api/mem_stats`（`{"free":..,"min_free":..}`）
- `POST /api/config`（保存配置并实时生效，返回 `{"ok":true}`）
- `POST /api/close-wifi`（保存配置后进入 5 秒收尾窗口，再关闭 WiFi 回到 BLE）
- Captive Portal 探测端点：`/generate_204`、`/gen_204`、`/hotspot-detect.html`、`/library/test/success.html`、`/ncsi.txt`、`/connecttest.txt`、`/canonical.html`、`/success.txt`、`/redirect`、`/kindle-wifi/wifistub.html`
- `WIFI-14`：路由注册必须集中维护，避免分散在多个函数里造成行为漂移。
- `WIFI-30`：前端三件套 `main/web/index.html + app.css + app.js` 由 `scripts/inline_static.py` 在编译时内联为 `main/web_index_html.h`，C 侧 `GET /` 直接吐出整页。修改前端后无需手写头文件，构建系统自动重新生成。
- `WIFI-31`：新增/修改 `/api/xxx` 路由时必须同步更新 `tests/check_portal_structure.py` 的 `REQUIRED_BACKEND_ROUTES` 与 `REQUIRED_FRONTEND_APIS` 列表，以及 `tests/mock_server.py` 的对应 handler。
- `WIFI-32`：iOS Captive Portal WebView 不加载外部 `.css/.js` 资源，必须采用内联方案，禁止改回外部链接。

## 超时与恢复

- `WIFI-15`：配网模式 `180s` 无配置提交或有效续期后必须自动关闭 WiFi。
- `WIFI-16`：配网结束通过 `APP_EVT_WIFI_DONE` 原地关闭 HTTP/DNS/SoftAP 并回到 BLE 主工作模式，不重启设备。

## 配网表单字段

- `WIFI-18`：门户表单字段必须与领域层和 NVS 字段一一对应。
- `WIFI-19`：当前配置提交涉及以下字段（v3.2.0 起前端 SPA 通过 `XMLHttpRequest` POST JSON，字段名即 JSON key）：
- `mode`（`steady` / `road` / `erg`；前端首页分段控件在 `steady`/`road` 间切换，`erg` 由骑行 App 下发自动进入）
- `device_name`
- `power`
- `cadence`
- `heartrate`
- `hr_lock`（0=心率随功率联动，1=锁定固定心率）
- `power_float`（功率浮动幅度 ±W，0-100）
- `cadence_float`（踏频浮动幅度 ±RPM，0-20）
- `hr_float`（固定心率浮动幅度 ±BPM，0-20）
- `profile_weight`
- `profile_ftp`
- `profile_thr_hr`
- `profile_ride_type`
- `WIFI-20`：其中 `power`、`cadence`、`heartrate` 仅在 `steady` 模式下应用；其他模式提交时忽略这三项的手动值。浮动幅度 `power_float`/`cadence_float`/`hr_float` 任意模式接受写入，仅在 `steady` 模式（心率浮动还需锁定心率）实际生效。
- `WIFI-21`：新增配置项时，必须同步修改 `bikepower_config.h` 或领域层常量、`wifi_portal.c` JSON 解析（`apply_json_fields`）与 `/api/state` 输出、`main/web/app.js` 表单字段、`tests/mock_server.py`、自测脚本和技术文档。
- `WIFI-28`：所有 `/api/xxx` JSON 响应和 `GET /` 内联 HTML 必须设置 `Cache-Control: no-store, no-cache, must-revalidate, max-age=0`，防止浏览器缓存导致页面回退时显示旧数据（v3.2.0 起由 `set_no_store_headers()` 统一注入）。
- `WIFI-29`：v3.2.0 起前端为 SPA 单页面，无多页隐藏表单；`/api/config` 接收的 JSON 必须包含 `WIFI-19` 列出的全部可变字段，前端 `app.js` 提交时统一构造完整 payload。

## 页面与资源边界

- `WIFI-22`：v4.0.0 起门户 UI 为 SPA 单页面（`main/web/index.html`），通过 `location.hash` 切换视图（首页/更多/关于），业务保存入口收敛到 `POST /api/config`。
- `WIFI-24`：`/api/renew` 只负责延长配网有效期，不得引入隐式保存。
- `WIFI-25`：所有错误路径都要正确释放 HTTP 请求体和连接资源。

## 验证要求

- `WIFI-26`：WiFi 门户行为改动后，必须复核 `tests/stage5_portal_test.py`。
- `WIFI-27`：若改动涉及按钮进入配网或超时退出链路，必须连同 `app_state.c`、`radio_ctrl.c` 的状态流一起检查。
