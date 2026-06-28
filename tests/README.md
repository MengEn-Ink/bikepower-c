# 测试脚本使用说明

> BikePower C 设备测试脚本集合
> 更新日期：2026-06-28

---

## 一、环境依赖

### 系统要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Linux（已验证 Ubuntu） |
| Python | 3.8+ |
| 串口权限 | 当前用户在 `dialout` 组（访问 /dev/ttyACM0） |
| 网络工具 | `nmcli`（NetworkManager CLI，用于 WiFi 连接管理） |
| 无线网卡 | 支持 AP 模式连接的 WiFi 网卡（如 wlp1s0） |

### Python 依赖

```bash
pip install pyserial bleak
```

| 包 | 用途 | 脚本 |
|----|------|------|
| `pyserial` | 串口通信（设备重启、日志抓取） | serial_capture.py |
| `bleak` | BLE 五服务自测 | ble_selftest.py, stage3_conn_test.py |

### ESP-IDF 工具链（编译固件时需要）

```bash
# 已安装 ESP-IDF v5.5.4，导出环境
export IDF_PATH=/home/mengen1014/work/esp32-power/esp-idf
source $IDF_PATH/export.sh
```

---

## 二、脚本清单

| 脚本 | 用途 | 依赖 |
|------|------|------|
| `stage5_portal_test.py` | 配网门户 HTTP 路由自测（8 条用例） | Python3 |
| `check_portal_js.py` | **门户 JS 语法静态检查（TC-3a，前置门）** | Python3 + Node.js |
| `check_portal_structure.py` | **门户结构静态检查（TC-3d，路由/HTML/表单）** | Python3 |
| `check_nvs_schema.py` | **NVS schema 静态一致性检查（TC-3e）** | Python3 |
| `check_board_desc.py` | **板型描述表静态一致性检查（TC-3f）** | Python3 |
| `check_tasks_wdt.py` | **任务/队列/WDT 静态完整性检查（TC-3g）** | Python3 |
| `check_config_consistency.py` | **常量一致性静态检查（TC-3c）** | Python3 |
| `ota_flow_test.py` | **OTA 全流程自测（TC-3b，静态+动态）** | Python3 + Node.js |
| `ota_local_mock.py` | **OTA 页面本地模拟器（无需连接 BikePower 热点）** | Python3 + Node.js |
| `ble_selftest.py` | BLE 五服务 + FTMS ERG 自测 | bleak |
| `stage3_conn_test.py` | BLE 连接事件驱动状态机验证 | bleak |
| `serial_capture.py` | 串口日志抓取工具 | pyserial |
| `serial_daemon.py` | 串口日志守护进程（后台常驻，自动让出烧录） | pyserial |

### check_portal_js.py（TC-3a，最高优先级前置门）

```bash
# 任何修改 main/wifi_portal.c 后必跑，JS 语法全绿才能编译/烧录
python3 tests/check_portal_js.py
# 详细模式（打印每个 script 块预览）
python3 tests/check_portal_js.py --verbose
```

从 `main/wifi_portal.c` 提取所有 C 字符串字面量拼接的 HTML，定位所有 `<script>` 块，用 `node --check` 验证 JS 语法。自动替换 C printf 占位符（`%d`/`%s` 等）避免误报。

历史教训：v3.1.6 `/ota` 页 `pollStatus` 结尾 `}}}` 多一个 `}`，导致整个 script 块报 `missing ) after argument list`，"检查更新"按钮点击完全无反应。本脚本在编译前拦截此类 bug。

### check_portal_structure.py（TC-3d，门户结构静态检查）

```bash
python3 tests/check_portal_structure.py
```

从 `main/wifi_portal.c` 提取 C 字符串拼接的 HTML，静态校验：
1. 路由表完整性（/、/save、/scan、/connect、/status、/ota）
2. HTML 表单标记（data-ssid、home-psk、ssid=/psk= 提交参数）
3. HTML 基础结构（html/head/body/meta viewport + form action）

### check_nvs_schema.py（TC-3e，NVS schema 一致性）

```bash
python3 tests/check_nvs_schema.py
```

校验 `main/nvs_store.c` 使用的 NVS key 与 `main/bikepower_config.h` 的 `NVS_KEY_*` 定义一致：
1. 所有定义的 key 必须被读写至少一次
2. nvs_store.c 使用的宏必须在 config 中定义
3. key 字符串长度 ≤ 15（NVS 限制）
4. NVS_NAMESPACE 与 NVS_NAMESPACE_WIFI 必须不同

### check_board_desc.py（TC-3f，板型描述表一致性）

```bash
python3 tests/check_board_desc.py
```

校验 `main/board_detect.c` 的 `s_board_desc[]` 与 `main/bikepower_config.h` 一致：
1. 板型枚举数量 = 描述表行数
2. 描述表每行使用的引脚/驱动/名称宏必须在 config 中定义
3. Kconfig 板型宏（CONFIG_BOARD_*_C3）与枚举一一对应
4. BOARD_NAME_* 符合 kebab-case + `-c3` 后缀命名规范

### check_tasks_wdt.py（TC-3g，任务/WDT 完整性）

```bash
python3 tests/check_tasks_wdt.py
```

