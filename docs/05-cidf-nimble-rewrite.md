# BikePower C / ESP-IDF + NimBLE 完全重写方案

> 本文是 `10-refactor-design.md` 第十一章「技术栈选型」中“转 ESP-IDF + NimBLE”路线的落地方案。
> 它面向“完全推倒重做、放弃 MicroPython、用 C + ESP-IDF + NimBLE 重写整个固件”的场景。
> 目标基线：首选 ESP-IDF v6.0.1 stable；若团队希望先沿用本仓库已有 ESP-IDF 5.x 工具链，则至少升级到 v5.5.4 bugfix 线。目标芯片 ESP32-C3（RISC-V 单核 160MHz）。
> 本文是设计与施工说明，不是当前实现的行为规格。落地前仍需按第九章五步迁移拆批验证。

> **当前实现校准（v3.1.8）**：本文保留 C 重写早期设计背景，但当前固件已经演进为普通配网默认启用 `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` 共存，BLE 保持在线，配置实时生效且退出配网不重启。独占 STA 与停 BLE 仅用于 OTA 升级，或共存初始化失败时由 `radio_ctrl` 兜底处理。当前硬规则以 `AGENTS.md`、`.trae/rules/wifi-ble-mutex.md` 和 `docs/04-technical-reference.md` 为准。

---

## 零、资料校准与版本决策

本文按 2026-06-12 可查官方资料校准：

| 主题 | 结论 | 对本方案的影响 |
|------|------|----------------|
| ESP-IDF 稳定版 | 官方 stable 文档当前指向 v6.0.1，生产用途应优先选择 stable 版本 | 新建 C 工程首选 v6.0.1，不再以 v5.4.1 作为目标基线 |
| ESP-IDF 5.x 备选 | v5.5.4 是 v5.5 bugfix 版本，并修复了 v5.5.3 中 ESP32/ESP32-C3/ESP32-S3 潜在 NimBLE host 断连问题 | 若保守留在 5.x，直接用 v5.5.4，不建议停在 v5.5.1 或 v5.5.3 |
| ESP-NimBLE | ESP-IDF 提供移植到 ESP32-C3 + FreeRTOS 的 NimBLE host，底层 controller 仍通过 VHCI 接口工作 | 方案继续采用 NimBLE host，避免 Bluedroid |
| WiFi/BLE 共存 | ESP-IDF 官方支持 WiFi/BLE RF coexistence，采用时间片仲裁；这不是“硬件绝对不能同时 init” | v3.1.8 当前普通配网默认采用 coex 共存，BLE 保持在线；独占 STA 仅用于 OTA 或共存失败兜底 |
| HTTP server | `esp_http_server` 是轻量 HTTP server，支持可配置任务栈、URI handler、chunk response；API 本身不保证线程安全 | Web 配网使用 `esp_http_server`，所有 HTTP 操作收口在 httpd task 或加锁 |
| NVS | NVS 是面向 flash 的 key-value 存储，key 长度有限，适合小配置 | C 版配置从 JSON 文件迁到 NVS，schema key 必须短且稳定 |

### 版本选择建议

| 选择 | 适用场景 | 建议 |
|------|----------|------|
| ESP-IDF v6.0.1 | 全新 C 工程、愿意接受 v6 迁移成本 | **推荐**，作为新工程默认线 |
| ESP-IDF v5.5.4 | 想沿用 5.x 生态、降低一次性迁移风险 | 可作为 POC 或第一版 v3.0.0 基线 |
| 当前本仓库 `esp-idf/` v5.5.1-dirty | 本地已有工具链 | 只适合参考，不建议作为量产基线 |

实施前必须在 `sdkconfig.defaults` 顶部写明实际锁定版本，并在 CI 中输出 `idf.py --version`。

## 一、为什么选 ESP-IDF + NimBLE

### 1.1 决策动机

MicroPython 版本的根本约束是内存：堆约 150KB，BLE 启动后仅剩约 105KB，Web 配网模式约 62KB。任何新功能都要先和内存搏斗。转 C / ESP-IDF + NimBLE 的核心收益：

