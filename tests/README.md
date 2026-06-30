# 测试脚本使用说明

> BikePower C 设备测试脚本集合
> 更新日期：2026-06-29（v3.2.0 前后端分离重构后梳理）

---

## 〇、目录分工说明

项目根下有两个测试目录，**职责不同、不合并**（评估结论见文末「test/ 与 tests/ 合并评估」）：

| 目录 | 用途 | 内容 |
|------|------|------|
| `test/` | 领域层 host 单测（纯 C） | `test_domain.c` + `Makefile`，产物 `test/test_domain` 已 gitignore |
| `tests/` | 设备测试 + 静态门禁 + mock + 工具（本目录） | Python / Shell 脚本 |

`test/` 由 ESP-IDF 生态约定保留给 host 单测；`tests/` 放所有需要外部依赖（Python/bleak/pyserial/Node）的脚本。

---

## 一、环境依赖

### 系统要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Linux（已验证 Ubuntu） |
| Python | 3.8+ |
| Node.js | 16+（仅 `check_portal_js.py` 需要） |
| 串口权限 | 当前用户在 `dialout` 组（访问 /dev/ttyACM0） |
| 网络工具 | `nmcli`（仅动态配网测试需要切换 WiFi 时用） |
| 无线网卡 | 支持 AP 模式连接的 WiFi 网卡（如 wlp1s0） |

### Python 依赖

```bash
pip install pyserial bleak
```

| 包 | 用途 | 脚本 |
|----|------|------|
| `pyserial` | 串口通信（设备重启、日志抓取） | serial_capture.py, serial_daemon.py |
| `bleak` | BLE 五服务自测 | ble_selftest.py, stage3_conn_test.py |

### ESP-IDF 工具链（编译固件时需要）

```bash
# 已安装 ESP-IDF v5.5.4，导出环境
export IDF_PATH=/home/mengen1014/work/esp32-power/esp-idf
source $IDF_PATH/export.sh
```

---

## 二、脚本分类清单（按用途）

### A. 静态门禁（无需设备，CI 前置）

每次代码改动必跑的前置门，全部静态扫描源码，零设备依赖。

| 脚本 | 用途 | 规则 ID | 优先级 |
|------|------|---------|--------|
| `check_portal_js.py` | 门户 JS 语法静态检查（`main/web/app.js` 跑 `node --check`） | TC-3a | **最高，每次改动必跑** |
| `check_portal_structure.py` | 门户结构静态检查（路由表 / HTML / 占位符 / API 一致性） | TC-3d | 高 |
| `check_config_consistency.py` | 常量一致性静态检查（config 头文件 ↔ 引用方） | TC-3c | 高 |
| `check_nvs_schema.py` | NVS schema 静态一致性检查（nvs_store ↔ config） | TC-3e | 高 |
| `check_board_desc.py` | 板型描述表静态一致性检查（board_detect ↔ config ↔ Kconfig） | TC-3f | 高 |
| `check_tasks_wdt.py` | 任务/队列/WDT 静态完整性检查（每个 xTaskCreate 后必须有错误检查） | TC-3g | 高 |

```bash
# 一键跑全部静态门禁
for s in check_portal_js check_portal_structure check_config_consistency \
         check_nvs_schema check_board_desc check_tasks_wdt; do
    python3 tests/$s.py || exit 1
done
```

### B. 设备端动态自测（需真机 + 串口/BLE）

| 脚本 | 用途 | 依赖 | 触发方式 |
|------|------|------|----------|
| `ble_selftest.py` | BLE 五服务（CPS+HRS+CSC+FTMS+DIS）+ FTMS ERG 控制点 | bleak | 设备启动后直接跑 |
| `stage3_conn_test.py` | BLE 连接事件驱动状态机验证 | bleak | 设备启动后直接跑 |
| `stage5_portal_test.py` | 配网门户 HTTP 路由自测（/api/xxx JSON 接口） | Python3 | 设备进入配网态后跑 |

