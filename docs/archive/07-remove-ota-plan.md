# 移除 OTA 功能 + 网页端精简方案

> 状态：方案文档（待评审，未动代码）
> 目标分支：`feat/remove-ota`（基于 main）
> 约束：移除 OTA 在线检查/下载升级功能；**flash 仍维持 4MB**；网页端保留 BLE 训练核心功能，裁剪外围。
> 基线版本：v3.2.0

---

## 1. 背景与目标

当前固件包含完整的在线 OTA 链路（HTTPS 拉 `latest.json` + 流式下载 + 自检回滚），以及为 OTA 服务的家庭 WiFi 配置子系统。本方案评估：

1. 单独移除 OTA 功能，分区表从「双 OTA」改回「单 app」，flash 保持 4MB。
2. 分析网页端（配网门户）功能，区分核心与外围，给出精简建议，保持核心 BLE 训练参数配置能力。

**先出文档，评审通过后再建分支动代码。**

---

## 2. OTA 与家庭网络的强绑定关系（关键结论）

OTA 在线升级需要设备连家庭路由器（STA）拉取固件。因此 `/api/wifi/*`（扫描/选网/测试连接/保存凭证）这套**家庭网络配置子系统唯一的用途就是为 OTA 提供 STA 凭证**——配网门户自身走 SoftAP，不需要家庭网络。

> 结论：移除 OTA 时，家庭网络配置子系统应**一并移除**，否则会留下没有任何下游消费者的死功能。

---

## 3. OTA 移除清单（代码层）

### 3.1 可整体删除的文件

| 文件 | 说明 |
|------|------|
| `main/ota_downloader.c` / `.h` | HTTPS 检查版本 + 下载升级 + SNTP 时间同步 |
| `main/ota_update.c` / `.h` | 启动自检确认 / 回滚（PENDING_VERIFY） |
| `components/bikepower_domain/ota_version.c` / `.h` | 语义版本比较，仅 OTA 用 |
| `tests/ota_flow_test.py` | OTA 流程测试 |
| `tests/ota_local_mock.py` | 本地 OTA mock |
| `tests/ota_serial_monitor.py` | OTA 串口监控 |
| `docs/user/04-ota-user-guide.md` | OTA 用户手册 |

> ⚠️ **不可删**：`components/bikepower_domain/json_miniparse.c/.h`（被 `config_store.c`、`wifi_portal.c` 复用）；`radio_ctrl_sta_connect/disconnect`（家庭网络若同步移除则一并删，否则保留）。

### 3.2 需要修改的文件（删除片段）

| 文件 | 位置 | 处理 |
|------|------|------|
| `main/app_main.c` | `#include "ota_update.h"`；BLE 失败时 `ota_update_mark_invalid_and_rollback()`；启动 `ota_update_check_and_confirm()` | 删除 include 与两处调用，保留 `app_state_set_error()` |
| `main/wifi_portal.c` | OTA 头文件 include；`/api/ota/*` 三路由 handler + `ota_check_task` + `ota_upgrade_task` + 状态变量（约 250 行）；路由注册表 3 行 | 整段删除；保留 `json_miniparse` include |
| `main/wifi_portal.c` | `/api/wifi/*` 四路由 + 扫描/连接异步任务（约 220 行）（随家庭网络移除） | 整段删除 |
| `main/radio_ctrl.c` / `.h` | `radio_ctrl_enter_sta_exclusive()`（仅 OTA 用）；OTA 相关注释 | 删除该函数；家庭网络移除后 `radio_ctrl_sta_connect/disconnect` 也删 |
| `main/CMakeLists.txt` | 源文件 `ota_update.c`/`ota_downloader.c`；REQUIRES `app_update esp_https_ota esp_http_client mbedtls` | 删除两源文件；删除 OTA 专属组件依赖 |
| `main/bikepower_config.h` | OTA 常量块（`OTA_VERSION_URL` 等）+ `TASK_STACK_OTA_*` | 删除 |
| `main/web/index.html` | OTA 视图 `#view-ota`、导航项 `data-h="/ota"`、WiFi 页 OTA 文案；WiFi 视图整块 | 删除 |
| `main/web/app.js` | 路由 `/ota`、OTA 区块（renderOta/otaCheck/轮询，约 80 行）；WiFi 区块（约 115 行） | 删除 |
| `main/web/app.css` | OTA loading `@keyframes spin`（若他处无用） | 视引用情况删除 |
| `main/nvs_store.c` | 家庭 WiFi 凭证存取（若同步移除家庭网络） | 删除相关函数与注释 |
| `tests/check_config_consistency.py` | 第 5 节「OTA 配置一致性」检查段 | 删除 |
| `tests/check_portal_*.py` / `stage5_portal_test.py` / `mock_server.py` | OTA / WiFi 路由相关断言与 mock | 删除相关片段 |
| `test/test_domain.c` | `test_ota_version_compare()`（保留 `test_json_miniparse()`） | 删除该测试 |
| `docs/INDEX.md` / `docs/dev/01-technical-reference.md` / `docs/user/02-user-manual.md` | OTA 索引/章节/FAQ | 清理 |