| 收益 | 说明 |
|------|------|
| 内存可控 | 无 GC，无解释器堆；ESP32-C3 的 ~400KB SRAM 几乎全部可用，BLE 栈占用可精确裁剪 |
| GATT 完整可控 | NimBLE 直接暴露 `ble_gatt_svc_def`，CPS/CSC/FTMS 的每个描述符、CCCD、通知时机都可控 |
| 无抖动 | 无解释器 GC 停顿，1s 通知节拍由 FreeRTOS 定时器精确驱动 |
| 可测试 | 纯逻辑层（功率/心率/踏频换算）可用 host 工具链在 PC 上跑单元测试 |
| 功耗优化 | 可启用 Modem-sleep / Light-sleep，电池供电场景显著省电 |

### 1.2 成本与风险（必须先认清）

| 项 | 现实 |
|----|------|
| 开发成本 | 相比 MicroPython 约翻倍，BLE GATT 表手写、回调式异步、内存手动管理 |
| 调试成本 | 需熟悉 `idf.py monitor` + GDB stub + coredump 分析 |
| 迭代速度 | 每次改动需重新编译烧录（约 30-90s），不像 MicroPython 改文件即生效 |
| 射频策略 | ⚠️ ESP-IDF 支持 WiFi/BLE 共存，但共享 2.4GHz RF 会引入时间片竞争；BikePower v3.1.8 普通配网已采用 coex 共存并保持 BLE 在线，OTA 独占 STA 才停 BLE |

> ⚠️ 质疑（AC-更优方案）：如果团队没有 C 嵌入式经验，且当前 MicroPython 版本功能已满足，**不建议**为了“更优雅”而重写。本方案仅在“内存焦虑无法靠优化解决”或“要做产品级量产/功耗优化”时才立项。

---

## 二、目标架构

### 2.1 分层

```text
┌───────────────────────────────────────────────┐
│ app_main (主任务 + 事件分发)                    │
├───────────────────────────────────────────────┤
│ 状态机层  app_state.c   (BOOTING→BLE_ACTIVE→…) │
│ 射频层    radio_ctrl.c  (唯一 BLE/WiFi 开关)    │
├───────────────────────────────────────────────┤
│ 领域层 (纯 C，可 host 测试，不依赖硬件)         │
│   power_engine.c  ride_modes.c  hr_model.c     │
│   config_store.c  profile.c                    │
├───────────────────────────────────────────────┤
│ BLE 层   ble_bikepower.c (NimBLE GATT 服务)     │
│ WiFi 层  wifi_portal.c   (AP + HTTP server)     │
├───────────────────────────────────────────────┤
│ HAL/驱动 button.c  led.c  nvs_store.c  wdt.c    │
├───────────────────────────────────────────────┤
│ ESP-IDF: NimBLE / esp_wifi / esp_http_server / │
│          NVS / FreeRTOS / esp_timer / GPIO      │
└───────────────────────────────────────────────┘
```

### 2.2 关键设计原则

- 领域层（power_engine / ride_modes / hr_model / config_store）**不 include 任何 ESP-IDF 头文件**，只依赖标准 C，保证可在 PC 上单测。
- BLE/WiFi 启停只能经 `radio_ctrl`，任何模块禁止直接调 `esp_wifi_start()` 或 `ble_gap_adv_start()`。
- 模块间通过 FreeRTOS Queue / EventGroup 通信，不共享可变全局状态。
- 所有 BLE 通知缓冲区静态预分配，运行时零 `malloc`。

### 2.3 任务（FreeRTOS Task）规划

| 任务 | 优先级 | 栈 | 职责 |
|------|--------|----|------|
| `nimble_host` | 高（IDF 默认） | 4KB | NimBLE 协议栈宿主，IDF 自带 |
| `app_main_task` | 中 | 4KB | 状态机迁移、事件分发 |
| `ble_notify_timer` | esp_timer | — | 1s 周期触发 GATT 通知（用 esp_timer 而非独立任务） |
| `button_task` | 中 | 2KB | GPIO 中断 + 去抖 + 长按计时 |
| `led_task` | 低 | 1.5KB | 状态机驱动 LED 闪烁 |
| `httpd`（WiFi 模式） | 中 | 4KB | esp_http_server，仅配网时启动 |

