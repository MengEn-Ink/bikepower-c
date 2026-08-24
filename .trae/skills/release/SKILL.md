---
name: "release"
description: "BikePower C 发版流程。用于准备版本发布、检查测试与产物、打 tag 和整理 Release 说明。"
---

# 发版 Skill（BikePower C）

## 适用场景

- 用户要求"发版""准备 release""打 tag""出 v3.0.x""创建发行版"
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
5. **mock/静态门禁**
- 运行 `python3 tests/test_portal_config_save.py`
- 运行 `python3 tests/test_core_flow_endurance.py`
- 运行 `check_config_consistency.py`、`check_portal_js.py`、`check_portal_structure.py`、`check_nvs_schema.py`、`check_tasks_wdt.py`、`check_board_desc.py`
6. **真机测试**
- 按 `.trae/rules/workflows/device-testing.md` 执行
- 至少包含串口观察、`tests/ble_selftest.py`
- 涉及配网时补 `tests/stage5_portal_test.py`
7. **测试报告**
- 生成并更新 `build/release/test-report.md`
- 写清编译、host、mock、3 分钟核心流程耐久、静态门禁、真机、人工项的结果
- 未执行项必须写明原因和责任人，不得默认通过
8. **用户手册**
- 更新 `docs/user/02-user-manual.md` 与相关 quickstart/hardware 文档
- 生成 `build/release/user-manual.md`，随固件一并分发
9. **核对版本号**
- `bikepower_config.h`、DIS 固件版本、CHANGELOG、tag 名必须一致
10. **文档同步**
- 对照 `.trae/rules/best-practices.md`
- 逐项检查 README.md / PROGRESS.md / docs/ 是否与代码一致
- 更新不一致的文档
11. **提交并推送**
- 提交所有变更（代码 + 文档 + CHANGELOG）
- 推送到远程
12. **打 tag**
- 使用非交互命令创建 `vMAJOR.MINOR.PATCH`
- 推送 tag
13. **生成双版本合并固件、随附文档与总包（强制，不可拆开）**
- **硬性要求**：发行版固件必须是合并后的单一 `.bin`，禁止分发分散的 bootloader / partition-table / app 三个文件。
- **硬性要求**：固件产物目录必须同时包含 generic-c3 与 m5stamp-c3 两个合并固件、`.sha256`、`manifest.txt`、`test-report.md`、`core-flow-endurance.md`、`user-manual.md`、`images/` 和 release 总包 `.zip`。
- 推荐使用 `scripts/build_all.sh` 统一生成产物；该脚本会在构建前跑 host/mock/3 分钟核心流程耐久/静态门禁，并生成测试报告、用户文档副本和可直接分发的总包。
- 使用 `esptool.py merge_bin` 将 bootloader + partition-table + app 合并为单一 `.bin`
- 合并命令：
  ```bash
  esptool.py --chip esp32c3 merge_bin \
    --flash_mode dio --flash_freq 80m --flash_size 4MB \
    --output bikepower-c-generic-c3-vX.Y.Z-stable-fix-gGitID.bin \
    0x0 build/bootloader/bootloader.bin \
    0x8000 build/partition_table/partition-table.bin \
    0x10000 build/bikepower-c.bin
  ```
- 用户烧录命令（单文件）：
  ```bash
  esptool.py --chip esp32c3 -b 460800 write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x0 bikepower-c-generic-c3-vX.Y.Z-stable-fix-gGitID.bin
  ```
- 总包命名：`bikepower-c-release-v<版本>-stable-fix-g<GitID>.zip`，包内必须包含两个板型固件、校验文件、manifest、测试报告、3 分钟核心流程报告、用户手册和截图目录。
14. **创建 Gitee Release**
- 从 `.env` 读取 `GITEE_TOKEN`
- 通过 `python3 scripts/tools/publish_gitee_release.py` 创建 Release，tag_name 与版本号一致
- 发布前先运行 `python3 scripts/tools/publish_gitee_release.py --dry-run` 确认附件清单和目标提交
- Release 说明必须包含：
  - **变更内容**：新增/修复/改进，逐条列出
  - **测试报告**：编译结果、host 单测结果、真机测试结果、内存状态
  - **烧录命令**：单文件合并固件烧录命令
  - **适配开发板**：当前支持的开发板和引脚映射
- 上传 release 总包（`bikepower-c-release-v<版本>-stable-fix-g<GitID>.zip`）作为主附件
- 同时上传两个合并固件、对应 `.sha256`、`manifest.txt`、`test-report.md`、`core-flow-endurance.md`、`user-manual.md`
15. **清理临时文件**
- 删除 release/ 目录和临时固件包

## 检查清单

- `CHANGELOG.md` 已更新
- `PROGRESS.md` 阶段状态与实际一致
- README.md 参数值与 config 头文件一致
- `docs/user/02-user-manual.md` 与当前固件功能一致
- `build/release/test-report.md` 已生成且含未执行项说明
- `build/release/core-flow-endurance.md` 已生成且核心流程耐久通过
- `build/release/user-manual.md` 已生成
- `build/release/bikepower-c-release-v<版本>-stable-fix-g<GitID>.zip` 已生成且包含双板型固件、测试报告和用户手册
- `python3 scripts/tools/publish_gitee_release.py --dry-run` 已通过
- `.trae/rules/` 与 `docs/` 未引用过时脚本名、断言数或常量值
- 工作区无不应混入发版的临时文件
- commit/tag/Release 三处版本号一致
- 固件为合并单文件，用户一次烧录即可

## 禁止事项

- 未跑测试就打 release tag
- 未清理调试输出就发版
- 未更新文档就发版
- 未生成测试报告和用户手册就分发固件
- 未生成 release 总包就创建 Release
- 上传分散的多个 bin 文件（**强制合并为单文件，不可拆开分发**）
- 在用户未确认时擅自 push tag 或发布 Release
