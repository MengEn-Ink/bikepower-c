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
- `WIFI-13`：当前 HTTP 服务注册 13 个业务路由 + 10 个 Captive Portal 探测路由，并注册 404 兜底重定向处理未知系统探测路径；新增或删除路由都必须同步更新规则、自测脚本和技术文档。
- v4.0.0 起路由契约（前后端分离，全部走 `/api/xxx` JSON；已移除 OTA 与家庭网络路由）：
- `GET /`（返回内联 HTML 整页，CSS/JS 由 `scripts/inline_static.py` 在编译时内联到 `index.html`）
- `GET /favicon.ico`（204）
- `GET /.well-known/captive-portal`（RFC 8910 Captive Portal API，返回 `application/captive+json`，引导系统打开 `http://192.168.4.1/`）
- `GET /api/state`（config/identity/revision/bridge/固件详情/预设/remaining_sec/closing）
- `GET /api/runtime`（实际输出、桥接新鲜度、跑步累计与事务结果）
- `GET /api/time`（纯数字剩余秒数）
- `POST /api/renew`（续期，返回新的剩余秒数）
- `GET /api/mem_stats`（`{"free":..,"min_free":..}`）
- `POST /api/config`（提交 revision 配置，返回 transaction，runtime 确认落盘）
- `POST /api/close-wifi`（保存配置后进入 5 秒收尾窗口，再关闭 WiFi 回到 BLE）
- `GET/POST /api/bridge/scan`（本轮 CPS 候选、启动/停止扫描）
- `POST /api/bridge/control`（test/stop/resume）
- Captive Portal 探测端点：`/generate_204`、`/gen_204`、`/hotspot-detect.html`、`/library/test/success.html`、`/ncsi.txt`、`/connecttest.txt`、`/canonical.html`、`/success.txt`、`/redirect`、`/kindle-wifi/wifistub.html`
- `WIFI-14`：路由注册必须集中维护，避免分散在多个函数里造成行为漂移。
- `WIFI-30`：前端三件套 `main/web/index.html + app.css + app.js` 由 `scripts/inline_static.py` 在编译时内联为 `main/web_index_html.h`，C 侧 `GET /` 直接吐出整页。修改前端后无需手写头文件，构建系统自动重新生成。
- `WIFI-31`：新增/修改 `/api/xxx` 路由时必须同步更新 `tests/check_portal_structure.py` 的 `REQUIRED_BACKEND_ROUTES` 与 `REQUIRED_FRONTEND_APIS` 列表，以及 `tests/mock_server.py` 的对应 handler。
- `WIFI-32`：iOS Captive Portal WebView 不加载外部 `.css/.js` 资源，必须采用内联方案，禁止改回外部链接。
- `WIFI-33`：Captive Portal 探测端点和 404 兜底必须返回 `303 See Other` + `Location: /` + 短文本 body，避免 iOS CNA 对空 302 兼容性差，并覆盖 Chrome/Chromium 等新增探测路径。
- `WIFI-34`：DHCP Captive Portal URI（Option 114）必须指向 `/.well-known/captive-portal` 且使用静态存储期字符串，不能传栈上临时 buffer；DNS 停止必须先 `shutdown()` 再 `close()` 唤醒阻塞 `recvfrom()`。

## 超时与恢复

- `WIFI-15`：配网默认 `300s` 自动关闭，仅显式 `/api/renew` 延长；读取、保存、扫描和测试不得隐式续期。
- `WIFI-16`：配网结束通过 `APP_EVT_WIFI_DONE` 原地关闭 HTTP/DNS/SoftAP 并回到 BLE 主工作模式，不重启设备。

## 配网表单字段

- `WIFI-18`：门户表单字段必须与领域层和 NVS 字段一一对应。
- `WIFI-19`：当前通过 fetch 提交 schema v13 数值配置与 revision，字段名称与 config_store 一致：
- `mode`（0=固定，1=机器人；ERG 是训练会话态，不持久化）
- `device_name`
- `power`
- `cadence`
- `heartrate`
- `has_user_hr`（0=联动心率，1=固定心率），关闭心率通过 broadcast_mask 清除心率位
- `power_float`（功率浮动幅度 ±W，0-100）
- `cadence_float`（踏频浮动幅度 ±RPM，0-20）
- `hr_float`（固定心率浮动幅度 ±BPM，0-20）
- `weight`、`ftp`、`thr_hr`、`ride_type`
- `cycling_source`、`bridge_bound`、`bridge_address`、`bridge_addr_type`、`bridge_calibration`、`bridge_watts`、`bridge_percent_x10`、`bridge_name`
- `WIFI-20`：模拟配置与真实来源独立持久化；实际桥接功率及踏频不覆盖模拟基础参数。运行时 ERG 不覆盖基础功率或配置 dirty。
- `WIFI-21`：新增配置项必须同步领域常量、config_store 解析/迁移、NVS/BLE codec、wifi_portal 快照、前端、mock、自测及文档。
- `WIFI-28`：所有 `/api/xxx` JSON 响应和 `GET /` 内联 HTML 必须设置 `Cache-Control: no-store, no-cache, must-revalidate, max-age=0`，防止浏览器缓存导致页面回退时显示旧数据（v3.2.0 起由 `set_no_store_headers()` 统一注入）。
- `WIFI-29`：v3.2.0 起前端为 SPA 单页面，无多页隐藏表单；`/api/config` 接收的 JSON 必须包含 `WIFI-19` 列出的全部可变字段，前端 `app.js` 提交时统一构造完整 payload。

## 页面与资源边界

- `WIFI-22`：门户单页明亮核心工作台，骑行/跑步与来源渐进披露；模板和 LED 编辑位于电脑工具。保存入口为 `POST /api/config`，区分草稿和实际输出。
- `WIFI-24`：`/api/renew` 只负责延长配网有效期，不得引入隐式保存。
- `WIFI-25`：所有错误路径都要正确释放 HTTP 请求体和连接资源。

## 验证要求

- `WIFI-26`：WiFi 门户行为改动后，必须复核 `tests/stage5_portal_test.py`。
- `WIFI-27`：若改动涉及按钮进入配网或超时退出链路，必须连同 `app_state.c`、`radio_ctrl.c` 的状态流一起检查。