---

## 三、工程结构与组件

### 3.1 目录骨架

```text
bikepower-c/
├── CMakeLists.txt
├── sdkconfig.defaults          # 默认裁剪配置（关键）
├── partitions.csv              # 分区表（含 NVS + factory）
├── main/
│   ├── CMakeLists.txt
│   ├── app_main.c              # 入口 app_main()
│   ├── app_state.c/.h          # 状态机
│   ├── radio_ctrl.c/.h         # BLE/WiFi 互斥控制器
│   ├── ble_bikepower.c/.h      # NimBLE GATT 五服务
│   ├── wifi_portal.c/.h        # AP + HTTP 配网
│   ├── button.c/.h  led.c/.h   # HAL
│   └── web_assets/             # 配网 HTML（嵌入 flash）
├── components/
│   ├── power_engine/           # 领域层：功率引擎
│   ├── ride_modes/             # 领域层：三种骑行模式
│   ├── hr_model/               # 领域层：心率动力学模型
│   └── config_store/           # 领域层：配置 schema + NVS 桥接
└── test/                       # host 端单元测试（Unity / CppUTest）
```

### 3.2 sdkconfig.defaults 关键裁剪

以下配置按 ESP-IDF 5.5.x 的 Kconfig 名称给出，v6.0.1 建项前必须用 `idf.py menuconfig` 或 `idf.py save-defconfig` 校验名称是否变化。不要把过期 Kconfig 名称直接提交进工程。

```ini
# 芯片
CONFIG_IDF_TARGET="esp32c3"
# BLE：使用 NimBLE，关闭 Bluedroid
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_MAX_BONDS=0
CONFIG_BT_NIMBLE_MAX_CCCDS=8
CONFIG_BT_NIMBLE_NVS_PERSIST=n
CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=64
CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=4096
# 日志：生产降到 WARN
CONFIG_LOG_DEFAULT_LEVEL_WARN=y
# 看门狗
CONFIG_ESP_TASK_WDT_EN=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=5
# 编译优化（减体积）
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
# WiFi：仅 AP，缩小缓冲池省内存
CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=8
# HTTP server：配网页只服务 1-2 个手机连接
CONFIG_HTTPD_MAX_REQ_HDR_LEN=512
CONFIG_HTTPD_MAX_URI_LEN=128
```

建议关闭或避免启用：

- BLE Mesh、GATT Caching、EATT、Extended Advertising、Periodic Advertising。
- WebSocket、HTTPS server、mDNS。
- WiFi STA 自动重连和扫描相关能力（本项目只做短时 SoftAP）。

### 3.3 partitions.csv

```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x6000
phy_init, data, phy,     0xf000,  0x1000
factory,  app,  factory, 0x10000, 0x1C0000
```

> 4MB Flash 充裕，单 factory 分区即可。若未来要做 OTA，再切 `ota_0/ota_1` 双分区（本方案默认不做 OTA，与现项目 v2.3.0 起移除 OTA 的决策一致）。

---

## 四、BLE 实现（NimBLE 五服务）

### 4.1 服务/特征映射（与现 MicroPython 版完全一致）

UUID 直接复用现 `config.py`，保证骑行 App（Zwift/TrainerRoad 等）无感切换。

| 服务 | UUID | 特征 | 属性 |
|------|------|------|------|
| Cycling Power | 0x1818 | 0x2A63 Measurement | READ + NOTIFY |
| Heart Rate | 0x180D | 0x2A37 Measurement | READ + NOTIFY |
| CSC | 0x1816 | 0x2A5B Measurement | READ + NOTIFY |
| FTMS | 0x1826 | 0x2ACC Feature(R) / 0x2AD2 IndoorBike(R+N) / 0x2AD9 ControlPoint(W+N) / 0x2ADA Status(R+N) | 混合 |
| DIS | 0x180A | 0x2A29 / 0x2A24 / 0x2A26 | READ |

