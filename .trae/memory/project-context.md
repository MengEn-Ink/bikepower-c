# BikePower C 项目记忆

## 项目

- C + ESP-IDF v5.5.4 + NimBLE，ESP32-C3。
- 当前固件 v4.1.1，配置 schema v11。
- 支持 generic-c3 与 m5stamp-c3。

## 用户偏好

- 简体中文沟通。
- 先分析影响和方案，再实施。
- 代码、文档、门禁与真机证据必须一致。
- 无硬件时明确记录 `NOT_RUN`，不复用旧 Git ID 证据。
- 参数、功能或模块移除要跨领域层、NVS、API、网页和测试彻底清理。

## 当前决策

- BLE 是核心，WiFi 仅用于用户主动触发的 SoftAP 配网。
- WiFi 启停只经过 `radio_ctrl`；普通配网保持 BLE 在线。
- 设备内 OTA 与家庭 WiFi已移除。
- 骑行 profile：CPS/HRS/CSC/FTMS/DIS。
- 跑步 profile：RSC/HRS/FTMS/DIS；不支持坡度。
- ERG 和跑步启停是会话态，不写 NVS。
- NimBLE 回调只做轻量校验、打标或投递；清理由主任务执行。
- `CHANGELOG.md` 是唯一历史记录；当前文档只放 `docs/user` 与 `docs/dev`。

## 验证

- 领域层：`make -C test run`
- 提交前：`python3 tests/operation_gate.py --level full`
- 全项目：`python3 scripts/tools/quality_gate.py`
- 真机：启动日志、骑行 BLE、跑步控制、二次重连、配网、按钮与 LED

## 凭证

- Gitee token 只放 `.env` 的 `GITEE_TOKEN`，不得写入代码、日志或提交。
