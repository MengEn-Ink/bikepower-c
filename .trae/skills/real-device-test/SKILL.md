---
name: "real-device-test"
description: "ESP32-C3 ESP-IDF 固件真机测试流程。变更分类智能跳过测试。Invoke when user says '真机测试'/'开始测试'/'部署测试'/'跑测试'/烧录/编译."
---

# 真机测试 Skill（ESP-IDF 版）

## 变更分类（智能跳过不必要测试）

```
用户说"提交/commit/推送/改好了" →
  Step 1: 检查变更文件类型
  Step 2: 判定分类 A/B/C/D
  Step 3: 执行对应等级测试
  Step 4: 通过后允许提交 → 必须立即 git push
```

| 分类 | 变更类型 | 示例 | 测试范围 |
|------|---------|------|---------|
| A | 纯文档/规则 | docs/, .trae/rules/, .trae/templates/, .trae/README.md | 跳过真机，仅检查文档一致性 |
| B | 领域层逻辑 | components/bikepower_domain/*.c | host 单测（test/test_domain），可跳过烧录 |
| C | 非硬件 host 可测模块 | 纯算法/数据结构 | host 单测 |
| D | 硬件相关 | main/ble_bikepower.c, button.c, led.c, app_state.c, app_main.c, radio_ctrl.c, wifi_portal.c | 完整真机流程 |

## 当前测试资产

- `test/test_domain`：领域层 host 单测
- `tests/test_portal_config_save.py`：mock 配网页保存回显测试
- `tests/test_core_flow_endurance.py`：3 分钟核心流程耐久测试（按钮/心率/网页编辑/模板/关闭 WiFi）
- `tests/serial_capture.py`：串口日志抓取
- `tests/ble_selftest.py`：BLE 五服务 + FTMS ERG 自测
- `tests/ble_config_e2e.py`：BLE 五组全量配置读写 + NVS 重启持久化 + GATT 重建 + 原配置恢复
- `tests/stage3_conn_test.py`：连接事件驱动状态机验证
- `tests/stage5_portal_test.py`：配网门户 HTTP 自测
- `scripts/tools/generate_release_docs.py`：固件随附测试报告 + 用户文档生成
- `scripts/build_all.sh`：双板型 release 产物构建与总包生成

## 完整真机流程（分类 D）

1. **编译前门禁**：`make -C test && ./test/test_domain`、`python3 tests/test_portal_config_save.py`、`python3 tests/test_core_flow_endurance.py`、相关静态检查必须通过。
2. **编译**：`idf.py build`，确认 0 error。
3. **烧录**：`idf.py -p <port> flash`（串口自动扫描）。
4. **串口确认**：`idf.py -p <port> monitor` 或 `tests/serial_capture.py`，检查启动日志（任务启动、state、空闲堆）。
5. **BLE 自测**：`python tests/ble_selftest.py`（五服务 + 四特征通知 + FTMS ERG 收敛）。
6. **配置服务自测**：`python3 tests/ble_config_e2e.py --port <port> --board <板型>`（五组读取/修改、错误路径、NVS 重启、GATT 重建和原配置恢复）。
7. **状态机自测**：`python tests/stage3_conn_test.py`（连接事件驱动状态迁移）。
8. **配网自测**：涉及 WiFi 门户时运行 `python tests/stage5_portal_test.py`。
9. **物理验证**（人工）：按钮短/中/长按 + 二次确认 + LED 灯效（远程无法触发，需用户配合）。
10. **报告产出**：每次生成可分发固件时，`build/release/` 必须同时包含双板型 merged bin、`.sha256`、`manifest.txt`、`test-report.md`、`core-flow-endurance.md`、`user-manual.md`、`images/` 与 release 总包 zip；测试未执行项必须写明原因，不得默认为通过。

## 通过标准

| 测试项 | 通过标准 |
|--------|---------|
| 编译 | idf.py build 0 error |
| 任务启动 | 串口出现 LED/STATE/BTN 任务日志，state=ST_BLE_ACTIVE |
| 内存 | 空闲堆 > 80KB（基线约 200-220KB） |
| BLE 五服务 | bleak 发现 CPS/HRS/CSC/FTMS/DIS |
| FTMS ERG | SetTargetPower 后功率收敛到目标 |
| 连接事件 | 连接 connected=1 / 断开 connected=0 |
| 配网门户 | 主页与关键 HTTP 路由行为符合 `stage5_portal_test.py` 预期 |
| 物理按钮 | 人工确认（PROGRESS.md 待确认清单） |
| 随附文档 | 固件产物目录含双板型 merged bin、`.sha256`、`manifest.txt`、`test-report.md`、`core-flow-endurance.md`、`user-manual.md`、`images/`、release 总包 zip |

## 注意事项

- macOS 无 `timeout` 命令，串口超时控制用纯 Python。
- 不用 heredoc 跑 Python（环境注入污染），写脚本文件再运行。
- 测试通过才允许提交，结果记录到 CHANGELOG.md 和随固件生成的 `test-report.md`。
- 用户手册变更必须同步到 `docs/user/02-user-manual.md`；固件产物中的 `user-manual.md` 由 `scripts/tools/generate_release_docs.py` 从 `docs/user/` 生成。
