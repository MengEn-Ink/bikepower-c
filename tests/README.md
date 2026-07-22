# 测试脚本使用说明

> BikePower C 设备测试脚本集合
> 更新日期：2026-07-03（v4.0.0 契约校准：固件生成同步产出测试报告与用户手册）

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
| `check_operation_coverage.py` | 操作覆盖矩阵检查（关键操作必须绑定自动化或人工门禁） | TC-3j | 高 |
| `check_ble_advertising_fields.py` | BLE 广播字段检查（骑行/跑步服务 UUID 与名字预算） | TC-BLE-ADV | 高 |

```bash
# 一键跑全部静态门禁
for s in check_portal_js check_portal_structure check_config_consistency \
         check_nvs_schema check_board_desc check_tasks_wdt check_operation_coverage \
         check_ble_advertising_fields; do
    python3 tests/$s.py || exit 1
done
```

### B. 设备端动态自测（需真机 + 串口/BLE）

| 脚本 | 用途 | 依赖 | 触发方式 |
|------|------|------|----------|
| `ble_selftest.py` | BLE 五服务（CPS+HRS+CSC+FTMS+DIS）+ FTMS ERG 控制点 | bleak | 设备启动后直接跑 |
| `ble_broadcast_monitor.py` | 通过配网页 API 切换广播项，并验证 CPS/HRS/CSC/FTMS 通知是否按开关停止/恢复 | bleak + 设备配网态 HTTP | 设备进入配网态后跑 |
| `stage3_conn_test.py` | BLE 连接事件驱动状态机验证 | bleak | 设备启动后直接跑 |
| `stage5_portal_test.py` | 配网门户 HTTP 路由自测（/api/xxx JSON 接口） | Python3 | 设备进入配网态后跑 |
| `../scripts/tools/serve_ble_monitor.py` + `../tools/web/ble-monitor.html` | 本地浏览器 BLE 监控，持续记录断连、通知停更和数值异常 | Chrome/Edge Web Bluetooth | 启动本地服务后浏览器手动授权连接 |

### C. 本地 Mock（无需烧设备调试前端）

| 脚本 | 用途 | 端口 |
|------|------|------|
| `mock_server.py` | 复刻设备全部 /api/xxx JSON 接口 + 首页静态资源，本地调试 `main/web/` 前端 | 默认 8080 |
| `test_portal_config_save.py` | 自动启动 mock server，模拟网页端 POST `/api/config` 保存并检查 `/api/state` 回显 | 随机空闲端口 |
| `test_core_flow_endurance.py` | 3 分钟核心流程耐久测试：循环模拟按钮调功、功率变化心率随动、网页端各类编辑、LED 静默、设备模板和关闭 WiFi | 随机空闲端口 |
| `operation_gate.py` | 操作级测试门禁聚合入口：quick/full/device，输出 Markdown 报告并可写入真机验收证据 | 随机空闲端口 / 真机端口 |

```bash
# 完整 mock（覆盖全部 /api/xxx）
python3 tests/mock_server.py
# 浏览器访问 http://127.0.0.1:8080/ 调试前端
python3 tests/test_portal_config_save.py  # 自动化验证保存回显，无需真机
python3 tests/test_core_flow_endurance.py # 默认跑 3 分钟核心流程耐久测试
python3 tests/test_core_flow_endurance.py --minutes 90 # 需要长时间老化时手动加长
python3 tests/operation_gate.py --level quick # 开发期快速操作门禁
python3 tests/operation_gate.py --level full  # 提交前完整无设备门禁
python3 scripts/tools/device_acceptance.py init --board generic-c3 --operator "验收人"
```

### D. BLE 浏览器监控客户端

| 文件 | 用途 |
|------|------|
| `tools/web/ble-monitor.html` | Web Bluetooth 客户端，解析 CPS/HRS/CSC/FTMS/RSC/DIS，显示实时数据、异常计数和事件日志 |
| `scripts/tools/serve_ble_monitor.py` | 从 `tools/web/` 启动无缓存本地 HTTP 服务，默认 `http://127.0.0.1:8766/ble-monitor.html` |
| `tests/check_ble_monitor.py` | 静态门禁，检查监控客户端、异常检测和门禁接入是否漂移 |

