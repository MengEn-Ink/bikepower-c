# BikePower C 测试指南

## 常用入口

```bash
# 快速开发门禁
python3 tests/operation_gate.py --level quick

# 提交前全量软件门禁
python3 tests/operation_gate.py --level full

# 编译 + host + 全部静态检查
python3 scripts/tools/quality_gate.py

# 领域层
make -C test run

# 协议固定向量
python3 tests/test_ble_protocol_vectors.py
```

## 静态与功能检查

| 命令 | 用途 |
|------|------|
| `python3 tests/check_config_consistency.py` | 版本、常量、文档与运行时事实 |
| `python3 tests/check_nvs_schema.py` | NVS key 和 schema |
| `python3 tests/check_tasks_wdt.py` | 任务、队列和 WDT |
| `python3 tests/check_board_desc.py` | 板型描述 |
| `python3 tests/check_portal_js.py` | Portal JS 语法 |
| `python3 tests/check_portal_structure.py` | Portal 路由、DOM 与 API |
| `python3 tests/check_portal_render.py` | agent-browser 页面渲染和截图 |
| `python3 tests/check_ble_advertising_fields.py` | GAP 广播字段 |
| `python3 tests/check_ble_monitor.py` | Web Bluetooth 监控页 |
| `python3 tests/check_ble_broadcast_monitor.py` | 广播项真机脚本接线 |
| `python3 tests/check_ble_reconnect.py` | 二次扫描与二次连接契约 |
| `python3 tests/check_wifi_done_reliability.py` | WiFi 完成事件最终送达 |
| `python3 tests/check_control_event_reliability.py` | FTMS pending 事件顺序 |
| `python3 tests/check_build_isolation.py` | 临时构建配置隔离 |
| `python3 tests/check_project_hygiene.py` | 文档目录与生成物卫生 |
| `python3 tests/test_portal_config_save.py` | 配置保存 mock |
| `python3 tests/test_core_flow_endurance.py` | 核心流程耐久 |

## 真机

```bash
# 启动日志
python3 tests/serial_capture.py -p <port> -d 12 --check-boot

# 骑行 BLE、Feature、ERG
python3 tests/ble_selftest.py

# 跑步 BLE、暂停、恢复、Reset
python3 tests/ble_selftest.py --profile running --control 12

# 两次独立扫描与重连
python3 tests/stage3_conn_test.py -p <port>

# 设备处于配网态时
python3 tests/stage5_portal_test.py
python3 tests/ble_broadcast_monitor.py
```

浏览器监控：

```bash
python3 scripts/tools/serve_ble_monitor.py
```

用 Chrome/Edge 打开 `tools/web/ble-monitor.html`。首次 Web Bluetooth 授权必须人工点击。

## 证据与发布

```bash
python3 scripts/tools/device_acceptance.py validate \
  --file tests/reports/device-acceptance-generic-c3.json

python3 scripts/tools/device_acceptance.py validate --strict \
  --file tests/reports/device-acceptance-generic-c3.json

python3 scripts/tools/check_release_artifacts.py \
  --require-pushed \
  --require-device-evidence
```

证据状态只有 `PASS`、`FAIL`、`NOT_RUN`。`NOT_RUN` 不是通过；正式发布要求 generic-c3 与 m5stamp-c3 的当前版本、Git ID 和全部 P0 项均为 `PASS`。

## 目录边界

- `test/`：纯 C host 单测。
- `tests/`：Python 门禁、mock、真机和证据工具。
- `tests/reports/`：只保留当前设备验收 JSON；运行生成的 Markdown 报告不纳入源码。
- `tests/logs/`：运行日志，不纳入源码。
