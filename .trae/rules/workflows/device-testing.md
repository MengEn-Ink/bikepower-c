# 真机测试流程（BikePower C）

<!-- stability: tier-2 (稳定，随测试链路调整) -->
<!-- rule-id-prefix: DT -->

> 本流程用于所有涉及设备行为的改动。用户已明确要求当前任务不要编译，因此本文件是流程定义，不代表本次执行。

## 标准流程

1. `DT-1`：编译前门禁
- 运行 `make -C test && ./test/build/test_domain`
- 运行 `python3 tests/test_portal_config_save.py`
- 运行 `python3 tests/test_core_flow_endurance.py`
- 运行相关静态门禁：`check_config_consistency.py` / `check_portal_js.py` / `check_portal_structure.py` / `check_nvs_schema.py` / `check_tasks_wdt.py` / `check_board_desc.py`
- 要求：全部 0 failure
2. `DT-2`：编译
- 运行 `idf.py build`
- 要求：0 error，生成固件产物
3. `DT-3`：烧录
- 运行 `idf.py -p <port> flash`
- 串口不写死，按项目约定自动识别 ESP32-C3 设备
4. `DT-4`：串口观察
- 运行 `idf.py -p <port> monitor` 或 `python3 tests/serial_capture.py`
- 检查启动日志、状态机、空闲堆和异常信息
5. `DT-5`：BLE 自测
- 运行 `python3 tests/ble_selftest.py`
- 验证五服务、DIS 读值、通知和 FTMS 控制点基本行为
6. `DT-6`：连接状态测试
- 运行 `python3 tests/stage3_conn_test.py`
- 验证状态机收到连接和断开事件
7. `DT-7`：按钮与 LED 人工确认
- 人工执行短按、长按、长按进入确认窗口、二次确认
- 人工观察 LED 慢闪、常亮、快闪、熄灭是否与状态一致
8. `DT-8`：测试报告与随附文档
- 每次生成可分发固件时，必须在同一产物目录生成 `test-report.md`、`core-flow-endurance.md`、`user-manual.md` 和 release 总包
- 未执行的真机/人工项必须在报告中写明原因，不得默认通过

## 涉及配网时的附加流程

1. `DT-9`：让设备进入配网模式
- 长按至少 2 秒
- 确认窗口内再次按下
2. `DT-10`：连上热点 `BikePower-XXXXXX`
- 密码 `12345678`
3. `DT-11`：运行门户自测
- `python3 tests/stage5_portal_test.py`
- 验证主页、配置提交、倒计时、续期与关键路由行为

## 通过标准

- `DT-12`：串口无启动崩溃、WDT 超时和明显 RF 冲突日志
- `DT-13`：BLE 自测发现 CPS/HRS/CSC/FTMS/DIS 五服务
- `DT-14`：FTMS `SetTargetPower` 能驱动目标功率行为
- `DT-15`：配网改动需验证受影响的 HTTP 路由和 Captive Portal 弹窗路径
- `DT-16`：按钮和 LED 物理表现需人工确认，远程代理不得伪造结果
- `DT-17`：固件产物目录必须含双板型 merged bin、SHA256、manifest、test-report.md、core-flow-endurance.md、user-manual.md、images/ 和 release 总包 zip

## 记录要求

- `DT-18`：测试结果写入 `CHANGELOG.md`、提交说明和 `build/release/test-report.md`
- `DT-19`：如果某一项因为硬件条件无法执行，必须明确写出“未执行”与原因
