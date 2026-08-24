---
name: real-device-test
description: ESP32-C3 ESP-IDF 固件真机测试流程。变更分类智能跳过测试。
---

# 真机测试

## 分类

- 文档/规则：文档一致性。
- 领域层：host 单测。
- `main/` 硬件行为：完整真机流程。

## 软件门禁

```bash
make -C test run
python3 tests/operation_gate.py --level full
idf.py build
```

## 真机

```bash
python3 tests/serial_capture.py -p <port> -d 12 --check-boot
python3 tests/ble_selftest.py
python3 tests/ble_selftest.py --profile running --control 12
python3 tests/stage3_conn_test.py -p <port>
python3 tests/stage5_portal_test.py
```

按钮、LED 和系统 Captive Portal 弹窗需人工确认。

## 记录

- 证据写入 `tests/reports/device-acceptance-<板型>.json`。
- 证据必须匹配当前板型、版本和 Git ID。
- 未执行项保持 `NOT_RUN`。
- 发布产物中的测试报告与用户手册由 release 脚本生成。