### 4.2 GATT 表定义（NimBLE 风格示意）

```c
// 16-bit UUID 宏
#define UUID16(x) BLE_UUID16_DECLARE(x)

static const struct ble_gatt_svc_def gatt_svcs[] = {
    { // Cycling Power
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = UUID16(0x1818),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = UUID16(0x2A63),
              .access_cb = cps_access_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_cps_val_handle },
            { 0 }
        }
    },
    { // FTMS
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = UUID16(0x1826),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = UUID16(0x2ACC), .access_cb = ftms_feat_cb,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = UUID16(0x2AD2), .access_cb = ftms_bike_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_ftms_bike_handle },
            { .uuid = UUID16(0x2AD9), .access_cb = ftms_cp_cb,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_ftms_cp_handle },
            { .uuid = UUID16(0x2ADA), .access_cb = ftms_status_cb,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &g_ftms_status_handle },
            { 0 }
        }
    },
    // … HRS / CSC / DIS 同理
    { 0 }
};
```

### 4.3 数据打包（用结构体 + 显式小端）

C 版用静态缓冲区 + 显式小端写入（ESP32-C3 本身小端，可直接 memcpy，但为可移植仍建议显式）：

```c
// CPS Measurement: flags(2) + power(2) + crankRev(2) + crankTime(2)
static uint8_t cps_buf[8];
static void cps_pack(uint16_t power, uint16_t rev, uint16_t crank_t1024) {
    cps_buf[0] = 0x20; cps_buf[1] = 0x00;          // flags: crank data present
    cps_buf[2] = power & 0xFF;  cps_buf[3] = power >> 8;
    cps_buf[4] = rev & 0xFF;    cps_buf[5] = rev >> 8;
    cps_buf[6] = crank_t1024 & 0xFF; cps_buf[7] = crank_t1024 >> 8;
}
```

> 踏频换算公式不变：`Cadence = (Δrev × 60 × 1024) / Δt(1/1024s)`，与现实现位级兼容。

### 4.4 通知发送

```c
// 由 esp_timer 每 1000ms 触发
static void notify_tick(void *arg) {
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    snapshot_t s = power_engine_snapshot();   // 领域层只读快照
    cps_pack(s.power, s.rev, s.crank_t);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(cps_buf, sizeof(cps_buf));
    ble_gatts_notify_custom(g_conn_handle, g_cps_val_handle, om);
    // HRS / CSC / FTMS IndoorBike 同理
}
```

### 4.5 FTMS 控制点（ERG 模式）

写回调解析 OpCode，与现 MicroPython 实现的状态机一致：

| OpCode | 含义 | 处理 |
|--------|------|------|
| 0x00 | Request Control | 回 0x80,0x00,0x01（成功） |
| 0x05 | Set Target Power | 取 `value[1..2]` 小端为目标功率 → `power_engine_set_erg()` → 回成功 |
| 0x07 | Start/Resume | status=0x01，回成功 |
| 0x08 | Stop/Pause | status=0x02，回成功 |
| 0x01 | Reset | 清 ERG 目标，status=0x00，回成功 |
| 其他 | — | 回 0x80,op,0x02（不支持） |

```c
static int ftms_cp_cb(uint16_t conn, uint16_t attr,
                      struct ble_gatt_access_ctxt *ctxt, void *arg) {
    uint8_t buf[20]; uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len);
    uint8_t op = buf[0];
    if (op == 0x05 && len >= 3) {
        uint16_t target = buf[1] | (buf[2] << 8);
        power_engine_set_erg(target);
        ftms_send_response(0x80, 0x05, 0x01);
    }
    // … 其余 OpCode
    return 0;
}
```

### 4.6 连接参数与广播

```c
// 广播：含五服务 16-bit UUID + 设备名
struct ble_gap_adv_params adv = {
    .conn_mode = BLE_GAP_CONN_MODE_UND,
    .disc_mode = BLE_GAP_DISC_MODE_GEN,
    .itvl_min = 0x140,  // 200ms
    .itvl_max = 0x140,
};
// 连接参数协商：min 20ms / max 40ms / latency 0 / timeout 4s（与现版一致）
```