### 3.3 保留项（避免误删）

- `OTA_TEST_AUTO_CONFIG`（CMake option + `app_state.c` 的 `#ifdef`）：名字带 OTA，但实为「自动触发配网」测试开关，**与在线 OTA 无关，保留**。
- `FIRMWARE_VERSION`：通用版本宏，保留。
- `json_miniparse`、`config_store`、coredump 分区与配置：保留。

---

## 4. 分区表调整（移除 OTA，维持 4MB）

### 4.1 当前布局（双 OTA，无 factory）

```
nvs       0x9000   0x6000
phy_init  0xf000   0x1000
ota_0     0x10000  0x1C0000   ← 删
ota_1     0x1D0000 0x1C0000   ← 删
otadata   0x390000 0x2000     ← 删
coredump  0x392000 0x10000
```

### 4.2 建议新布局（单 factory，flash 仍 4MB = 0x400000）

```
# Name,    Type, SubType, Offset,   Size
nvs,       data, nvs,      0x9000,   0x6000
phy_init,  data, phy,      0xf000,   0x1000
factory,   app,  factory,  0x10000,  0x3E0000   # 单 app，约 3.875MB
coredump,  data, coredump, 0x3F0000, 0x10000
```

- 删除 `ota_0/ota_1/otadata`，改单 `factory`。
- app 起始仍 `0x10000`，coredump 留 64KB，刚好填满 4MB。
- app 可用空间从 1.75MB 提升到约 3.875MB。

### 4.3 sdkconfig.defaults 同步

