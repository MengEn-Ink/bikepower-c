# 构建与测试指南

> 本文档详细说明 BikePower C 的构建系统、编译选项、固件烧录、测试体系和发布流程。

## 目录

- [开发环境要求](#开发环境要求)
- [快速开始](#快速开始)
- [构建系统详解](#构建系统详解)
- [构建选项](#构建选项)
- [固件烧录](#固件烧录)
- [测试体系](#测试体系)
- [发布构建](#发布构建)
- [GitHub Pages 网页刷写](#github-pages-网页刷写)
- [产物校验](#产物校验)
- [常用命令速查](#常用命令速查)

---

## 开发环境要求

| 组件 | 版本要求 | 说明 |
|------|---------|------|
| ESP-IDF | v5.5.4 | 本地工具链安装，非 Docker |
| 目标芯片 | ESP32-C3 | RISC-V 单核 160MHz |
| Python | 3.8+ | 用于构建脚本和测试 |
| CMake | 3.16+ | ESP-IDF 构建系统要求 |
| Ninja / Make | - | ESP-IDF 后端构建工具 |
| clang (可选) | - | 用于 host 单元测试编译 |
| zip (可选) | - | 用于 `build_all.sh` 打包 release |

### ESP-IDF 安装

默认安装路径：`$HOME/esp/esp-idf-v5.5.4/`

如果安装在其他位置，设置环境变量 `IDF_PATH` 指向 ESP-IDF 根目录。

---

## 快速开始

### 1. 首次编译烧录

```bash
# 激活 ESP-IDF 环境
source ~/esp/esp-idf-v5.5.4/export.sh

# 编译
idf.py build

# 烧录并监视串口
idf.py -p /dev/ttyACM0 flash monitor
```

### 2. 一键烧录（推荐日常开发）

```bash
# 自动探测 IDF 路径和串口，编译+烧录+抓 15s 启动日志
./scripts/flash_latest.sh

# 指定板型（M5Stamp C3）
./scripts/flash_latest.sh --board m5stamp

# 烧录后进入 monitor
./scripts/flash_latest.sh --monitor

# 首次烧录（擦除整片 flash，切换分区表时需要）
./scripts/flash_latest.sh --erase
```

### 3. 运行测试

```bash
# Host 单元测试（领域层）
make -C test && ./test/test_domain

# 静态门禁检查
python3 tests/check_config_consistency.py
python3 tests/check_portal_js.py
python3 tests/check_portal_structure.py
python3 tests/check_nvs_schema.py
python3 tests/check_tasks_wdt.py
python3 tests/check_board_desc.py

# Portal mock 测试
python3 tests/test_portal_config_save.py
```

---

## 构建系统详解

### 顶层 CMakeLists.txt

[CMakeLists.txt](../../CMakeLists.txt) 定义项目级配置：

```cmake
cmake_minimum_required(VERSION 3.16)

# 从 bikepower_config.h 提取 FIRMWARE_VERSION 作为 PROJECT_VER
file(STRINGS "main/bikepower_config.h" _fv_line REGEX "^#define FIRMWARE_VERSION")
string(REGEX REPLACE "^[^\"]*\"([^\"]+)\".*" "\\1" PROJECT_VER "${_fv_line}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bikepower-c)
```

**关键点**：
- 版本号从 `main/bikepower_config.h` 中提取，不依赖 `git describe`，确保 esp_app_desc.version 与宏一致
- 这避免了 OTA 防降级误判（虽然 OTA 已移除，但 DIS 版本字段仍依赖此）

### 组件结构

构建使用 ESP-IDF 组件模型：

| 组件 | 路径 | 依赖 |
|------|------|------|
| main | `main/` | ESP-IDF (NimBLE, WiFi, HTTP, NVS, esp_timer) + bikepower_domain |
| bikepower_domain | `components/bikepower_domain/` | 无 ESP-IDF 依赖（纯 C） |

### 编译时资源处理

前端静态文件通过 CMake `add_custom_command` 在编译时自动内联：

```
web/index.html (含 <!-- INLINE_CSS --> 和 <!-- INLINE_JS --> 占位符)
    ↓ scripts/inline_static.py
web/app.css → <style>...</style> 替换占位符
web/app.js  → <script>...</script> 替换占位符
    ↓
main/web_index_html.h
    ├─ const char index_html[] = "...";
    └─ const unsigned int index_html_len = ...;
    ↓
wifi_portal.c: #include "web_index_html.h"
    └─ h_home(): httpd_resp_send(req, index_html, index_html_len)
```

修改 `web/` 下文件后无需手动操作，`idf.py build` 自动检测并重新生成头文件。

### 编译注入变量

以下变量通过 `main/CMakeLists.txt` 在编译时注入：

| 宏 | 来源 | 说明 |
|----|------|------|
| `BIKEPOWER_GIT_SHA` | `git rev-parse --short HEAD` | Git 提交哈希 |
| `BIKEPOWER_BUILD_TIME` | `date` 命令 | 构建时间字符串 |
| `PORTAL_TEST_AUTO_CONFIG` | CMake 选项（默认 OFF） | 配网测试自动触发固件 |

---

## 构建选项

### 板型选择

通过 Kconfig 选项选择目标板型：

| Kconfig | 板型 | LED | 按钮 |
|---------|------|-----|------|
| `CONFIG_BOARD_GENERIC_C3=y` | 通用 ESP32-C3 | GPIO12（普通 LED） | GPIO9 |
| `CONFIG_BOARD_M5STAMP_C3=y` | M5Stamp C3 | GPIO2（SK6812 RGB） | GPIO3 |

**默认**：`sdkconfig.defaults` 中设置通用板。

通过 `flash_latest.sh --board m5stamp` 或 `build_all.sh` 自动处理板型切换。

### 分区表

分区表在 [partitions.csv](../../partitions.csv) 定义，为单 factory 布局（无 OTA 双分区）：

| 分区 | 地址 | 大小 | 说明 |
|------|------|------|------|
| bootloader | 0x0 | - | 由 ESP-IDF 生成 |
| partition-table | 0x8000 | - | 分区表本身 |
| factory | 0x10000 | ~3.6MB | 应用固件 |
| coredump | - | 64KB | 核心转储 |
| nvs | - | 24KB | 配置持久化 |

> **重要**：v4.0.0 起已移除 OTA，不再有 ota_0/ota_1 分区。从旧版本升级时必须 `erase-flash`。

### sdkconfig.defaults

[sdkconfig.defaults](../../sdkconfig.defaults) 包含默认配置：

- `CONFIG_ESP_COEX_SW_COEXIST_ENABLE=y`：WiFi/BLE 共存启用
- `CONFIG_BT_NIMBLE_ENABLED=y`：使用 NimBLE host（非 Bluedroid）
- NimBLE 连接数、MTU、广播间隔等从 `bikepower_config.h` 读取
- 分区表、Flash 大小（4MB）、Flash 模式（dio 80MHz）

---

## 固件烧录

### idf.py flash（标准方式）

```bash
idf.py -p <port> flash
```

烧录三个独立 bin 文件到各自地址：
- `0x0` → bootloader.bin
- `0x8000` → partition-table.bin
- `0x10000` → bikepower-c.bin（应用固件）

### flash_latest.sh（开发推荐）

[flash_latest.sh](../../scripts/flash_latest.sh) 提供一键式开发烧录：

| 选项 | 说明 |
|------|------|
| `-p, --port PORT` | 指定串口，默认自动探测 Espressif USB 设备 |
| `--board generic\|m5stamp` | 选择板型（默认 generic） |
| `--no-checks` | 跳过快速静态门禁 |
| `--no-build` | 跳过编译，仅烧录已有产物 |
| `--erase` | 烧录前擦除整片 flash（首次切换分区表时必用） |
| `--monitor` | 烧录后进入 `idf.py monitor` |
| `--capture-seconds N` | 烧录后抓 N 秒启动日志（默认 15s） |
| `--portal-test-auto-config` | 编译配网自动触发测试固件 |
| `--keep-daemon` | 不停止占用串口的 serial_daemon.py |

**自动功能**：
- 自动探测 ESP-IDF 路径（检查多个候选位置）
- 自动探测串口（pyserial 按优先级排序：303A:1001 > ESP/eppressif > ttyACM/USB > cu.usbmodem）
- 自动停止占用同一串口的 `serial_daemon.py` 进程
- 烧录前运行快速静态门禁
- 备份/恢复 sdkconfig，自动处理板型切换
- 烧录后自动抓取启动日志保存到 `tests/logs/`

### merge_bin（发布产物）

发布产物通过 `esptool.py merge_bin` 合并为单文件：

```bash
python3 esptool.py --chip esp32c3 merge_bin \
    -o bikepower-c-generic-c3-v4.1.1-stable-fix-gXXXXXXX.bin \
    --flash_mode dio --flash_freq 80m --flash_size 4MB \
    0x0 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/bikepower-c.bin
```

单文件 bin 可通过网页端 ESP Web Tools 一次烧录，对用户更友好。这是发布给终端用户的唯一分发格式（禁止分发分散 bin）。

---

## 测试体系

BikePower C 采用多层测试策略：

| 测试层级 | 工具/命令 | 覆盖范围 | 依赖硬件 |
|---------|----------|---------|---------|
| Host 单元测试 | `make -C test && ./test/test_domain` | 领域层算法（功率/心率/配速/配置/按钮逻辑/LED 模式） | 无 |
| 静态门禁 | `tests/check_*.py` | 代码结构、常量一致性、schema 完整性 | 无 |
| Mock 测试 | `tests/test_portal_config_save.py`、`test_core_flow_endurance.py` | Portal JSON 解析、配置流（mock server） | 无 |
| BLE 自测 | `tests/ble_selftest.py` | 五服务发现、通知格式、FTMS 控制点 | 需要设备 |
| 连接状态测试 | `tests/stage3_conn_test.py` | 连接事件→状态机→LED 联动 | 需要设备 |
| 门户自测 | `tests/stage5_portal_test.py` | HTTP 路由、Captive Portal、API 契约 | 需要设备+WiFi |
| 真机验收 | `tests/test_device_acceptance.py` | 端到端功能验收，生成 evidence JSON | 需要设备+人工 |
| 压力测试 | `tests/run_stress.sh` | 长时间运行稳定性 | 需要设备 |

### Host 单元测试

[test/Makefile](../../test/Makefile) 使用 clang 直接编译领域层（纯 C，无 ESP-IDF 依赖）：

```makefile
CC ?= clang
CFLAGS := -std=c11 -Wall -Wextra -I$(DOMAIN)/include
SRCS := test_domain.c \
        $(DOMAIN)/profile.c \
        $(DOMAIN)/domain_rng.c \
        $(DOMAIN)/hr_model.c \
        $(DOMAIN)/power_engine.c \
        $(DOMAIN)/running_engine.c \
        $(DOMAIN)/config_store.c \
        $(DOMAIN)/device_template.c \
        $(DOMAIN)/device_analysis.c \
        $(DOMAIN)/json_miniparse.c \
        $(DOMAIN)/button_logic.c \
        $(DOMAIN)/led_pattern.c \
        $(DOMAIN)/button_debounce.c
```

运行：
```bash
make -C test && ./test/test_domain
```

### 静态门禁脚本

| 脚本 | 检查内容 |
|------|---------|
| `check_config_consistency.py` | `bikepower_config.h` 与 `domain_config.h` 重叠常量一致性、表单 min/max 与代码一致 |
| `check_portal_js.py` | `node --check` 验证 `app.js` 语法 |
| `check_portal_structure.py` | index.html 结构完整性、后端路由与前端 API 调用一致、mock_server handler 同步 |
| `check_nvs_schema.py` | NVS key/schema 静态一致性，迁移路径完整 |
| `check_tasks_wdt.py` | 任务栈大小、WDT 订阅、队列深度配置完整 |
| `check_board_desc.py` | 板型描述表（引脚/LED/按钮）一致性 |
| `check_ble_advertising_fields.py` | BLE 广播字段大小不超 31 字节 |

### BLE 自测脚本

[ble_selftest.py](../../tests/ble_selftest.py) 使用 `bleak` 库连接设备验证：

1. 扫描并连接设备
2. 发现 GATT 服务，确认 CPS/HRS/CSC/FTMS/DIS（或 RSC/HRS/FTMS/DIS）五/四服务存在
3. 读取 DIS 特征值，验证厂商/型号/固件版本
4. 订阅通知，验证 CPS/CSC/HRS/FTMS 通知数据格式
5. FTMS Control Point 写 RequestControl/SetTargetPower/Start/Stop/Reset，验证响应

运行：
```bash
python3 tests/ble_selftest.py
```

### 门户自测脚本

[stage5_portal_test.py](../../tests/stage5_portal_test.py) 在设备进入配网模式后验证：

1. 连接 BikePower 热点
2. 访问首页、验证内联 HTML
3. 测试 `/api/state`、`/api/time`、`/api/renew`、`/api/mem_stats`
4. POST 配置并验证保存和实时生效
5. 测试 Captive Portal 探测端点重定向
6. 测试 RFC 8910 API

运行：
```bash
# 先让设备进入配网模式（长按→确认）
python3 tests/stage5_portal_test.py
```

### Mock Server（前端开发）

[mock_server.py](../../tests/mock_server.py) 在 PC 上启动 mock HTTP 服务器，模拟设备 `/api/xxx` 接口，前端开发无需烧录设备：

```bash
python3 tests/mock_server.py
# 然后在浏览器访问 http://localhost:8080/
```

---

## 发布构建

### build_all.sh（Release 构建）

[build_all.sh](../../scripts/build_all.sh) 执行完整的 release 构建流程：

```
./scripts/build_all.sh [板型...] [选项]
  板型: generic / m5stamp（默认两种都编）
  选项: --dry-run 预览不编译
        -h, --help  帮助
```

### 构建流程

```
build_all.sh 执行顺序:
  1. 提取版本元信息（从 bikepower_config.h）
  2. 运行构建前测试门禁（必须全部通过）
     ├─ make -C test && ./test/test_domain
     ├─ python3 tests/test_portal_config_save.py
     ├─ python3 tests/test_core_flow_endurance.py --minutes 3
     └─ python3 tests/check_*.py（7 个静态门禁）
  3. 对每个板型:
     ├─ 生成 sdkconfig.board.tmp（追加板型 Kconfig）
     ├─ idf.py build
     └─ esptool.py merge_bin → 单文件 bin
  4. Release 标准化:
     ├─ 生成 SHA256 旁文件
     ├─ 汇总 manifest.txt（文件名/大小/SHA256）
     ├─ 收录真机验收证据（evidence/）
     └─ generate_release_docs.py 生成 test-report.md/user-manual.md
  5. 打包 zip 总包
  6. 产物门禁 check_release_artifacts.py
  7. 输出产物到 build/release/
```

### 输出产物（build/release/）

| 产物 | 说明 |
|------|------|
| `bikepower-c-<板型>-v<版本>-stable-fix-g<GitSHA>.bin` | 合并单文件固件 |
| `*.bin.sha256` | SHA256 校验文件 |
| `manifest.txt` | 产物清单（文件名/大小/SHA256/版本） |
| `test-report.md` | 测试报告模板 |
| `core-flow-endurance.md` | 核心流程耐久测试报告 |
| `user-manual.md` | 用户手册 |
| `images/` | 文档引用截图 |
| `evidence/device-acceptance-<板型>.json` | 真机验收证据（如存在） |
| `bikepower-c-release-v<版本>-stable-fix-g<GitSHA>.zip` | 总包 |
| `*.zip.sha256` | 总包校验 |

### 版本元信息

从 `main/bikepower_config.h` 提取：

```c
#define FIRMWARE_VERSION       "4.1.1"
#define FIRMWARE_STABLE_VERSION "4.1"
#define FIRMWARE_FIX_VERSION   "0"
```

产物文件名格式：
`bikepower-c-<board>-v<VERSION>-stable-fix-g<GIT_SHA>.bin`

示例：`bikepower-c-generic-c3-v4.1.1-stable-fix-ga1b2c3d.bin`

---

## GitHub Pages 网页刷写

`.github/workflows/deploy-web-tools.yml` 把 `tools/web/ble-monitor.html` 作为 GitHub Pages 首页发布，并在部署阶段调用 `scripts/tools/prepare_web_flash_site.py`：

1. 优先读取当前 GitHub 仓库的最新 Release；没有完整附件时回退到 `GITEE_REPOSITORY`。
2. 严格选择当前 tag 对应的 generic-c3 / m5stamp-c3 merged bin 和同名 `.sha256`。
3. 下载后校验 SHA256，再把固件复制进同源 Pages artifact。
4. 生成 `firmware/latest.json` 与两个 ESP Web Tools manifest，芯片为 `ESP32-C3`，merged bin 从 `offset=0` 刷写。
5. 没有可用 Release 时生成 `available=false` 索引，BLE Monitor 继续部署，刷写按钮保持禁用。

首次使用需在 GitHub 仓库 `Settings → Pages` 选择 `GitHub Actions`。Release 位于 Gitee 时配置 Actions secret `GITEE_TOKEN`；可选仓库变量 `GITEE_REPOSITORY` 默认 `mengen4jv/bikepower-c`。工作流支持 main 相关改动、GitHub Release 发布、每日同步和手动触发。

本地离线验证：

```bash
python3 tests/test_prepare_web_flash_site.py
python3 tests/check_ble_monitor.py
```

浏览器刷写必须使用 HTTPS 页面和桌面版 Chrome / Edge；用户仍需在 Web Serial 弹窗中主动选择串口，网页不能静默选择设备。

---

## 产物校验

### check_release_artifacts.py

双板型构建完成后，自动运行产物完整性门禁：

```bash
python3 scripts/tools/check_release_artifacts.py \
    --release-dir build/release \
    --version 4.1.1 \
    --git-sha <sha>
```

校验内容：
- 两个板型 merged bin 存在
- SHA256 旁文件存在且与实际文件一致
- manifest.txt 格式正确、覆盖所有产物
- test-report.md / core-flow-endurance.md / user-manual.md 存在
- zip 总包存在且包含所有文件
- 版本号与文件名一致

### device_acceptance.py（真机验收）

```bash
python3 scripts/tools/device_acceptance.py validate \
    --file tests/reports/device-acceptance-generic-c3.json \
    --firmware-version 4.1.1 \
    --git-sha <sha> \
    --board generic-c3
```

验证 evidence JSON 的完整性，确认 P0 测试项全部通过。

### quality_gate.py（综合质量门禁）

[quality_gate.py](../../scripts/tools/quality_gate.py) 是发版前的综合检查入口，运行所有静态门禁+测试+产物校验。

---

## 常用命令速查

### 日常开发

| 命令 | 场景 |
|------|------|
| `./scripts/flash_latest.sh` | 默认开发流程：门禁→编译→烧录→抓日志 |
| `./scripts/flash_latest.sh --monitor` | 烧录后持续监视串口 |
| `./scripts/flash_latest.sh --board m5stamp` | 烧录 M5Stamp 版 |
| `./scripts/flash_latest.sh --erase` | 首次烧录/分区表变更后整片擦除 |
| `idf.py -p <port> monitor` | 仅监视串口日志 |
| `idf.py build` | 仅编译 |

### 测试

| 命令 | 场景 |
|------|------|
| `make -C test && ./test/test_domain` | 领域层单元测试 |
| `python3 tests/check_config_consistency.py` | 配置一致性检查 |
| `python3 tests/mock_server.py` | 启动前端 mock 服务器 |
| `python3 tests/ble_selftest.py` | （需设备）BLE 服务自测 |
| `python3 tests/stage5_portal_test.py` | （需设备+配网）门户自测 |
| `python3 tests/test_prepare_web_flash_site.py` | 网页刷写站点生成器离线回归 |

### 发布

| 命令 | 场景 |
|------|------|
| `./scripts/build_all.sh --dry-run` | 预览 release 构建计划 |
| `./scripts/build_all.sh` | 双板型完整 release 构建 |
| `./scripts/build_all.sh generic` | 仅编译通用板 |
| `python3 scripts/tools/check_release_artifacts.py --release-dir build/release --version <ver> --git-sha <sha>` | 产物完整性校验 |
| `python3 scripts/tools/prepare_web_flash_site.py --site-dir <dir>` | 生成 Pages 预览站点并同步最新 Release |

### 日志调试

| 命令 | 场景 |
|------|------|
| `python3 tests/serial_capture.py --port <port> --duration 30` | 抓 30s 串口日志保存到文件 |
| `python3 tests/serial_daemon.py --port <port>` | 串口守护进程（持续记录日志） |
| `./scripts/log_daemon.sh` | 日志守护包装脚本 |

---

## 构建相关脚本索引

| 脚本 | 路径 | 用途 |
|------|------|------|
| flash_latest.sh | `scripts/flash_latest.sh` | 一键开发烧录 |
| build_all.sh | `scripts/build_all.sh` | 双板型 release 构建 |
| inline_static.py | `scripts/inline_static.py` | 编译时内联 CSS/JS 到 HTML |
| log_daemon.sh | `scripts/log_daemon.sh` | 串口日志守护 |
| quality_gate.py | `scripts/tools/quality_gate.py` | 综合质量门禁 |
| check_release_artifacts.py | `scripts/tools/check_release_artifacts.py` | Release 产物校验 |
| device_acceptance.py | `scripts/tools/device_acceptance.py` | 真机验收证据校验 |
| generate_release_docs.py | `scripts/tools/generate_release_docs.py` | 生成 release 文档 |
| prepare_web_flash_site.py | `scripts/tools/prepare_web_flash_site.py` | 校验 Release 固件并生成 Pages 刷写站点 |
| device_monitor.py | `scripts/tools/device_monitor.py` | BLE 设备实时监控 |
| build_firmware.sh | `scripts/platform/unix/build_firmware.sh` | 单固件构建（被 build_all.sh 调用） |