随机静态地址（`ble_hs_id_gen_rnd`）保留，重启换 MAC，客户端识别为新设备，沿用现有行为。

---

## 五、WiFi 配网（esp_wifi AP + esp_http_server）

### 5.1 与现版一致的契约

- SSID `BikePower` / 密码 `12345678` / IP `192.168.4.1` / 端口 80。
- 路由表与 `09-web-api-reference.md` 保持一致：`GET /`、`POST /config`、`POST /draft`、`GET /more`、`GET /ui/module`、`GET /time`、`GET /renew`、`GET /mem_stats`。
- 180s 自动超时关闭并恢复 BLE。

### 5.2 HTTP server 实现要点

用 `esp_http_server`（比手写 socket 省事且健壮）：

```c
httpd_uri_t home = { .uri="/", .method=HTTP_GET, .handler=h_home };
httpd_register_uri_handler(server, &home);
// POST /config 用 req->content_len 收齐 body 后解析
```

页面 HTML 用 `EMBED_FILES` 嵌入 flash，运行时不占 RAM 堆；动态值（功率/模式/剩余时间）用占位符替换或分片 `httpd_resp_send_chunk` 输出。

### 5.3 倒计时

`GET /time` 返回剩余秒数（由 esp_timer 维护的关机时间戳计算），前端 JS 每秒轮询，逻辑与现版一致。

---

## 六、领域层（可 host 测试的纯 C）

### 6.1 模块职责

| 模块 | 职责 | 关键函数 |
|------|------|----------|
| `power_engine` | 统一数据快照、调功、ERG 目标 | `snapshot()` `adjust(d)` `set_power(p)` `set_erg(p)` |
| `ride_modes` | steady/road/erg 三模式功率生成 | `compute(mode, ctx)` |
| `hr_model` | 心率一阶指数动力学 + HRV + 死区 | `hr_step(power, dt)` |
| `config_store` | schema 校验、默认值、版本迁移 | `load()` `save()` `validate()` |
| `profile` | 体重/FTP/阈值心率/配速机器人 | `clamp_all()` |

### 6.2 心率模型直接移植

现 `config.py` 的心率参数（τ_rise=30s / τ_fall=60s、HRV RSA/Mayer/drift、死区、FTP 区间映射）是纯数学，可 1:1 移植为 C。这是最该写单测的部分：

```c
// HR(t) = HR_old + alpha*(HR_target - HR_old), alpha = 1 - exp(-dt/tau)
int hr_step(hr_state_t *st, int power_w, float dt_s) { ... }
```

### 6.3 配置持久化：JSON 文件 → NVS

C 版建议放弃 JSON 文件，改用 ESP-IDF **NVS**（键值存储，原子、抗掉电）：

| 现版 | C 版 |
|------|------|
| `power_config.json` 明文 | NVS namespace `bikepower` |
| 手工 JSON 解析 | `nvs_get_i32/u16/str` |
| 版本字段 `cv=3` | NVS key `schema_ver`，迁移逻辑在 `config_store_migrate()` |

> ⚠️ 质疑（AC-跨模块影响）：改用 NVS 后，旧设备的 `power_config.json` 不会被读取。需在文档与发版说明中标注“首次刷 C 固件后配置重置为默认值”，并提供出厂默认值列表。

---

## 七、WiFi/BLE 射频策略（C 版策略）

⚠️ 需要修正一个常见误区：ESP32-C3 在 ESP-IDF 下支持 WiFi/BLE 共存，底层通过 coexistence 模块按优先级和时间片仲裁 RF。它不是“物理上绝对不能同时 init”。

v3.0.0 早期设计曾倾向“独占配网 + 可恢复软切换”，主要担心 SoftAP/http 流量影响 1s BLE 通知节拍、抬高 RAM 峰值、增加调试复杂度。v3.1.8 当前实现已经完成策略演进：

