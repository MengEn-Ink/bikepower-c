# 测试覆盖策略（BikePower C）

<!-- stability: tier-2 (稳定，随测试资产调整) -->
<!-- rule-id-prefix: TC -->

> 本文件定义“改什么，测什么”。它不替代 `device-testing.md`，而是说明覆盖层次和最低要求。

## 覆盖分层

### `TC-1`：host 单测

- 目标：覆盖领域层纯 C 逻辑
- 入口：`test/test_domain`
- 源文件：`test/test_domain.c`
- 覆盖对象：
- `profile.c`
- `domain_rng.c`
- `hr_model.c`
- `power_engine.c`
- `config_store.c`
- 当前断言规模：`581` 条
- 适用改动：
- `components/bikepower_domain/` 下的逻辑变更
- 配置 schema、范围校验、模式计算、心率模型变更

### `TC-2`：BLE 自测

- 目标：验证对外 BLE 协议面
- 入口：`tests/ble_selftest.py`
- 覆盖内容：
- 扫描并连接 `BikePower`
- 发现五服务 `CPS/HRS/CSC/FTMS/DIS`
- 读取 DIS 厂商、型号、固件版本
- 接收功率相关通知
- 发送 FTMS 控制点并观察 ERG 行为
- 适用改动：
- `main/ble_bikepower.c`
- `app_main.c` 中 BLE 相关接线
- `bikepower_config.h` 中 BLE UUID、设备名、版本号等常量

### `TC-3`：配网自测

- 目标：验证 WiFi 门户与 HTTP 路由
- 入口：`tests/stage5_portal_test.py`
- 覆盖内容：
- 访问首页
- 读取倒计时
- 校验配网页面关键内容
- 校验续期接口
- 校验配置提交后的行为
- 适用改动：
- `main/wifi_portal.c`
- `main/radio_ctrl.c`
- 按钮进入配网的状态机链路

## 选择规则

- `TC-4`：纯治理文档或规则改动可不跑测试，但要做一致性复核。
- `TC-5`：领域层改动至少跑 `test/test_domain`。
- `TC-6`：BLE 行为改动至少跑 `tests/ble_selftest.py`，涉及设备行为时再走完整真机流程。
- `TC-7`：配网改动至少跑 `tests/stage5_portal_test.py`，并复核 BLE/WiFi 射频互斥链路。
- `TC-8`：跨层改动按最高等级执行，不能只挑便宜测试。

## 维护要求

- `TC-9`：新功能落地时必须明确归属到以上至少一层测试。
- `TC-10`：如果规则、文档或提交说明引用断言数、脚本名、服务数、路由数，必须与当前仓库保持一致。