```bash
python3 scripts/tools/serve_ble_monitor.py
# 用 Chrome / Edge 打开 http://127.0.0.1:8766/ble-monitor.html
python3 tests/check_ble_monitor.py
```

Web Bluetooth 首次连接必须由用户点击“连接设备”并在浏览器弹窗中授权；授权后可点“连接已授权设备”重连。页面会监控 BLE 断连、通知停更、功率/心率/踏频/跑步速度异常，并可导出 JSON 报告用于排查。

### E. BLE 广播项数据面门禁

`tests/ble_broadcast_monitor.py` 是真机自动化门禁：设备处于配网态时，脚本通过 `POST /api/config` 切换 `broadcast_mask`，同时保持 BLE 连接并统计通知。

覆盖场景：
- 全开：CPS、HRS、CSC、FTMS 功率/踏频字段都应有数据。
- 关闭功率：CPS 停止，FTMS 不再出现功率字段，心率和踏频仍正常。
- 只开心率：仅 HRS 持续通知，CPS/CSC/FTMS 停止。
- 只开功率：CPS 和 FTMS 功率字段正常，HRS/CSC 停止。

```bash
python3 tests/ble_broadcast_monitor.py --base-url http://192.168.4.1
python3 tests/check_ble_broadcast_monitor.py
python3 tests/operation_gate.py --level device --run-portal \
  --board generic-c3 --operator "验收人" --port /dev/cu.usbmodem101
```

脚本结束会尝试恢复原始 `sport_profile` 和 `broadcast_mask`。未传 `--run-portal` 时，`operation_gate.py --level device` 会跳过这项，因为它需要设备处于配网态才能切换网页配置。

### F. 串口日志工具

| 脚本 | 用途 | 运行模式 |
|------|------|----------|
| `serial_capture.py` | 一次性抓取串口日志；`--check-boot` 校验 ROM/复位启动并拒绝 panic/WDT/Backtrace/coredump | 前台阻塞，超时退出 |
| `serial_daemon.py` | 串口日志守护进程（后台常驻，自动让出烧录串口，识别重启与故障快照） | 后台 nohup |

```bash
# 一次性抓 30 秒
python3 tests/serial_capture.py --port /dev/ttyACM0 --duration 30
# device 验收使用严格启动判定；空日志、缺 ROM/复位标志或致命异常均返回失败
python3 tests/serial_capture.py --port /dev/ttyACM0 --duration 12 --check-boot
# 后台守护（不传 -p 自动探测端口：Linux /dev/ttyACM*、macOS /dev/cu.usbmodem*）
nohup python3 tests/serial_daemon.py > /tmp/serial_daemon.out 2>&1 &
# 也可显式指定端口
nohup python3 tests/serial_daemon.py -p /dev/cu.usbmodem101 > /tmp/serial_daemon.out 2>&1 &
# 检测到 ESP-ROM/rst/BikePower C 启动时自动切新文件
python3 tests/serial_daemon.py --split-on-reboot
# 关闭 panic/WDT/Backtrace 故障快照
python3 tests/serial_daemon.py --no-crash-snapshot
```

> 守护进程异步写入 `tests/logs/device_<YYYYMMDD_HHMMSS>.log`（保留最近 5 个，已被 `.gitignore` 忽略），烧录时自动让出串口、烧完自动重连，跨 Linux/macOS。默认按串口连接会话切文件；设备软重启但串口未断开时，会在当前日志中插入 `DEVICE REBOOT / BOOT BANNER` 分隔线。`--split-on-reboot` 可改为每次启动 banner 切新文件。检测到 `panic` / `WDT` / `Backtrace` 等关键字时，会生成 `tests/logs/crash_<YYYYMMDD_HHMMSS>.log` 保存最近上下文。停止：`pkill -f serial_daemon.py`。

> v4.0.0 起永久放弃设备内 OTA 与家庭网络配置；普通用户升级统一走网页端在线烧录，入口见 README 和用户手册。

### G. 压力测试

| 脚本 | 用途 |
|------|------|
| `run_stress.sh` | BLE 长连接 + 频繁通知压力测试 |

### H. 测试运行器（统一入口）

| 脚本 | 用途 |
|------|------|
| `test_runner.sh` | 统一入口，串联 host 单测 + BLE 自测 + 配网自测 + 压力 + 日志 |
| `operation_gate.py` | 操作覆盖门禁入口，按 quick/full/device 分层执行并生成报告 |