- 普通配网默认启用 `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` 共存，BLE 不停、不断开现有连接。
- 配网期间主循环继续生成快照并通过 BLE 通知，网页“保存设置”后配置实时生效。
- 用户点击“保存并退出配网”或 180s 超时后，由 `wifi_portal` 回调投递 `APP_EVT_WIFI_DONE`，`radio_ctrl` 关闭 HTTP/SoftAP 并回到 BLE 正常工作状态。
- OTA 下载进入独占 STA，或共存初始化失败兜底时，才由 `radio_ctrl` 停 BLE 后重试或重启恢复。

当前普通配网流程：

```text
enter_wifi_config():
  1. 确认 coex 配置启用，BLE 保持在线
  2. radio_ctrl_enter_wifi() → esp_wifi_init() → 配 AP → esp_wifi_start()
  3. 启动 httpd / DNS captive portal
  4. 网页保存配置 → power_engine setter / NVS，BLE 通知实时反映新值
配网结束 / 超时:
  5. APP_EVT_WIFI_DONE → radio_ctrl_exit_wifi()
  6. httpd_stop() → esp_wifi_stop() → esp_wifi_deinit()
  7. 回到 ST_BLE_ACTIVE，不重启设备
```

| 方案 | 优点 | 风险 | C 版可行性 |
|------|------|------|-----------|
| A 每次 esp_restart | 100% 可靠 | 体验差 | 高 |
| B 独占软切换不重启 | 体验好 | 控制器二次 init 偶发失败 | 历史备选 |
| C 独占软切换 + 兜底重启 | 兼顾 | 实现复杂 | v3.0.0 早期方案 |
| D WiFi/BLE 共存 | BLE 在线、配置实时生效、不重启 | 需要 coex 配置与真机验证 | **v3.1.8 当前默认** |

> 注意：当前规则已演进为 coex 默认路径。后续任何改动都不得绕过 `radio_ctrl` 直接启停 WiFi；只有 OTA 独占 STA 或共存初始化失败兜底时，才允许由 `radio_ctrl` 停 BLE。

---

## 八、状态机与按钮/LED

### 8.1 状态枚举（对齐 10 号文档第四章）

```c
typedef enum {
    ST_BOOTING, ST_BLE_ACTIVE, ST_WIFI_CONFIRM,
    ST_WIFI_CONFIG, ST_SAVING, ST_REBOOTING, ST_ERROR_RECOVERY
} app_state_t;
```

按钮/LED/WiFi/BLE 全部只响应状态迁移，迁移由 `app_state_transition(event)` 集中处理。

### 8.2 按钮（中断驱动，替代轮询）

```c
// GPIO9 下降沿中断 → 入队 → button_task 去抖 + 计时
gpio_set_intr_type(BTN_PIN, GPIO_INTR_ANYEDGE);
gpio_isr_handler_add(BTN_PIN, btn_isr, NULL);
```

时长阈值与现版一致：短按<500ms（-20W）、长按≥500ms 连续+20W（200ms 间隔）、≥2s 松开进确认窗口、3s 内二次确认进配网。

### 8.3 LED

`led_task` 按状态查表闪烁：广播慢闪(1s) / 已连接常亮 / 确认快闪(200ms) / WiFi 模式熄灭，与现版表现一致。

---

## 九、实施步骤（分阶段，每段可独立验证）

> 遵循 10 号文档第九章「不一次性推倒」原则，按里程碑推进，每段产出可烧录固件并真机验证。

### 阶段 0：环境与骨架（0.5 天）

1. 安装 ESP-IDF v6.0.1；保守路线可用 v5.5.4，但必须记录理由。执行 `. ./export.sh`。
2. `idf.py create-project bikepower-c`，配 `set-target esp32c3`。
3. 写 `sdkconfig.defaults`、`partitions.csv`，`idf.py build flash monitor` 跑通空工程。
   - 验收：串口打印 `app_main` 启动日志。

### 阶段 1：BLE 最小闭环（2-3 天）

