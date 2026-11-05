# 源码清理与验证报告

> 归档于 2026-11-05：上一轮清理证据，保留原提交、测试和待实物项。本轮结果见 [仓库治理报告](../dev/31-repository-hygiene-report.md)。

日期：2026-11-05。范围：当前三设备桥接候选、对应测试资产、用户手册、Code Wiki 和治理规则。固件版本 v4.2.1，配置 schema v14。

## 清理与修复

- 删除已由主备协调层替代的单设备重试常量、没有生产调用的 `ble_power_bridge_portal_stop()`，以及运行时只写不读的字段。
- 保留原会话状态编号；补充主任务、跨任务邮箱、快照读取和最近成功回调的接口契约。
- 为 host 桥接测试二进制及 Windows 产物补充忽略规则。
- 修复 `parse_bridge_address()` 对短字符串直接读取第 13 个字符的问题；旧 schema v13 空地址输入经精确长度分配后，由 ASan 稳定复现 `heap-buffer-overflow`，补回长度检查后安全拒绝且不污染输出。
- 修复八组配置 E2E 在空主设备后写入备选的夹具错误：先补有效主设备，保持配置来源与校正模式；新增身份避免重复，已有备选只改名称，防止固件压紧或去重后出现虚假的回读失败。
- 统一八组配置与三设备主备语义，归档已执行的运动模式与主备实施计划；原计划检查框不作为执行证据。
- 用户手册按钮阈值与生产常量对齐：短按小于 500ms 降功率，按住 500ms–2s 连续增功率，长按超过 2s 后在 3s 确认窗口内再次短按。

| 级别 | 问题 | 修复及验证 |
|------|------|------------|
| 高 | 未提交候选的旧配置短地址越界读取 | `config_store.c` 补长度检查；`test_domain.c` 精确分配短 JSON，ASan/UBSan 验证 |
| 中 | 八组配置测试夹具存在空槽位和重复身份风险 | `ble_config_e2e.py` 维持连续且唯一的身份；离线回归覆盖空列表、已有主设备和完整三设备列表 |
| 中 | 用户手册按钮方向与阈值过时 | 对照 `bikepower_config.h`、按钮任务及状态机修正文案 |
| 低 | 单设备重试残留、接口契约不足与当前文档语义过时 | 删除无生产调用的代码，补充所有权注释，同步 Code Wiki、规则、测试入口并归档实施计划 |

## 本轮验证

| 验证 | 结果与边界 |
|------|-----------|
| 领域层 ASan/UBSan | `make -C test -B test_domain CFLAGS='-std=c11 -Wall -Wextra -fsanitize=address,undefined -I../components/bikepower_domain/include'` 后运行 `./test/test_domain`：6178 通过、0 失败 |
| 生产会话/扫描/运行时 ASan/UBSan | `python3 tests/test_bridge_session.py`：全部通过 |
| 八组 E2E 离线协议 | `python3 tests/test_ble_config_e2e.py`：通过；空列表补主设备、合成地址去重、已有三设备身份保留、非法值与 GATT 切换覆盖通过 |
| 完整 operation gate | 36 项通过，含 3 分钟 15 轮耐久、生产压缩门户 320/390/1440、HTTP/Pages 异常和 ASan/UBSan；报告 `/tmp/bikepower-cleanup-full-20261105.md`。该聚合运行先于短地址及 E2E 夹具修复，修改项已分别复验 |
| 质量门禁 | `python3 scripts/tools/quality_gate.py --no-build`：111 PASS、2 WARN、0 FAIL；host 6178 断言。两项 WARN 为工作区未提交及由此跳过的最近提交 whitespace 检查，构建另行验证 |
| 双板独立编译 | ESP-IDF v5.5.4：generic-c3、m5stamp-c3 均通过；app `0x113ac0`，factory 剩余 72%；两板均为 WARN 日志、WiFi/BLE 共存、`PORTAL_TEST_AUTO_CONFIG=OFF` |
| 门户视觉复核 | `portal_bridge_browser.py --screenshot-dir /tmp/bikepower-cleanup-visual-20261105`：320/390/1440 回归通过，生成 18 张场景截图；人工查看手机三设备、手机降级及桌面三设备截图，浅色文案/控件可读，无横向溢出 |
| 文档与覆盖矩阵 | 源码事实摘要一致；Markdown 链接 56 文档、0 错误；30 个关键操作均有门禁记录 |
| 文档治理门禁 | `python3 scripts/tools/quality_gate.py --level docs`：78 PASS、2 dirty WARN、0 FAIL |
| 当前候选真机验收 | `NOT_RUN`：未检测到 ESP32 USB 串口，也未具备多台真实 CPS 来源 |

双板使用独立 `/tmp/bikepower-cleanup-<generic|m5>-build` 目录和对应 sdkconfig，复用仓库默认配置并显式选择板型；未修改根 sdkconfig。最后一次编译日志分别为 `/tmp/bikepower-cleanup-generic-build.log` 和 `/tmp/bikepower-cleanup-m5-build.log`，质量门禁日志为 `/tmp/bikepower-cleanup-quality-20261105.log`。

| 本地编译校验对象 | app SHA256 |
|------------------|------------|
| generic-c3 | `454ec6757396054725edaee3f71874659a2456a0dfb92f088d96658c00064609` |
| m5stamp-c3 | `887e77420ee945e6d050c17e0768795e2d7b8d2fc04aacf78c93e7a7d02369d4` |

这些 app 文件仅供本轮编译校验，绑定编译时 `255f82a` 加未提交工作树，不是发布产物。先前生成的本地候选包不代表清理后的当前镜像。

## 提交批次

1. 工程清理：`32eba03` 已推送，仅补测试二进制忽略规则。
2. 源码与配套测试：`aa1df06` 已推送，包含当前三设备主备实现、残留清理、短地址修复、八组夹具和相关门禁。
3. 文档治理：本文所属 `docs(maintenance)` 提交同步用户手册、Code Wiki、规则、索引、历史计划和本报告；最终哈希与远端 CI 结果随交付回复记录。

每批采用指定路径暂存、新建提交并立即推送；最终提交的 GitHub CI 单独核验，不将本地门禁等同于远端结果。

## 实物与发布边界

多设备主备接管、重启后最近成功设备自动连接、全部离线后的模拟连续性、每 60 秒低频恢复、三种校正的真实 0W，以及上游功率计、下游 App、WiFi 共存与最低堆水位均为 `NOT_RUN`。

旧版本或其他 Git/镜像的真机 PASS 不覆盖当前候选。自动化与编译通过仅允许源码提交验证；发布、打 tag 和分发仍须补齐当前镜像的实物证据。
