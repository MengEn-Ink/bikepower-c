# WiFi 配网领域规则（BikePower C）

<!-- stability: tier-2 (稳定，随配网方案调整) -->
<!-- rule-id-prefix: WIFI -->
<!-- applies-to: main/radio_ctrl.c, main/wifi_portal.c, main/app_state.c, tests/stage5_portal_test.py -->

> 本文件只定义 BikePower C 的 WiFi 配网实现规则。射频互斥的硬约束以 `wifi-ble-mutex.md` 为准。

## 角色与触发

- `WIFI-1`：WiFi 仅用于短时本地配网，不承担常驻联网、扫描周边热点或后台同步职责。
- `WIFI-2`：配网模式只能由按钮长按进入确认窗口后，再次确认触发。
- `WIFI-3`：禁止在开机流程、异常恢复路径或普通 BLE 工作流中自动启动 WiFi。

## SoftAP 固定配置

- `WIFI-4`：SoftAP SSID 固定为 `BikePower`。
- `WIFI-5`：SoftAP 密码固定为 `12345678`。
- `WIFI-6`：SoftAP 信道固定为 `1`。
- `WIFI-7`：HTTP 服务器端口固定为 `80`。
- `WIFI-8`：热点参数统一从 `main/bikepower_config.h` 读取，禁止在 `wifi_portal.c` 内散落硬编码副本。

## BLE/WiFi 射频互斥

- `WIFI-9`：启动 WiFi 前必须先停止 BLE。
- `WIFI-10`：唯一允许执行 BLE -> WiFi 射频切换的入口是 `radio_ctrl.c`。
- `WIFI-11`：`wifi_portal.c` 不得自行决定 BLE 停止顺序，只能假定调用前 RF 已经切换完成。

## HTTP 服务与路由

- `WIFI-12`：配网门户使用 `esp_http_server`。
- `WIFI-13`：当前 HTTP 服务固定注册 11 个路由，新增或删除路由都必须同步更新规则、自测脚本和技术文档。
- 路由列表：
- `GET /`
- `POST /config`
- `GET /success`
- `GET /more`
- `POST /draft`
- `GET /ui/module`
- `GET /closing`
- `GET /time`
- `GET /renew`
- `GET /mem_stats`
- `GET /favicon.ico`
- `WIFI-14`：路由注册必须集中维护，避免分散在多个函数里造成行为漂移。

## 超时与恢复

- `WIFI-15`：配网模式 `180s` 无配置提交或有效续期后必须自动关闭并重启设备。
- `WIFI-16`：自动关闭后的恢复方式是 `esp_restart()`，通过重启回到 BLE 主工作模式。
- `WIFI-17`：保存成功后也走短延时重启，不做 WiFi -> BLE 的原地热切换。

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