```bash
bash tests/test_runner.sh -d    # 仅 host 单测
bash tests/test_runner.sh -m    # mock 配网页保存测试（无需真机）
bash tests/test_runner.sh -e    # 3 分钟核心流程耐久测试（无需真机）
bash tests/test_runner.sh -b    # BLE 自测（需真机）
bash tests/test_runner.sh -w    # 配网自测（需真机配网态）
bash tests/test_runner.sh -a    # 全部
bash tests/test_runner.sh -s    # 压力测试
bash tests/test_runner.sh -l    # 串口日志
python3 tests/operation_gate.py --level quick
python3 tests/operation_gate.py --level full --build
python3 tests/operation_gate.py --level device --port /dev/cu.usbmodem101
```

### I. 真机验收证据

`scripts/tools/device_acceptance.py` 把真机自动结果与按钮、LED、手机 Captive Portal 等人工观察按板型保存到 `tests/reports/device-acceptance-generic-c3.json` 或 `tests/reports/device-acceptance-m5stamp-c3.json`。

```bash
python3 scripts/tools/device_acceptance.py init \
  --board generic-c3 \
  --operator "验收人"

python3 tests/operation_gate.py --level device \
  --board generic-c3 \
  --operator "验收人" \
  --port /dev/cu.usbmodem101 \
  --run-portal

python3 scripts/tools/device_acceptance.py set \
  --file tests/reports/device-acceptance-generic-c3.json \
  --item led_patterns \
  --status PASS \
  --evidence "generic-c3：待连接慢闪、连接常亮、确认快闪"

python3 scripts/tools/device_acceptance.py validate \
  --file tests/reports/device-acceptance-generic-c3.json
python3 scripts/tools/device_acceptance.py validate --strict \
  --file tests/reports/device-acceptance-generic-c3.json
```

状态限定为 `PASS`、`FAIL`、`NOT_RUN`。`NOT_RUN` 不是通过；严格模式要求证据板型、当前固件版本和 Git ID 匹配，且全部 P0 项为 `PASS`。本地 `build_all.sh` 在证据存在时按板型收录到 release 总包，正式双板 Gitee 发布强制要求 generic-c3 与 m5stamp-c3 两份完整 P0 证据。

---

## 三、典型场景

### 场景 1：改了 `main/web/app.js` 后的最小验证

```bash
python3 tests/check_portal_js.py                    # TC-3a，必跑
python3 tests/check_portal_structure.py             # TC-3d
python3 tests/check_portal_render.py                # 渲染关于页并保存截图
python3 tests/test_portal_config_save.py            # mock 保存回显
python3 tests/test_core_flow_endurance.py --cycles 1 --skip-host  # 快速走一轮核心流程
python3 tests/mock_server.py                        # 本地浏览器手测
```

### 场景 2：改了 `main/wifi_portal.c` 后的完整静态门禁

```bash
for s in check_portal_js check_portal_structure check_config_consistency \
         check_nvs_schema check_board_desc check_tasks_wdt check_ble_advertising_fields; do
    python3 tests/$s.py || exit 1
done
```

### 场景 3：领域层（components/bikepower_domain）改动

```bash
make -C test && ./test/test_domain    # host 单测，不在本目录
python3 tests/test_portal_config_save.py  # 如涉及配网页保存字段，同步跑 mock 保存回显
python3 tests/test_core_flow_endurance.py --cycles 1  # 如涉及按钮/心率/网页核心流程，先快速验证
```

### 场景 4：固件生成前的 3 分钟核心流程测试

```bash
python3 tests/test_core_flow_endurance.py
```

默认跑 3 分钟并生成 `tests/reports/core_flow_endurance_<时间>.md`，覆盖模拟按钮短按/长按调功、功率变化心率随动、网页端首页与设置页编辑、LED 静默模式、设备模板、续期和关闭 WiFi。快速开发验证可用 `--cycles 1`，正式固件生成时由 `scripts/build_all.sh` 自动执行；如需半小时或 90 分钟老化，可传 `--minutes 30` 或 `--minutes 90`。

### 场景 5：完整真机回归（v4.0.0 交付前）