### C. 本地 Mock（无需烧设备调试前端）

| 脚本 | 用途 | 端口 |
|------|------|------|
| `mock_server.py` | 复刻设备全部 /api/xxx JSON 接口 + 首页静态资源，本地调试 `main/web/` 前端 | 默认 8080 |

```bash
# 完整 mock（覆盖全部 /api/xxx）
python3 tests/mock_server.py
# 浏览器访问 http://127.0.0.1:8080/ 调试前端
```

### E. 串口日志工具

| 脚本 | 用途 | 运行模式 |
|------|------|----------|
| `serial_capture.py` | 一次性抓取串口日志（指定时长） | 前台阻塞，超时退出 |
| `serial_daemon.py` | 串口日志守护进程（后台常驻，自动让出烧录串口） | 后台 nohup |

```bash
# 一次性抓 30 秒
python3 tests/serial_capture.py --port /dev/ttyACM0 --duration 30
# 后台守护（不传 -p 自动探测端口：Linux /dev/ttyACM*、macOS /dev/cu.usbmodem*）
nohup python3 tests/serial_daemon.py > /tmp/serial_daemon.out 2>&1 &
# 也可显式指定端口
nohup python3 tests/serial_daemon.py -p /dev/cu.usbmodem101 > /tmp/serial_daemon.out 2>&1 &
```

> 守护进程异步写入 `tests/logs/serial_<YYYYMMDD>.log`（按日滚动，已被 `.gitignore` 忽略），烧录时自动让出串口、烧完自动重连，跨 Linux/macOS。停止：`pkill -f serial_daemon.py`。

> v4.0.0 起已移除在线 OTA 与家庭网络配置，固件升级走全量重新烧录（分区表变更需 `erase_flash`）。

### F. 压力测试

| 脚本 | 用途 |
|------|------|
| `run_stress.sh` | BLE 长连接 + 频繁通知压力测试 |

### G. 测试运行器（统一入口）

| 脚本 | 用途 |
|------|------|
| `test_runner.sh` | 统一入口，串联 host 单测 + BLE 自测 + 配网自测 + 压力 + 日志 |

```bash
bash tests/test_runner.sh -d    # 仅 host 单测
bash tests/test_runner.sh -b    # BLE 自测（需真机）
bash tests/test_runner.sh -w    # 配网自测（需真机配网态）
bash tests/test_runner.sh -a    # 全部
bash tests/test_runner.sh -s    # 压力测试
bash tests/test_runner.sh -l    # 串口日志
```

---

## 三、典型场景

### 场景 1：改了 `main/web/app.js` 后的最小验证

```bash
python3 tests/check_portal_js.py                    # TC-3a，必跑
python3 tests/check_portal_structure.py             # TC-3d
python3 tests/mock_server.py                        # 本地浏览器手测
```

### 场景 2：改了 `main/wifi_portal.c` 后的完整静态门禁

```bash
for s in check_portal_js check_portal_structure check_config_consistency \
         check_nvs_schema check_board_desc check_tasks_wdt; do
    python3 tests/$s.py || exit 1
done
```

### 场景 3：领域层（components/bikepower_domain）改动

```bash
make -C test && ./test/test_domain    # host 单测，不在本目录
```

### 场景 4：完整真机回归（v3.2.0 发版前）

```bash
# 1. 烧正式固件
idf.py -p /dev/ttyACM0 flash
# 2. 串口确认启动
python3 tests/serial_capture.py --port /dev/ttyACM0 --duration 12
# 3. BLE 五服务 + ERG
python3 tests/ble_selftest.py
# 4. 烧测试固件进配网态，跑门户自测
idf.py reconfigure -DOTA_TEST_AUTO_CONFIG=ON && idf.py build && idf.py -p /dev/ttyACM0 flash
python3 tests/stage5_portal_test.py --ip 192.168.4.1
# 5. 烧回正式固件
idf.py reconfigure -DOTA_TEST_AUTO_CONFIG=OFF && idf.py build && idf.py -p /dev/ttyACM0 flash
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

> 切换 option 后必须 `idf.py reconfigure` 或删 `build/CMakeCache.txt`，否则缓存不刷新。

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
idf.py reconfigure -DOTA_TEST_AUTO_CONFIG=ON
idf.py build
```

