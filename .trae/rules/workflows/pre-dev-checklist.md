# 开发前检查清单（BikePower C）

<!-- stability: tier-3 (动态，随开发实践变更) -->
<!-- rule-id-prefix: PDC -->

> 开始编辑前，先完成这份最小检查。目的是确认上下文、修改范围和强约束，而不是机械打卡。

## 必做项

1. `PDC-1`：运行 `git status --short`，确认本次任务会碰到哪些文件。
2. `PDC-2`：阅读 `.trae/memory/project-context.md`，确认用户偏好、提交要求和已验证决策。
3. `PDC-3`：阅读与本次任务直接相关的规则：
- 通用至少读 `general-rules.md`、`coding-style.md`
- 涉及硬件或射频再读 `hardware-constraints.md`、`wifi-ble-mutex.md`
- 涉及 BLE 读 `domain/ble.md`
- 涉及配网读 `domain/wifi.md`
4. `PDC-4`：确认是否需要同步更新技术文档、规则、模板、测试脚本或 `CHANGELOG.md`。

## 影响面检查

5. `PDC-5`：是否触及 `components/bikepower_domain/`？如果是，至少准备 `test/build/test_domain`。
6. `PDC-6`：是否触及 `main/ble_bikepower.c` 或 BLE 常量？如果是，至少准备 `tests/ble_selftest.py`。
7. `PDC-7`：是否触及 `main/wifi_portal.c`、`main/radio_ctrl.c` 或按钮进入配网链路？如果是，至少准备 `tests/stage5_portal_test.py` 并复核射频互斥。
8. `PDC-8`：是否引入新常量、新配置项或新路由？如果是，确认落点集中且文档同步。

## 安全检查

9. `PDC-9`：中断路径是否仍只使用 `*FromISR` API。
10. `PDC-10`：BLE/GATT/定时器回调是否仍无阻塞、无大块重分配。
11. `PDC-11`：WiFi 启动前是否仍保证 BLE 已完全停止。
12. `PDC-12`：是否会与当前工作树里的用户改动冲突；若冲突，先停下来澄清。