4. 初始化 NimBLE（`nimble_port_init` + host task）。
5. 注册 CPS 单服务 + 广播。
6. esp_timer 每 1s 推固定功率（如 200W 写死）。
   - 验收：手机 nRF Connect / Zwift 搜到 `BikePower`，能连接，看到功率通知。

### 阶段 2：领域层 + 五服务（3-4 天）

7. 移植 `power_engine` / `ride_modes` / `hr_model` 为纯 C component，配 host 单测。
8. 补齐 HRS / CSC / FTMS / DIS 五服务。
9. 实现 FTMS 控制点（ERG）。
   - 验收：Zwift ERG 模式下发目标功率生效；心率/踏频曲线与 MicroPython 版一致。

### 阶段 3：按钮 + LED + 状态机（2 天）

10. GPIO 中断按钮 + LED 任务 + `app_state` 状态机。
    - 验收：短/中/长按、二次确认窗口、LED 各状态正确。

### 阶段 4：配置持久化（1-2 天）

11. `config_store` 接 NVS，实现 load/save/migrate。
    - 验收：调功后断电重启，配置恢复。

### 阶段 5：WiFi 配网（3-4 天）

12. `radio_ctrl` 实现 WiFi/BLE 共存配网入口，普通配网保持 BLE 在线；同时保留 OTA/失败兜底的独占路径。
13. esp_wifi AP + esp_http_server + 嵌入式 HTML 页面 + 路由。
14. 倒计时 / renew / 保存设置实时生效 / 保存并退出配网。
    - 验收：长按确认→配网页→保存设置实时生效→保存并退出配网→WiFi 关闭且 BLE 保持正常，全链路通。

### 阶段 6：稳定性与发版（2 天）

15. Task WDT、coredump、低内存监控、72h 老化测试。
16. 编译 release 固件，写 CHANGELOG + 测试报告，打 tag v3.0.0。

**总工期估算：约 14-20 人日**（不含学习曲线）。

---

## 十、注意事项（踩坑清单）

| 类别 | 注意事项 |
|------|----------|
| NimBLE 初始化顺序 | 必须 `nvs_flash_init()` → `nimble_port_init()` → 注册服务 → `nimble_port_freertos_init()` 启 host task，顺序错会 assert |
| GATT 句柄 | `val_handle` 必须在 `ble_gatts_count_cfg` + `ble_gatts_add_svcs` 后才有效，不能在注册前缓存 |
| 通知 mbuf | `ble_gatts_notify_custom` 会接管 mbuf，不能复用同一个 om 给多次 notify，每次重新 `ble_hs_mbuf_from_flat` |
| WiFi/BLE 内存 | 普通配网允许 WiFi/BLE coex 同时启用，但必须经 `radio_ctrl` 管理生命周期并监控堆水位；OTA 独占 STA 或共存失败兜底才停 BLE |
| WiFi/BLE 共存 | ESP-IDF 支持共存但不是免费午餐；BikePower v3.1.8 默认普通配网 coex，后续需持续验证 BLE 通知、HTTP 配网和堆水位 |
| 栈溢出 | NimBLE 回调里别放大数组；FreeRTOS 任务栈给够（host 任务 ≥4KB） |
| 中断上下文 | GPIO ISR 里只能入队，禁止调 BLE/日志 API；用 `xQueueSendFromISR` |
| esp_timer 回调 | 跑在 timer task 上下文，别做耗时操作；通知发送本身轻量可接受 |
| 看门狗 | 所有长循环任务必须 `esp_task_wdt_reset()`，否则 5s 触发重启 |
| 字节序 | ESP32-C3 小端，BLE GATT 也小端，可直接 memcpy 结构体；但跨平台单测需注意 |
| 配置兼容 | 旧 JSON 配置不迁移，首刷重置为默认（见 6.3）；发版说明必须标注 |
| 日志体积 | 生产用 `CONFIG_LOG_DEFAULT_LEVEL_WARN`，`ESP_LOGI` 字符串会进 flash，DEBUG 级别显著增大固件 |
| 版本号 | C 重写是不兼容大版本，按 SemVer 应为 **v3.0.0**，DIS 固件版本特征同步更新 |
| NVS key | NVS key 名长度有限，配置 key 使用短名如 `schema_ver`、`mode`、`ftp`、`thr_hr`，不要直接照搬长 JSON 字段 |