```bash
# 1. 烧正式固件
idf.py -p /dev/ttyACM0 flash
# 2. 串口确认启动
python3 tests/serial_capture.py --port /dev/ttyACM0 --duration 12
# 3. BLE 五服务 + ERG
python3 tests/ble_selftest.py
# 4. 烧测试固件进配网态，跑门户自测
idf.py reconfigure -DPORTAL_TEST_AUTO_CONFIG=ON && idf.py build && idf.py -p /dev/ttyACM0 flash
python3 tests/stage5_portal_test.py --ip 192.168.4.1
# 5. 烧回正式固件
idf.py reconfigure -DPORTAL_TEST_AUTO_CONFIG=OFF && idf.py build && idf.py -p /dev/ttyACM0 flash
```

---

## 四、测试固件编译说明

### 正式固件（无自动配网）

```bash
idf.py build
# 产物：build/bikepower-c.bin（约 1MB）
```

### Release 固件（合并单文件 + 随附文档）

```bash
./scripts/build_all.sh          # 默认编译 generic + m5stamp
./scripts/build_all.sh generic  # 仅编译通用版
```

`scripts/build_all.sh` 是分发固件的标准入口。脚本会先运行 host 单测、mock 配网页保存测试、3 分钟核心流程耐久测试和静态门禁，再生成 `build/release/` 下的双板型合并固件、`.sha256`、`manifest.txt`、`test-report.md`、`core-flow-endurance.md`、`user-manual.md`、`images/` 与 release 总包 zip。

`test-report.md` 必须如实记录编译、host、mock、核心流程耐久、静态门禁、真机和人工项结果；未执行的烧录、BLE、配网、按钮或 LED 验证必须写明原因，不能按通过处理。`user-manual.md` 由 `docs/user/` 生成，更新固件交互时必须先同步源文档。总包命名为 `bikepower-c-release-v<版本>-stable-fix-g<GitID>.zip`，用于一次性分发两个板型固件和随附文档。

若 `tests/reports/device-acceptance-<板型>.json` 存在且匹配当前版本，构建脚本会复制到 `build/release/evidence/`，同时写入 manifest、测试报告和总包。无设备环境允许不带证据完成本地构建；`publish_gitee_release.py` 正式双板发布前会严格阻断任一板型证据缺失、Git ID 不一致或 P0 未通过。

### 测试固件（带自动配网，用于配网门户自动化测试）

```bash
idf.py build -DPORTAL_TEST_AUTO_CONFIG=ON
# 启动后 BLE 就绪 3s 自动触发配网，便于 stage5_portal_test.py 等脚本全自动验证
```

> `PORTAL_TEST_AUTO_CONFIG` 是 CMake option，默认 OFF。正式发版编译不带此选项，测试编译传 `-DPORTAL_TEST_AUTO_CONFIG=ON`。两者代码一致，仅测试固件多一个启动自动配网钩子。

> 切换 option 后必须 `idf.py reconfigure` 或删 `build/CMakeCache.txt`，否则缓存不刷新。`scripts/flash_latest.sh` 已在 build 前自动执行 reconfigure，确保 `BIKEPOWER_GIT_SHA` / `BIKEPOWER_BUILD_TIME` 不沿用旧 CMake 缓存；若 `build_all.sh` 后残留 `sdkconfig.board.tmp` 缓存，脚本会先清理 `build/CMakeCache.txt` 再重新配置。

---

## 五、常见问题

### Q1：nmcli 连接 BikePower-XXXXXX 报"缺少属性"

使用 `nmcli connection add` 显式创建连接而非 `nmcli device wifi connect`：

```bash
nmcli connection add type wifi ifname wlp1s0 con-name BikePowerTest \
  802-11-wireless.ssid "BikePower-A1B2C3" \
  802-11-wireless-security.key-mgmt wpa-psk \
  802-11-wireless-security.psk "12345678"
nmcli connection up BikePowerTest ifname wlp1s0
```

上面的 `BikePower-A1B2C3` 只是示例，实际 SSID 以后缀不同为准。

### Q2：串口权限拒绝

```bash
sudo usermod -aG dialout $USER
# 重新登录或 newgrp dialout 生效
```

### Q3：测试固件不自动开配网

确认编译时传了 `-DPORTAL_TEST_AUTO_CONFIG=ON`。若之前编译过正式版，需清除 CMake 缓存：

```bash
rm -f build/CMakeCache.txt
idf.py reconfigure -DPORTAL_TEST_AUTO_CONFIG=ON
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
