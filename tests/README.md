# 测试脚本使用说明

> BikePower C 设备测试脚本集合
> 更新日期：2026-06-27

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
| `pyserial` | 串口通信（设备重启、日志抓取） | ota_full_test.sh, serial_capture.py |
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
| `ota_full_test.sh` | OTA 全流程自动测试（升级+降级+版本确认） | nmcli + pyserial + Python3 |
| `ota_upload_test.py` | OTA 固件上传单步测试 | Python3（无第三方依赖） |
| `stage5_portal_test.py` | 配网门户 HTTP 路由自测（8 条用例） | Python3 |
| `ble_selftest.py` | BLE 五服务 + FTMS ERG 自测 | bleak |
| `stage3_conn_test.py` | BLE 连接事件驱动状态机验证 | bleak |
| `serial_capture.py` | 串口日志抓取工具 | pyserial |

---

## 三、ota_full_test.sh — OTA 全流程测试

### 功能

自动完成 OTA 升级全链路验证：
1. 重启设备
2. 连接设备 SoftAP（BikePower）
3. 配网门户 8 条路由测试
4. /ota 页面可达性测试
5. 上传新版本固件（验证升级成功）
6. 等待设备重启并重连
7. 上传旧版本固件（验证降级被拒）
8. 串口确认设备版本号
9. 恢复本机网络

### 用法

```bash
tests/ota_full_test.sh --baseline <旧版本bin> --target <新版本bin> [选项]
```

### 参数

| 参数 | 必填 | 默认值 | 说明 |
|------|------|--------|------|
| `--baseline <bin>` | 是 | — | 旧版本固件路径（降级测试用，期望被拒） |
| `--target <bin>` | 是 | — | 新版本固件路径（升级测试用，期望成功） |
| `--port <serial>` | 否 | `/dev/ttyACM0` | 设备串口设备 |
| `--iface <wifi_if>` | 否 | `wlp1s0` | 本机无线网卡接口名 |
| `--ap-ssid <ssid>` | 否 | `BikePower` | 设备 SoftAP SSID |
| `--ap-psk <psk>` | 否 | `12345678` | 设备 SoftAP 密码 |
| `--home-ssid <ssid>` | 否 | `CU_juuw` | 测试结束连回的家庭 WiFi |

### 前置条件

1. 设备已烧录 baseline 固件到 ota_0 分区
2. baseline 固件需带自动配网能力（编译时 `-DOTA_TEST_AUTO_CONFIG=ON`）
3. 本机 nmcli 可用，无线网卡已启用
4. target 固件版本号 > baseline 固件版本号

### 示例

```bash
# 编译测试固件（带自动配网）
idf.py build -DOTA_TEST_AUTO_CONFIG=ON
cp build/bikepower-c.bin /tmp/ota_target.bin

# 烧录 baseline（旧版本）到设备
python3 -m esptool --chip esp32c3 -p /dev/ttyACM0 erase_flash
python3 -m esptool --chip esp32c3 -p /dev/ttyACM0 write_flash \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 /tmp/ota_baseline.bin \
  0x390000 build/ota_data_initial.bin

# 运行全流程测试
tests/ota_full_test.sh \
  --baseline /tmp/ota_baseline.bin \
  --target /tmp/ota_target.bin
```

### 退出码

| 码 | 含义 |
|----|------|
| 0 | 全流程通过 |
| 1 | 某步骤失败（见输出 FAIL 行） |
| 2 | 参数错误 |

---

## 四、ota_upload_test.py — OTA 上传单步测试

### 功能

向设备 `/ota/upload` 接口上传固件 bin，校验响应状态和内容。用于单独验证上传逻辑，不包含设备重启/重连流程。

### 用法

```bash
python3 tests/ota_upload_test.py --file <固件bin> [--expect success|fail] [选项]
```

### 参数

| 参数 | 必填 | 默认值 | 说明 |
|------|------|--------|------|
| `--file <bin>` | 是 | — | 固件文件路径 |
| `--expect <结果>` | 否 | `success` | 期望结果：`success`（上传成功）或 `fail`（被拒绝/降级） |
| `--ip <ip>` | 否 | `192.168.4.1` | 设备 IP 地址 |
| `--port <port>` | 否 | `80` | HTTP 端口 |

### 前置条件

1. 设备已进入配网模式（SoftAP 已启动）
2. 本机已连上设备 SoftAP（`nmcli` 或手动连接）
3. 能 ping 通 192.168.4.1

### 示例

```bash
# 连接设备 SoftAP
nmcli device wifi connect BikePower password 12345678

# 上传新固件（期望成功）
python3 tests/ota_upload_test.py --file /tmp/ota_target.bin --expect success

# 上传旧固件（期望被拒绝降级）
python3 tests/ota_upload_test.py --file /tmp/ota_baseline.bin --expect fail
```

### 退出码

| 码 | 含义 |
|----|------|
| 0 | 测试通过（响应符合 expect） |
| 1 | 测试失败（响应不符合 expect） |

---

## 五、其他脚本简述

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

## 六、测试固件编译说明

### 正式固件（无自动配网）

```bash
idf.py build
# 产物：build/bikepower-c.bin（约 1MB）
```

### 测试固件（带自动配网，用于 OTA 自动化测试）

```bash
idf.py build -DOTA_TEST_AUTO_CONFIG=ON
# 启动后 BLE 就绪 3s 自动触发配网，支持 ota_full_test.sh 全自动测试
```

> `OTA_TEST_AUTO_CONFIG` 是 CMake option，默认 OFF。正式发版编译不带此选项，测试编译传 `-DOTA_TEST_AUTO_CONFIG=ON`。两者代码一致，仅测试固件多一个启动自动配网钩子。

---

## 七、常见问题

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

### Q4：OTA 上传超时

WiFi 共存模式下传输速度受限，1MB 固件上传可能需 30-60 秒。`ota_upload_test.py` 默认超时 120 秒，若仍超时检查 WiFi 信号强度。