- 删除 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`（单 factory 无回滚分区，无意义）。
- `CONFIG_ESPTOOLPY_FLASHSIZE="4MB"` 保留。
- 分区表注释更新为单 factory 布局。

> ⚠️ **从双 OTA 改单 factory 必须全量重新烧录**（`esptool.py erase_flash` + 全量 flash），不能走 OTA 平滑迁移。

---

## 5. 可一并裁掉的依赖组件

OTA 移除后，以下组件无其它消费者，可从 CMake 依赖中裁掉以减小固件体积：

- `app_update`、`esp_https_ota`、`mbedtls`（证书 bundle）
- `esp_http_client`（确认家庭网络同步移除后无其它使用）
- SNTP（`esp_sntp`，全项目仅 OTA 用于 HTTPS 证书校验时间）

---

## 6. 网页端功能分析（核心 vs 外围）

门户为单页应用（SPA）：C 端 `GET /` 吐出内联 HTML，前端 `app.js` 首次拉 `/api/state` 后驱动 7 个 hash 路由视图。

### 6.1 核心功能（必须保留）

| 功能 | 说明 |
|------|------|
| 首页：目标功率 / 踏频 / 心率 输入 + 训练区间染色 | BLE 训练核心参数 |
| `/api/state` + `/api/config` + `/api/close-wifi` | 状态获取、保存配置、保存并关 WiFi 回 BLE |
| 更多：体重 / FTP / 阈值心率 / 配速机器人 pacer | 驱动功率与心率区间计算 |
| 底部导航 + 路由框架 | SPA 骨架 |

### 6.2 半核心（保留，可微调）

| 功能 | 说明 | 建议 |
|------|------|------|
| 续期倒计时 `/api/time` + `/api/renew` | 180s 无操作自动关 WiFi 回 BLE，保障射频安全释放 | **保留**；前端续期按钮可简化为自动续期 |
| 关闭页 / 成功页倒计时 | 收尾过渡 UI | 保留 |
| Captive Portal 探测端点（10 条）+ DNS 劫持 | 连上 SoftAP 后系统自动弹配网页 | **DNS 保留**；探测端点可从 10 条瘦身到 iOS/Android/Windows 主流 3-4 条，省 `max_uri_handlers` 配额 |

### 6.3 外围（建议移除/简化）

| 功能 | 代码量估计 | 建议 |
|------|-----------|------|
| OTA 检查/状态/升级 | C ~250 行 + JS ~80 行 + HTML ~16 行 + 下载器组件 | **移除**（本方案核心目标） |
| 家庭网络配置 `/api/wifi/*` | C ~220 行 + JS ~115 行 + HTML ~40 行 | **移除**（仅为 OTA 服务，无独立价值） |
| `/api/draft` 路由 | C ~30 行 + JS ~17 行 | **死代码，零风险删除**（前端从不调用） |
| 内存统计 `/api/mem_stats` | ~25 行 | 调试用，可移除或保留为隐藏入口 |
| 旧版死 CSS（seg/menu/panel/module/ver 等 6 组） | ~25 行 | **零风险删除**（无引用，v3.2.0 前遗留） |
| 已配家庭 WiFi 提示行 | 少量 | 随家庭网络移除 |

### 6.4 精简后的门户形态

移除 OTA + 家庭网络后，门户退化为**纯 BLE 参数配置页**：首页（功率/踏频/心率）+ 更多（体重/FTP/阈值心率/pacer）+ 关于（版本/设备名）。合计削减约 **门户层 770 行 C + 290 行前端**，并解耦 `ota_update`/`ota_downloader`/STA 相关 `radio_ctrl` 路径，显著降低复杂度与内存占用。

---

## 7. 实施步骤（评审通过后）

1. `git checkout -b feat/remove-ota`（基于 main；先处理或暂存当前 3 个未提交改动）。
2. 删除 OTA 源文件 + 领域层 ota_version + 测试脚本 + OTA 文档。
3. 改 `wifi_portal.c`：删 OTA 路由段 + 家庭网络路由段 + 注册表。
4. 改前端三件套：删 OTA 视图 / WiFi 视图 / draft / 死 CSS。
5. 改 `partitions.csv` 单 factory + `sdkconfig.defaults` 关回滚。
6. 改 `CMakeLists.txt` 删源文件与 OTA 专属组件依赖。
7. 清理常量、注释、文档索引、PROGRESS.md 过时描述。
8. 验证：
   - host 单测（`test/test_domain`）通过。
   - `idf.py build` 通过，确认 app 适配单 factory。
   - 门户静态检查脚本通过（删 OTA/WiFi 断言后）。
   - 真机：`erase_flash` + 全量烧录 → BLE 五服务 + 配网保存参数 + 回 BLE。
9. 分批提交（OTA 移除 / 分区表 / 网页端精简 / 文档分开 commit），确认后 push。

---

## 8. 风险与注意

1. **分区表变更需全量烧录**，老设备升级到此版本不能走 OTA。
2. **误删风险**：`json_miniparse`、`OTA_TEST_AUTO_CONFIG`、`config_store` 不能删。
3. **家庭网络与 OTA 绑定**：若产品后续仍想保留「连家庭 WiFi」的独立能力（非 OTA 用途），则家庭网络部分需单独评估是否保留。
4. **版本号**：建议新版本号区分（如 v4.0.0，因分区表不兼容），由用户确认。
