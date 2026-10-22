# WiFi 配网门户详解

> 本文档详细说明 WiFi SoftAP 配网门户的实现，包括 HTTP 路由、JSON API、Captive Portal、超时控制、前端内联方案和 BLE 扫描/设备模板功能。

## 目录

- [架构概述](#架构概述)
- [HTTP 路由表](#http-路由表)
- [超时与生命周期](#超时与生命周期)
- [JSON API 详解](#json-api-详解)
- [Captive Portal 机制](#captive-portal-机制)
- [配置应用流程](#配置应用流程)
- [BLE 扫描与设备模板](#ble-扫描与设备模板)
- [前端内联方案](#前端内联方案)
- [线程安全与资源管理](#线程安全与资源管理)
- [关键函数 API](#关键函数-api)

---

## 架构概述

WiFi 配网门户基于 `esp_http_server` 实现，在 SoftAP 模式下提供 HTTP 配置界面。v3.2.0 重构为前后端分离架构：

| 层 | 实现 | 说明 |
|----|------|------|
| 静态资源 | `web/index.html` + `app.css` + `app.js` | 编译时由 `scripts/inline_static.py` 内联为 `web_index_html.h` |
| HTTP 服务 | `esp_http_server` | 端口 80，最多 7 个并发 socket |
| JSON API | `/api/xxx` 路由 | 纯数据接口，前端 SPA 通过 fetch 渲染 |
| Captive Portal | DNS 劫持 + DHCP Option 114 + RFC 8910 | 自动弹出配网页面 |
| BLE 扫描 | `ble_scanner` 模块 | 配网期间可扫描周围 BLE 设备 |
| 设备模板 | `device_template_store` 模块 | 存储 5 个设备信息模板槽位 |

### 关键约束

- **WiFi 仅用于短时配网**：180s 无操作自动关闭，不常驻
- **BLE 保持在线**：coex 共存模式，配网期间 BLE 继续通知
- **配置实时生效**：通过 power_engine setter 立即更新 BLE 广播数据
- **原地关闭不重启**：配网结束后关闭 HTTP/SoftAP 回到 BLE 模式，不 `esp_restart()`
- **唯一 RF 入口**：WiFi 启停必须经 `radio_ctrl.c`，`wifi_portal.c` 不直接调用 `esp_wifi_*`

---

## HTTP 路由表

路由注册集中在 `start_httpd()` 函数中，共注册 **18 个业务路由** + **10 个 Captive Portal 探测端点** + 1 个 404 兜底处理。

### 静态资源

| 方法 | 路径 | Handler | 说明 |
|------|------|---------|------|
| GET | `/` | `h_home` | 返回内联完整 HTML（含 CSS/JS） |
| GET | `/favicon.ico` | `h_favicon` | 返回 204 No Content |
| GET | `/.well-known/captive-portal` | `h_captive_portal_api` | RFC 8910 Captive Portal API |

### 状态与计时

| 方法 | 路径 | Handler | 说明 |
|------|------|---------|------|
| GET | `/api/state` | `h_api_state` | 返回设备全状态 JSON |
| GET | `/api/time` | `h_api_time` | 返回剩余秒数（纯文本，高频轮询） |
| POST | `/api/renew` | `h_api_renew` | 续期配网窗口 |
| GET | `/api/mem_stats` | `h_api_mem_stats` | 返回内存统计 JSON |

### 配置保存

| 方法 | 路径 | Handler | 说明 |
|------|------|---------|------|
| POST | `/api/config` | `h_api_config` | 保存配置到 NVS（不关 WiFi） |
| POST | `/api/close-wifi` | `h_api_close_wifi` | 保存配置 + 5s 后关闭 WiFi |

### 设备信息模板

| 方法 | 路径 | Handler | 说明 |
|------|------|---------|------|
| GET | `/api/device-templates` | `h_api_device_templates` | 获取模板列表 |
| POST | `/api/device-templates/save` | `h_api_device_template_save` | 保存新模板 |
| POST | `/api/device-templates/delete` | `h_api_device_template_delete` | 删除模板 |
| POST | `/api/device-templates/apply` | `h_api_device_template_apply` | 应用模板（设设备名） |

### BLE 扫描（设备信息模板库）

| 方法 | 路径 | Handler | 说明 |
|------|------|---------|------|
| POST | `/api/ble-scan/start` | `h_api_ble_scan_start` | 启动 BLE 扫描 |
| POST | `/api/ble-scan/stop` | `h_api_ble_scan_stop` | 停止 BLE 扫描 |
| GET | `/api/ble-scan/results` | `h_api_ble_scan_results` | 获取扫描结果 |
| POST | `/api/ble-scan/read` | `h_api_ble_scan_read` | 读取指定设备 DIS 信息 |
| POST | `/api/ble-scan/analyze` | `h_api_ble_scan_analyze` | 连接分析设备（采样通知数据） |

### Captive Portal 探测端点（10 个）

各操作系统和浏览器用于检测网络连通性的固定路径，统一返回 303 重定向到首页触发自动弹窗：

| 路径 | 来源系统/浏览器 |
|------|---------------|
| `/generate_204` | Android/Chrome |
| `/gen_204` | Android/Chrome（备选） |
| `/hotspot-detect.html` | iOS/macOS |
| `/library/test/success.html` | iOS（旧版） |
| `/ncsi.txt` | Windows（NCSI） |
| `/connecttest.txt` | Windows（新版） |
| `/canonical.html` | Ubuntu/NetworkManager |
| `/success.txt` | Firefox |
| `/redirect` | 通用 |
| `/kindle-wifi/wifistub.html` | Kindle |

### 404 兜底

所有未匹配路径由 `h_captive_not_found` 处理，调用 `captive_portal_detect_handler()` 返回 303 重定向到 `/`。这确保任何未知的系统探测请求都能触发 Captive Portal 弹窗。

---

## 超时与生命周期

### 超时机制

| 常量 | 值 | 说明 |
|------|----|------|
| `WIFI_SHUTDOWN_MS` | 180000（180s） | 配网窗口默认时长 |
| `WIFI_PORTAL_CHECK_INTERVAL_US` | 1000000（1s） | 定时器检查间隔 |
| `WIFI_PORTAL_REBOOT_DELAY_SEC` | 5 | close-wifi 后收尾窗口 |

### 时间管理变量

```c
static volatile uint32_t s_deadline_sec;   // 截止时间（秒级单调时间，32位原子读写）
static volatile bool s_closing;             // 收尾中标志
static volatile bool s_done_called;         // done_cb 幂等标志
static esp_timer_handle_t s_shutdown_timer; // 周期检查定时器
```

### 生命周期状态机

```
wifi_portal_start(engine)
  ├→ arm_deadline() 设置 s_deadline_sec = now + 180s
  ├→ start_httpd() 注册所有路由
  └→ esp_timer_start_periodic(1s) 开始超时检查
       │
       ▼
  运行中（接受 API 请求）
       │
       ├→ /api/renew 或任意 API 请求 → arm_deadline() 续期
       │
       ├→ /api/close-wifi → s_closing=true, deadline = now + 5s
       │
       └→ 1s 定时器 shutdown_check_cb()
            └→ now >= deadline → portal_done()
                 └→ s_done_called=true, s_closing=true
                      └→ s_done_cb() → 投递 APP_EVT_WIFI_DONE
                           │
                           ▼
                   app_main → radio_ctrl_exit_wifi()
                        └→ wifi_portal_stop()
                             ├→ esp_timer_stop/delete
                             ├→ httpd_stop
                             └→ 清 s_closing/s_done_called
```

### 幂等性保证

- `s_done_called` 确保 `portal_done()` 只调用一次 `s_done_cb`，防止定时器和 close-wifi 双重触发投递多个 `APP_EVT_WIFI_DONE`
- `s_closing` 期间拒绝新的配置保存/模板修改/扫描请求
- `wifi_portal_stop()` 可重复调用（检查指针非 NULL 再释放）

---

## JSON API 详解

### GET /api/state

返回首页/更多/关于视图所需的全部数据，前端 init() 一次性拉取后缓存。

**响应字段**（`buf[3328]` 静态缓冲区）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `device_name` | string | 设备名（JSON 转义） |
| `firmware_version` | string | 固件版本号（如 `"4.1.1"`） |
| `firmware_name` | string | 完整固件名（含板型+Git SHA） |
| `board` | string | 板型名称（如 `"ESP32-C3"` / `"M5StampC3"`） |
| `stable_version` / `fix_version` | string | 版本信息 |
| `git_sha` | string | Git 提交哈希（编译注入） |
| `build_time` | string | 构建时间（编译注入） |
| `sport_profile` | string | `"cycling"` / `"running"` |
| `broadcast_mask` | int | 广播字段掩码 |
| `power` / `cadence` / `heartrate` | int | 当前功率/踏频/心率 |
| `mode` | string | `"steady"` / `"road"` / `"erg"` |
| `run_speed_x100` | int | 跑步速度（0.01 km/h 单位） |
| `run_pace_float_sec` | int | 跑步配速浮动（秒） |
| `run_cadence` / `run_cadence_float` | int | 步频/浮动（SPM） |
| `run_mode` | string | `"pacer"` / `"fixed"` |
| `run_pacer_type` | string | `"jog"` / `"aerobic"` / `"tempo"` / `"interval"` / `"custom"` |
| `run_pace_low_sec` / `run_pace_high_sec` | int | 自定义配速范围 |
| `run_custom_name` | string | 自定义跑步配速机器人名称 |
| `power_float` / `cadence_float` / `hr_float` | int | 浮动幅度 |
| `power_step` | int | 按钮调功步进（W） |
| `profile_weight` / `profile_ftp` / `profile_thr_hr` | int | 用户配置：体重/FTP/阈值心率 |
| `profile_ride_type` | string | 配速机器人 ID（coco/yumi/jacques/genie/constance/custom） |
| `custom_wkg_low` / `custom_wkg_high` | int | 自定义档 W/kg 范围（×10） |
| `custom_pacer_name` | string | 自定义档昵称 |
| `led_silent` | int | LED 静默模式 |
| `led_wait_mode/color` 等 | int | 5 个 LED 场景的模式/颜色 |
| `remaining_sec` | int | 配网剩余秒数 |
| `closing` | bool | 是否在收尾中 |
| `hr_locked` | bool | 心率是否锁定 |
| `pacers` | array | 骑行配速机器人列表（含自定义档） |
| `run_pacers` | array | 跑步配速机器人列表（含自定义档） |

### POST /api/config

请求体：JSON 对象，包含上述配置字段。未传字段保留旧值。

处理流程：
1. 检查 `s_closing`，收尾中返回错误
2. `arm_deadline()` 续期
3. `recv_body()` 读取请求体（最大 `HTTP_MAX_BODY_LEN`，超限返回 413）
4. **写临时副本**：`memcpy(&next, s_engine, sizeof(power_engine_t))`
5. `apply_json_fields(&next, body, true)` 解析并写入副本
6. `nvs_store_save(&next)` 持久化到 NVS
7. **成功**：`memcpy(s_engine, &next, ...)` 原子替换，BLE 实时生效
8. **失败**：丢弃副本，`s_engine` 保持原值，返回错误

响应：`{"ok":true}` 或 `{"ok":false,"error":"..."}`

### POST /api/close-wifi

与 `/api/config` 几乎相同，额外步骤：
- NVS 保存成功后设置 `s_closing = true`
- `s_deadline_sec = now + WIFI_PORTAL_REBOOT_DELAY_SEC`（5s 收尾窗口）
- 前端收到 `{"ok":true}` 后切到 `#/closing` 页面
- 5s 后定时器触发 `portal_done()` 关闭 WiFi

### GET /api/time

返回纯文本剩余秒数（如 `"173"`）。设计为纯文本而非 JSON，减少高频轮询的解析开销。

### POST /api/renew

调用 `arm_deadline()` 重置截止时间，返回新的剩余秒数（纯文本）。

### GET /api/mem_stats

返回 `{"free":<当前空闲堆>,"min_free":<历史最小空闲堆>}`，用于配网页诊断内存状态。

---

## Captive Portal 机制

### 三层弹窗机制

| 层 | 协议/端口 | 实现模块 | 说明 |
|----|----------|---------|------|
| DNS 劫持 | UDP 53 | `captive_portal.c` | 所有 DNS 查询返回 192.168.4.1 |
| DHCP Option 114 | DHCP | SoftAP 配置 | 通知客户端 Captive Portal URL |
| HTTP 探测端点 | TCP 80 | `wifi_portal.c` | 10 个已知路径 + 404 兜底返回 303 |
| RFC 8910 API | TCP 80 | `h_captive_portal_api` | `/.well-known/captive-portal` 返回 JSON |

### captive_portal_detect_handler 行为

所有探测端点和 404 兜底统一调用此 handler，返回：

```http
HTTP/1.1 303 See Other
Location: /
Cache-Control: no-store, no-cache, must-revalidate, max-age=0
Connection: close

```

使用 303（而非 302）+ 短 body 是为了兼容 iOS CNA（Captive Network Assistant）对空 302 的处理差异。

### RFC 8910 Captive Portal API

`GET /.well-known/captive-portal` 返回：

```json
{"captive":true,"user-portal-url":"http://192.168.4.1/"}
```

Content-Type 为 `application/captive+json`，这是 RFC 8910 定义的标准 MIME 类型，支持现代 OS（iOS 14+、Android 11+、Windows 11）无需硬编码探测路径即可发现 Captive Portal。

---

## 配置应用流程

### apply_json_fields() 详解

`apply_json_fields()` 是配置写入的核心，被 `/api/config` 和 `/api/close-wifi` 共用。它解析 JSON 字段并调用对应的 power_engine setter。

**字段解析顺序**：

```
apply_json_fields(engine, body, allow_sport_switch)
  ├→ mode（ride_mode_t: steady/road/erg）
  ├→ device_name
  ├→ sport_profile（需 allow_sport_switch=true）
  ├→ broadcast_mask
  ├→ custom_pacer_name
  ├→ power（仅 STEADY 生效）
  ├→ cadence（仅 STEADY 生效）
  ├→ power_float / cadence_float / hr_float（任意模式接受）
  ├→ power_step
  ├→ run_speed_x100 / run_pace_float_sec
  ├→ run_cadence / run_cadence_float
  ├→ run_custom_name
  ├→ run_pace_low_sec / run_pace_high_sec
  ├→ run_pacer_type / run_mode
  ├→ hr_lock + heartrate（锁定/解锁心率）
  ├→ profile_weight / profile_ftp / profile_thr_hr
  ├→ custom_wkg_low / custom_wkg_high（先于 ride_type 设置）
  ├→ profile_ride_type
  ├→ led_silent
  └→ led_wait/conn/wifi/confirm/error 的 mode + color（5 个场景 × 2 字段）
```

### 临时副本 + 原子替换的安全设计

配置写入采用"写临时副本→NVS 持久化→原子替换"模式：

```c
power_engine_t next;
memcpy(&next, s_engine, sizeof(power_engine_t));  // 克隆当前状态
apply_json_fields(&next, body, true);              // 写入副本
if (nvs_store_save(&next)) {                       // 持久化
    memcpy(s_engine, &next, sizeof(power_engine_t)); // 原子替换
    power_engine_clear_dirty(s_engine);
    // BLE 实时生效（主循环下一次 50ms tick 读取新值）
}
// NVS 失败则 next 被丢弃，s_engine 完全不变
```

这确保了：
1. NVS 写入失败时，运行时值不会处于中间状态
2. BLE 通知不会读到半更新的数据
3. 配置变更要么完全生效，要么完全不生效

### 实时生效路径

配置保存后，BLE 通知数据的更新路径：

```
/api/config → memcpy(s_engine, &next)
  → 主循环 50ms tick: power_engine_snapshot() 读取 s_engine 新值
  → ble_bikepower_set_data() 更新 g_power_w 等 volatile 变量
  → BLE 1s notify_tick: pack_all() 读取新值打包发送
```

设备名变更额外调用 `ble_bikepower_refresh_device_name()`：
- 已连接时只更新 GAP Device Name 服务，不断开当前连接
- 未连接时重启广播使用新名

运动配置文件切换设置 `s_ble_restart_requested = true`，主循环检测到后执行 BLE 重启以重新注册 GATT 服务表。

LED 配置变更调用 `app_state_refresh_led()` 立即更新 LED 显示。

---

## BLE 扫描与设备模板

配网期间可作为 BLE central 扫描周围设备，读取设备信息（DIS），采样通知数据做信号分析，结果可保存为模板用于一键仿制设备名。

### 扫描 API

| API | 功能 | 关键参数 |
|-----|------|---------|
| POST `/api/ble-scan/start` | 启动 BLE 扫描（默认 `BLE_SCAN_DEFAULT_MS`） | - |
| POST `/api/ble-scan/stop` | 停止扫描 | - |
| GET `/api/ble-scan/results` | 获取扫描结果列表 | 返回 `{"scanning":bool,"devices":[...]}` |
| POST `/api/ble-scan/read` | 连接指定设备读取 DIS 信息 | addr, addr_type |
| POST `/api/ble-scan/analyze` | 连接设备采样通知数据做分析 | addr, addr_type（采样 `DEVICE_ANALYSIS_SAMPLE_MS`） |

### 扫描结果过滤

根据 `BLE-3a` 规则：扫描结果必须同时满足"有名称"和"服务命中 CPS/CSC/HRS/FTMS/RSC 任意一种"才呈现给用户，仅 DIS/BAS 或未知服务的广播不进入可选结果。

### 设备模板

最多保存 `DEVICE_TEMPLATE_MAX`（5）个设备模板，存储在 NVS 中。

| API | 功能 |
|-----|------|
| GET `/api/device-templates` | 获取所有模板 |
| POST `/api/device-templates/save` | 保存新模板（JSON: alias/adv_name/device_name/manufacturer/model/firmware/hardware/software/addr/addr_type/last_rssi/service_flags） |
| POST `/api/device-templates/delete` | 删除模板（JSON: `{"id":N}`） |
| POST `/api/device-templates/apply` | 应用模板，将模板的 device_name 设为当前设备名（JSON: `{"id":N}`） |

模板自动填充逻辑：
- alias 为空时取 device_name 或 adv_name
- device_name 为空但 adv_name 非空时，用 adv_name 填充 device_name

---

## 前端内联方案

### 为什么内联

iOS Captive Portal WebView（CNA）不加载外部 `.css`/`.js` 资源，因此所有 CSS 和 JS 必须内联到单个 HTML 文件中。

### 编译时内联流程

```
构建时（idf.py build）:
  web/index.html
    ↓ 包含 <!-- INLINE_CSS --> 占位符
  scripts/inline_static.py
    ├→ 读取 web/app.css，替换 <!-- INLINE_CSS --> 为 <style>...</style>
    ├→ 读取 web/app.js，替换 <!-- INLINE_JS --> 为 <script>...</script>
    └→ 生成 main/web_index_html.h
         ├→ const char index_html[] = "...";
         └→ const unsigned int index_html_len = ...;

C 代码:
  #include "web_index_html.h"
  h_home(): httpd_resp_send(req, index_html, index_html_len);
```

这通过 CMake `add_custom_command` 实现，修改前端文件后无需手动生成头文件，构建系统自动重新内联。

### 前端 SPA 架构

- 单页面应用，通过 `location.hash` 切换视图（首页/更多/关于/模板/扫描）
- 通过 `XMLHttpRequest`（fetch）调用 `/api/xxx` JSON 接口
- 所有状态缓存在前端 JS 变量中，`/api/time` 做高频轮询驱动倒计时
- 保存配置后不刷新页面，就地更新状态

### 响应头统一设置

所有 API 响应和首页都设置：
```http
Cache-Control: no-store, no-cache, must-revalidate, max-age=0
Pragma: no-cache
Expires: 0
Connection: close
```

防止浏览器缓存导致回退显示旧配置页面。

---

## 线程安全与资源管理

### 并发模型

| 上下文 | 访问的共享资源 |
|--------|--------------|
| httpd 任务（多个 worker） | `s_engine`（读/写副本替换）、`s_deadline_sec`、`s_closing` |
| esp_timer 回调（shutdown_check_cb） | `s_deadline_sec`、`s_done_called`、`s_closing` |
| app_main 任务 | 调用 `wifi_portal_start/stop`、读取 `s_ble_restart_requested` |

### 原子性保证

- `s_deadline_sec` 使用 `uint32_t`（C3 32 位核上单字读写原子），避免 64 位撕裂
- `s_closing`、`s_done_called` 使用 `volatile bool`
- `s_engine` 的替换用 `memcpy` 整结构体拷贝（在 httpd worker 上下文中）
- NVS 保存失败时不更新 `s_engine`，保证一致性

### 资源释放

所有错误路径都正确释放资源：

| 资源 | 分配 | 释放 |
|------|------|------|
| HTTP 请求体 | `recv_body()` malloc | handler 中 free（包括错误路径） |
| httpd 句柄 | `httpd_start()` | `wifi_portal_stop()` 中 `httpd_stop()` |
| shutdown 定时器 | `esp_timer_create()` | `wifi_portal_stop()` 中 stop + delete |
| JSON 响应缓冲区 | 静态 `buf[]` | 无需释放（栈/静态分配） |

### 请求体大小限制

- `HTTP_MAX_BODY_LEN`：请求体最大字节数，超限返回 413 Payload Too Large
- 接收超时重试：`HTTP_RECV_TIMEOUT_RETRIES` 次超时后放弃，防止慢速连接阻塞
- `recv_body()` 分配 `total + 1` 字节（末尾 `\0`），用于字符串解析

### JSON 转义

`json_escape()` 函数对输出到 JSON 响应中的用户可控字符串（设备名、自定义名称等）做转义：
- `"` → `\"`
- `\` → `\\`
- 控制字符（< 0x20）→ `\u00XX`

防止 XSS 或 JSON 格式破坏。

---

## 关键函数 API

### 生命周期

| 函数 | 说明 |
|------|------|
| `wifi_portal_start(engine)` | 启动 HTTP 服务、定时器、设置 done 回调；AP 需由 radio_ctrl 预先启动 |
| `wifi_portal_stop()` | 停止定时器和 HTTP 服务，清理状态标志 |
| `wifi_portal_set_done_callback(cb)` | 设置配网结束回调（调用方为 app_main，投递 APP_EVT_WIFI_DONE） |

### 状态查询

| 函数 | 说明 |
|------|------|
| `wifi_portal_consume_ble_restart_requested()` | 读取并清除 BLE 重启请求标志（主循环消费） |
| `wifi_portal_ble_restart_requested()` | 只读查询 BLE 重启请求标志 |

### 内部工具函数

| 函数 | 说明 |
|------|------|
| `recv_body(req, out)` | 读取完整 HTTP 请求体，返回长度或错误码（-1=失败, -2=超限） |
| `set_no_store_headers(req)` | 统一设置禁止缓存响应头 |
| `json_escape(in, out, out_sz)` | JSON 字符串转义 |
| `json_has_key(json, key)` | 检查 JSON 是否包含指定 key（区分"未传"和"=0"） |
| `json_get_int_def(json, key, def)` | 取 JSON 整数，未找到返回默认值 |
| `send_json(req, body)` | 统一发送 JSON 响应（Content-Type + close + no-store） |
| `send_text(req, body)` | 统一发送纯文本响应 |
| `arm_deadline()` | 重置截止时间（当前时间 + 180s） |
| `remaining_seconds()` | 返回剩余秒数（0 表示已到期） |
| `portal_done()` | 触发配网结束（幂等），调用 s_done_cb |
| `apply_json_fields(e, body, allow_sport)` | 解析 JSON 字段写入引擎副本 |

### HTTP 服务配置

| 参数 | 值 | 说明 |
|------|----|------|
| server_port | `WIFI_PORT`（80） | HTTP 端口 |
| stack_size | `TASK_STACK_HTTPD`（12288B） | httpd 任务栈 |
| max_open_sockets | 7 | 最大并发连接 |
| max_uri_handlers | `HTTP_MAX_URI_HANDLERS` | 最大路由数 |
| lru_purge_enable | true | 启用 LRU 连接清理 |
| keep_alive_enable | false | 禁用 keep-alive，配网场景短连接即可 |