---

## 十一、与现 MicroPython 版的差异对照

| 维度 | MicroPython v2.4.2 | C / ESP-IDF + NimBLE v3.0.0 |
|------|--------------------|------------------------------|
| 语言/栈 | MicroPython + `bluetooth` | C + ESP-IDF + NimBLE |
| 并发 | 单循环 50ms 轮询 | FreeRTOS 多任务 + esp_timer |
| 按钮 | 轮询电平 | GPIO 中断 |
| 配置 | `power_config.json` 明文 | NVS 键值 |
| BLE 五服务 | ✅ | ✅（UUID/数据格式位级兼容） |
| FTMS/ERG | ✅ | ✅ |
| 可用内存 | BLE 后 ~105KB | 预计 ~250KB+（裁剪后） |
| 可测试性 | 弱（需自建 HAL） | 强（领域层 host 单测） |
| 迭代速度 | 快（改文件即生效） | 慢（需编译烧录） |
| OTA | 已移除 | 默认不做（可选双分区扩展） |
| 部署 | mpremote 上传 .mpy | `idf.py flash` 烧 firmware.bin |

> 客户端无感：因 UUID 与数据包格式完全一致，Zwift/TrainerRoad 等无需重新配对即可识别（仅随机地址导致显示为新设备，与现版行为相同）。

---

## 十二、验收标准

C 重写版至少满足：

- 五服务 BLE 广播 + 通知，Zwift/TrainerRoad 正常识别功率/心率/踏频。
- FTMS ERG 模式目标功率可被骑行软件下发并生效。
- 三骑行模式（steady/road/erg）输出与 MicroPython 版数值趋势一致。
- 长按二次确认 → BLE 保持在线 → 开 WiFi AP → 配网页 → 保存设置实时生效 → 保存并退出配网 → WiFi 关闭且 BLE 正常，全链路可靠。
- 普通配网 WiFi 启停必须经 `radio_ctrl` coex 路径；开机不自动进 WiFi；OTA 独占 STA 或共存失败兜底才停 BLE。
- 配置持久化（NVS）断电可恢复。
- 领域层 host 单测覆盖状态机、配置迁移、模式计算、心率模型。
- Task WDT 启用，72h 老化无 coredump、无内存泄漏。
- 固件版本 v3.0.0，DIS 同步，CHANGELOG + 测试报告齐全。

---

## 十三、落地建议（一句话总结）

先用阶段 0-1 在 1 周内做出“能被 Zwift 连上、能看到功率”的最小 BLE 闭环验证可行性与团队上手度，再决定是否投入完整 14-20 人日推进全功能。**不要一上来就全量重写**，按里程碑逐段烧录验证，每段都保持主链路可用。

---

## 参考资料

- ESP-IDF Versions：确认生产用途应选择当前 stable，并按版本文档开发。
  - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/versions.html
- ESP-IDF v6.0.1 Release：当前 stable bugfix 线，适合作为新 C 工程首选。
  - https://github.com/espressif/esp-idf/releases/tag/v6.0.1
- ESP-IDF v5.5.4 Release：v5.5 bugfix 线，修复 v5.5.3 中 ESP32/ESP32-C3/ESP32-S3 潜在 NimBLE host 断连问题。
  - https://github.com/espressif/esp-idf/releases/tag/v5.5.4
- ESP-NimBLE for ESP32-C3：NimBLE host 移植、FreeRTOS threading model、VHCI transport。
  - https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/bluetooth/nimble/index.html
  - https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/ble/overview.html
- ESP32-C3 RF Coexistence：WiFi/BLE 共存机制、时间片和动态优先级。
  - https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/coexist.html
- ESP HTTP Server：轻量 HTTP server、handler、chunk response、线程安全注意事项。
  - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html
- ESP-IDF NVS：key-value 存储、初始化、类型和 key 限制。
  - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/storage/nvs_flash.html
