# 开发效率优化落地记录

2026-11-08 记录本轮九项优化的实际改动、验证证据与剩余缺口。只写已发生的事实，未验证的能力不写成已通过。

## 结论

九项优化中八项已落地并有可复现证据；「macOS 实机」与「SC indication 实物确认」仍是明确边界，虚拟板只覆盖到协议与配置层。CI 已恢复全绿：`docs` 9 秒、`virtual-board (ubuntu-24.04)` 291 秒、`virtual-board (macos-15)` 365 秒、`quick-harness` 271 秒（四作业并行，总墙钟约 6 分钟）。

## 九项落地情况

| 优化项 | 落地内容 | 验证证据 | 边界 |
| --- | --- | --- | --- |
| CI 增加 macOS 矩阵与缓存 | `virtual-board` 改为 `ubuntu-24.04` + `macos-15` 矩阵；缓存 `~/.espressif` 与虚拟环境 | macos-15 完整跑完 25 项检查并产出报告 | `macos-14` arm64 托管镜像已被 GitHub 弃用，报 `configuration not supported`，因此升到 15；缓存首次未命中、已写入，命中效果需下一轮观测 |
| 工作区脏改动归位 | `workspace_audit.py` 只读五分类；`.gitignore` 补 scratch/tooling 段 | 审计 35 → 34 项（tracked-change 1、review 32、tooling 1、scratch 0、generated 0） | `design-demos` 下 128 个已跟踪文件，不能整体忽略；32 项 review 归属未确认，未擅自移动 |
| 收敛单一入口 | `dev.ps1` 增加 `sim`/`sim-setup`/`sim-doctor`/`sim-build`/`sim-test`/`sim-debug`/`sim-stats`/`accept` | PowerShell 5.1 与 7 双端解析、`accept -DryRun` 双端输出一致 | `accept` 未在真机执行过，只验证了命令构造 |
| pre-push 门禁 | `pre_push_gate.py`：提交/暂存区空白、Markdown 链接、文档门禁、两项单测，共 6 项 | 手动执行 6/6，用时 2.0–6.7 秒；推送日志中可见门禁先跑后推送 | 钩子只在本地 `.git/hooks`，不入库；解释器不可用时明确跳过 |
| 真机验收批处理 | `dev.ps1 accept` 自动探测串口、按板型映射 `generic-c3`/`m5stamp-c3`、调 `operation_gate.py --level device` 并落报告 | `-DryRun` 打印完整命令与报告路径 | 缺硬件，未跑真机 |
| 文档事实自动生成 | 核实 `doc_facts.py` 早已由 `quality_gate.py --level docs` 调用 | `PASS 源码摘要一致`；文档门禁 79 通过 / 2 警告 / 0 失败 | 无需新增脚本 |
| 证据包瘦身 | `report_maintenance.py` 只清理最近会话之外、可由源码重建的镜像 | 314.8 MB / 1421 文件 → 清理后 146.7 MB / 1339 文件（释放 168.2 MB）；当前 155 MB / 1360 文件 | 两个 20.7 MB 的 `.bundle` 属人工证据，工具刻意不碰 |
| 耗时基线 | `timing_summary.py` 汇总会话耗时，`sim-stats` 入口 | 28 个样本，中位 38.575 秒，最快 0.003 秒，最慢 117.788 秒 | 含失败会话，样本非同一固件版本 |

## 期间修掉的四个真实阻断

1. `virtual_board_e2e.py` 的 `run_protocol` 漏传 `record_not_obtained`，SC 分支触发 `NameError`，两个虚拟板作业在 21 项检查后异常退出。
2. 并发提交在生产代码新增 `BLE_GAP_EVENT_ENC_CHANGE` 分支，host 契约桩只定义到 `BLE_GAP_EVENT_NOTIFY_TX`，`-Werror` 下三个契约测试编译失败。
3. Git Bash 的 `python3` 指向 Microsoft Store 空壳别名，执行退出 49，pre-push 把推送静默挡掉；钩子改为逐个候选执行 `import sys` 探测。
4. `889c04b` 重构 `connect()` 时把 `winrt={"use_cached_services": False}` 降级为默认关闭，配置端 E2E 丢掉了禁用 Windows BLE 服务缓存的保证；同时新加的 SC indication 检查在虚拟板取不到时直接让作业退出 1，与报告 `status=PASS` 自相矛盾。

## 剩余缺口

- macOS 至今无实机验证，虚拟板只证明协议与配置行为。
- `sc_indication_after_rebuild` 在虚拟链路取不到，报告记为 NOT_OBTAINED，需实物 directACK 确认。
- CI 缓存命中率、`accept` 真机路径、`.bundle` 清理策略均未取得新证据。
- 32 项 review 类未跟踪文件归属未确认，保持原样。

## 复现命令

```powershell
.\scripts\dev.ps1 sim                # 本机完整虚拟回归
.\scripts\dev.ps1 sim-doctor         # 模拟器与依赖版本
.\scripts\dev.ps1 sim-stats          # 耗时基线
.\scripts\dev.ps1 accept -Port COMx  # 真机验收批处理
python scripts\tools\pre_push_gate.py run
python scripts\tools\workspace_audit.py
python scripts\tools\report_maintenance.py
```
