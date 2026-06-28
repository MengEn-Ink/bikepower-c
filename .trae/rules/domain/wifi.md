# WiFi 配网领域规则（BikePower C）

<!-- stability: tier-2 (稳定，随配网方案调整) -->
<!-- rule-id-prefix: WIFI -->
<!-- applies-to: main/radio_ctrl.c, main/wifi_portal.c, main/app_state.c, tests/stage5_portal_test.py -->

> 本文件只定义 BikePower C 的 WiFi 配网实现规则。射频互斥的硬约束以 `wifi-ble-mutex.md` 为准。

## 角色与触发

- `WIFI-1`：WiFi 仅用于短时本地配网、家庭网络配置和在线 OTA，不承担常驻后台联网职责。
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
- `WIFI-10`：唯一允许启停 WiFi、切 AP/APSTA/STA 或停 BLE 兜底的入口是 `radio_ctrl.c`。
- `WIFI-11`：`wifi_portal.c` 不得直接调用 `esp_wifi_*` 或自行决定 BLE 停止顺序；OTA 独占 STA 也必须通过 `radio_ctrl_enter_sta_exclusive()`。

## HTTP 服务与路由

- `WIFI-12`：配网门户使用 `esp_http_server`。
- `WIFI-13`：当前 HTTP 服务注册 21 个业务路由 + 10 个 Captive Portal 探测路由，新增或删除路由都必须同步更新规则、自测脚本和技术文档。
- 路由列表：
- `GET /`
- `POST /config`
- `POST /close-wifi`
- `GET /success`
- `GET /more`
- `GET /about`
- `GET /ota`
- `POST /ota/check`
- `GET /ota/status`
- `POST /ota/upgrade`
- `POST /wifi/scan`
- `GET /wifi/scan/status`
- `POST /wifi/save`
- `GET /wifi/connect/status`
- `POST /draft`
- `GET /ui/module`
- `GET /closing`
- `GET /time`
- `GET /renew`
- `GET /mem_stats`
- `GET /favicon.ico`
- Captive Portal 探测端点：`/generate_204`、`/gen_204`、`/hotspot-detect.html`、`/library/test/success.html`、`/ncsi.txt`、`/connecttest.txt`、`/canonical.html`、`/success.txt`、`/redirect`、`/kindle-wifi/wifistub.html`
- `WIFI-14`：路由注册必须集中维护，避免分散在多个函数里造成行为漂移。

## 超时与恢复

- `WIFI-15`：配网模式 `180s` 无配置提交或有效续期后必须自动关闭 WiFi。
- `WIFI-16`：配网结束通过 `APP_EVT_WIFI_DONE` 原地关闭 HTTP/DNS/SoftAP 并回到 BLE 主工作模式，不重启设备。
- `WIFI-17`：OTA 升级进入独占 STA 后会停 BLE 与 SoftAP，下载写入完成或失败后通过重启恢复。

## 配网表单字段

- `WIFI-18`：门户表单字段必须与领域层和 NVS 字段一一对应。
- `WIFI-19`：当前配置提交与草稿回写涉及以下字段：
- `mode`
- `device_name`
- `power`
- `cadence`
- `heartrate`
- `profile_weight`
- `profile_ftp`
- `profile_thr_hr`
- `profile_ride_type`
- `WIFI-20`：其中 `power`、`cadence`、`heartrate` 仅在 `steady` 模式下应用；其他模式提交时忽略这三项的手动值。
- `WIFI-21`：新增配置项时，必须同步修改 `bikepower_config.h` 或领域层常量、`wifi_portal.c` 表单解析、自测脚本和技术文档。
- `WIFI-28`：所有动态 HTML 响应（首页、更多配置页、成功页）必须设置 `Cache-Control: no-store, no-cache, must-revalidate, max-age=0`，防止浏览器缓存导致页面回退时显示旧数据。
- `WIFI-29`：多页表单中每个提交到 `/draft` 或 `/config` 的页面隐藏表单必须包含 `WIFI-19` 列出的全部可变字段，否则提交时会丢失未包含的字段值。

## 页面与资源边界

- `WIFI-22`：门户 UI 可以分首页、更多配置页和模块化片段输出，但业务保存入口保持收敛到 `/config`。
- `WIFI-23`：`/draft` 只允许回写内存草稿，不得保存到 NVS，也不得触发重启。
- `WIFI-24`：`/renew` 只负责延长配网有效期，不得引入隐式保存。
- `WIFI-25`：所有错误路径都要正确释放 HTTP 请求体和连接资源。

## 验证要求

- `WIFI-26`：WiFi 门户行为改动后，必须复核 `tests/stage5_portal_test.py`。
- `WIFI-27`：若改动涉及按钮进入配网或超时退出链路，必须连同 `app_state.c`、`radio_ctrl.c` 的状态流一起检查。
