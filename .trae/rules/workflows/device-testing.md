# 真机测试流程

## 何时执行

- 纯文档：无需真机。
- 纯领域层：host 单测即可。
- `main/` 中 BLE、WiFi、状态机、按钮或 LED 变更：必须真机。

## 顺序

```bash
make -C test run
python3 tests/operation_gate.py --level full
idf.py build
python3 tests/serial_capture.py -p <port> -d 12 --check-boot
python3 tests/ble_selftest.py
python3 tests/ble_selftest.py --profile running --control 12
python3 tests/stage3_conn_test.py -p <port>
python3 tests/stage5_portal_test.py
```

按钮、LED 和系统 Captive Portal 弹窗需要人工确认。

## 通过标准

- 启动日志无 panic、WDT、Backtrace 或 coredump。
- 空闲堆高于 80KB。
- 骑行和跑步服务、Feature、Sensor Location 与通知符合协议。
- ERG、跑步暂停/恢复/Reset 正常。
- 二次扫描和连接成功。
- 配网退出后 HTTP/DNS/SoftAP 清理，BLE 连续。
- 设备证据绑定当前板型、版本和 Git ID。

未执行项保持 `NOT_RUN`。
