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
| `ble_selftest.py` | BLE 五服务 + FTMS ERG 自测 | bleak |
| `stage3_conn_test.py` | BLE 连接事件驱动状态机验证 | bleak |
| `serial_capture.py` | 串口日志抓取工具 | pyserial |

> v3.1.6 起 OTA 升级统一为 HTTPS 在线下载方式，已移除基于 `/ota/upload` 上传的 `ota_full_test.sh` 与 `ota_upload_test.py`。在线 OTA 全流程需配合家庭 WiFi 与 OTA 版本服务器，建议人工按 [docs/11-ota-user-guide.md](../docs/11-ota-user-guide.md) 验证。

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