扫描 `main/*.c`，校验：
1. 每个 `xTaskCreate` 栈大小必须用宏（TASK_STACK_* 或局部 #define）
2. 每个 `xTaskCreate` 后必须紧跟错误检查（pdPASS/BaseType_t/ESP_LOGE）
3. `app_main.c` 必须调用 `esp_task_wdt_add` 注册看门狗 + `esp_task_wdt_reset` 喂狗
4. 优先级硬编码数字降级为 WARN（不阻断）

历史发现：`wifi_portal.c` 的 `ota_upgrade_task` 创建后曾缺少错误检查；现已修复，TC-3g 会阻断同类回归。

### ota_flow_test.py（TC-3b，OTA 全流程）

```bash
# 静态部分（无需设备，默认）
python3 tests/ota_flow_test.py --static
# 动态部分（需设备进入配网态）
python3 tests/ota_flow_test.py --dynamic --ip 192.168.4.1
# 动态部分 + 完整家庭 WiFi 流程
python3 tests/ota_flow_test.py --dynamic --ip 192.168.4.1 --ssid MyHome --psk secret
```

静态部分验证：OTA 路由注册、按钮元素、onclick 绑定、JS 函数定义、x.open 调用与路由一致、JS 语法。
动态部分验证：GET /ota、POST /ota/check、GET /ota/status 状态机轮询、配 WiFi 后完整检查流程。

### ota_local_mock.py（OTA 页面本地模拟）

```bash
# 自动启动 localhost mock server，并用 Node 执行固件 /ota 页面的真实 JS 流程
python3 tests/ota_local_mock.py --self-test

# 手动打开网页测试
python3 tests/ota_local_mock.py --serve --port 8080
# 浏览器访问 http://127.0.0.1:8080/ota
```

适用于本机连接 `BikePower` 热点后没有互联网、NetworkManager 自动回连家庭 WiFi、或不方便切换无线网络的场景。它只模拟 `/ota/check`、`/ota/status`、`/ota/upgrade` 的 HTTP 合约和 OTA 页面前端状态机，不覆盖 ESP32 真机 STA 切换、HTTPS 下载写入 flash 和 bootloader 回滚。

### serial_daemon.py（串口日志守护进程）

```bash
# 后台启动（自动让出烧录串口）
nohup python3 tests/serial_daemon.py -p /dev/ttyACM0 > /tmp/serial_daemon.out 2>&1 &
# 停止
pkill -f serial_daemon.py
# 日志按日滚动：tests/logs/serial_<YYYYMMDD>.log
```

> v3.1.6 起 OTA 升级统一为 HTTPS 在线下载方式，已移除基于 `/ota/upload` 上传的 `ota_full_test.sh` 与 `ota_upload_test.py`。在线 OTA 全流程由 `ota_flow_test.py` 覆盖，人工验证参考 [docs/11-ota-user-guide.md](../docs/11-ota-user-guide.md)。

---

## 三、其他脚本简述

### stage5_portal_test.py

```bash
# 前置：已连上设备 SoftAP
python3 tests/stage5_portal_test.py
# 输出 8/8 用例通过即配网门户路由正常
```

测试配网门户的 8 条 HTTP 路由：首页、草稿提交、Captive Portal 探测端点（generate_204、hotspot-detect.html 等）、成功页守卫。

### ble_selftest.py

```bash
# 前置：设备已启动 BLE 广播
python3 tests/ble_selftest.py
# 验证 CPS+HRS+CSC+FTMS+DIS 五服务 + FTMS ERG 控制点
```

### serial_capture.py

```bash
# 抓取串口日志
python3 tests/serial_capture.py --port /dev/ttyACM0 --duration 30
```

---

## 四、测试固件编译说明

### 正式固件（无自动配网）

```bash
idf.py build
# 产物：build/bikepower-c.bin（约 1MB）
```

### 测试固件（带自动配网，用于配网门户自动化测试）

```bash
idf.py build -DOTA_TEST_AUTO_CONFIG=ON
# 启动后 BLE 就绪 3s 自动触发配网，便于 stage5_portal_test.py 等脚本全自动验证
```

> `OTA_TEST_AUTO_CONFIG` 是 CMake option，默认 OFF。正式发版编译不带此选项，测试编译传 `-DOTA_TEST_AUTO_CONFIG=ON`。两者代码一致，仅测试固件多一个启动自动配网钩子。

---

## 五、常见问题

### Q1：nmcli 连接 BikePower 报"缺少属性"

使用 `nmcli connection add` 显式创建连接而非 `nmcli device wifi connect`：

```bash
nmcli connection add type wifi ifname wlp1s0 con-name BikePowerTest \
  802-11-wireless.ssid "BikePower" \
  802-11-wireless-security.key-mgmt wpa-psk \
  802-11-wireless-security.psk "12345678"
nmcli connection up BikePowerTest ifname wlp1s0
```

### Q2：串口权限拒绝

```bash
sudo usermod -aG dialout $USER
# 重新登录或 newgrp dialout 生效
```

### Q3：测试固件不自动开配网

确认编译时传了 `-DOTA_TEST_AUTO_CONFIG=ON`。若之前编译过正式版，需清除 CMake 缓存：

```bash
rm -f build/CMakeCache.txt
idf.py build -DOTA_TEST_AUTO_CONFIG=ON
```
