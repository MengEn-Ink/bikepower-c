---
name: "release"
description: "BikePower C 发版流程。用于准备版本发布、检查测试与产物、打 tag 和整理 Release 说明。"
---

# 发版 Skill（BikePower C）

## 适用场景

- 用户要求“发版”“准备 release”“打 tag”“出 v3.0.x”
- 需要核对版本号、测试结果、日志级别、CHANGELOG 和 Release 说明

## 固定流程

1. **检查 CHANGELOG**
- 确认本次版本条目完整
- 写清功能、修复、测试结果和已知限制
2. **检查日志级别**
- 清理临时调试日志
- 禁止残留 `printf`
- 统一使用 `ESP_LOGI/W/E`
3. **编译**
- 运行 `idf.py build`
- 要求 0 error
4. **host 单测**
- 运行 `make -C test`
- 运行 `./test/test_domain`
5. **真机测试**
- 按 `.trae/rules/workflows/device-testing.md` 执行
- 至少包含串口观察、`tests/ble_selftest.py`
- 涉及配网时补 `tests/stage5_portal_test.py`
6. **核对版本号**
- `bikepower_config.h`、DIS 固件版本、CHANGELOG、tag 名必须一致
7. **打 tag**
- 使用非交互命令创建 `vMAJOR.MINOR.PATCH`
8. **Release**
- 整理 Release 文案
- 说明主要变更、验证结果、未覆盖风险

## 检查清单

- `CHANGELOG.md` 已更新
- `PROGRESS.md` 阶段状态与实际一致
- `.trae/rules/` 与 `docs/` 未引用过时脚本名、断言数或常量值
- 工作区无不应混入发版的临时文件
- commit/tag/Release 三处版本号一致

## 禁止事项

- 未跑测试就打 release tag
- 未清理调试输出就发版
- 在用户未确认时擅自 push tag 或发布 Release