### Q4：bleak 连接超时（服务发现卡住）

重置蓝牙适配器后重试：

```bash
bluetoothctl power off
sleep 1
bluetoothctl power on
sleep 2
python3 tests/ble_selftest.py
```

### Q5：本机不能切换 WiFi（保持互联网连接）

- 用 `tests/mock_server.py` 在本地 8080 调试前端（零设备依赖）
- 动态配网测试改日再做，或换另一台机器执行

---

## 六、test/ 与 tests/ 合并评估（2026-06-29）

**结论：不合并**。两个目录职责不同，合并反而引入风险。

### 当前分工

- `test/` — 领域层 host 单测（纯 C）：`test_domain.c` + `Makefile`，产物 `test/test_domain` 已 gitignore
- `tests/` — 设备测试 + 静态门禁 + mock + 工具（Python/Shell，本目录）

### 评估过的方案

| 方案 | 改动量 | 优点 | 缺点 |
|------|--------|------|------|
| A. 合并到 `tests/host/` 子目录 | ~15 个文件路径改写 | 单一 tests/ 目录 | Makefile 路径变 `../../`，复杂 |
| B. 合并到 `tests/` 根 | ~15 个文件路径改写 | 最简单 | C 源码与 Python 脚本混杂 |
| **C. 保持现状（采纳）** | 0 | ESP-IDF 生态约定；Makefile 路径简单；零风险 | 两个测试目录仍存在 |

### 不合并的理由

1. **ESP-IDF 生态约定**：`test/` 是 ESP-IDF 官方约定放 host 单测的目录，与 `tests/`（设备测试脚本）语义不同
2. **Makefile 路径复杂化**：当前 `test/Makefile` 用 `../components/...` 引用领域层；合并后要改成 `../../components/...`，反而不直观
3. **影响范围大**：合并需修改 15+ 文件中的路径引用（规则、文档、CHANGELOG、skill、quality_gate.py、test_runner.sh、.gitignore 等），改动风险高于收益
4. **现有配置已正确**：`.gitignore` 已正确忽略 `test/test_domain` 产物，无冲突

### 影响路径清单（若未来仍要合并，需同步修改）

- `.gitignore`：`test/test_domain` → `tests/host/test_domain`
- `tests/test_runner.sh`：`HOST_TEST="./test/test_domain"` 与 `make -C test`
- `scripts/tools/quality_gate.py`：3 处路径引用
- `.trae/rules/best-practices.md`、`general-rules.md`、`workflows/test-coverage.md`、`workflows/pre-dev-checklist.md`、`workflows/context-engineering.md`
- `.trae/skills/release/SKILL.md`、`.trae/skills/real-device-test/SKILL.md`
- `AGENTS.md`、`docs/04-technical-reference.md`、`docs/test-coverage-analysis.md`
- `CHANGELOG.md`（历史记录，可保留旧路径）

---

## 七、已移除的脚本（历史记录）

| 脚本 | 移除版本 | 原因 | 替代 |
|------|----------|------|------|
| `ota_full_test.sh` | v3.1.6 | OTA 升级改为 HTTPS 在线下载，移除上传流程 | （在线 OTA 已于 v4.0.0 整体移除） |
| `ota_upload_test.py` | v3.1.6 | 同上 | （同上） |
| `ota_flow_test.py` | v4.0.0 | 移除在线 OTA 功能 | 无 |
| `ota_local_mock.py` | v4.0.0 | 移除在线 OTA 功能 | `mock_server.py` |
| `ota_serial_monitor.py` | v4.0.0 | 移除在线 OTA 功能 | `serial_capture.py` |
