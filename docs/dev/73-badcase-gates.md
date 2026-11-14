# Badcase 门禁：已发生事故的固化约束

本文把 2026-11-14 ～ 2026-11-15 这轮发布收尾里真实踩到的坑逐条固定下来：能自动判定的进 `scripts/tools/badcase_gate.py`（CI 与本地 pre-push 都会跑），只能靠流程约束的写成提交纪律。目标不是写检讨，而是让同类问题在下一次提交前被拦下。

## 自动检查（CI + pre-push 双向执行）

| 编号 | 事故 | 症状与代价 | 现在的规则 | 落点 |
|---|---|---|---|---|
| BC-01 | 提交夹带未提交的网页资源 | `ble-monitor.html` 引用了 `./listing-share.css`，但该文件仍在本地未提交；CI quick-harness 报 `ENOENT … listing-share.css`，整轮红（run 43609683142） | HTML 中 `./*.css`、`./*.js` 必须文件存在且已被 git 跟踪 | `badcase_gate.check_web_assets` |
| BC-02 | 文档链接指向未跟踪文件 | `PROGRESS.md` 链到未提交的 `docs/dev/72-*.md`，本地能打开、CI docs 报「链接不存在」（run 43608742276） | Markdown 里的本地链接目标必须已被 git 跟踪；只检查已提交状态，避免未提交 wip 误报 | `badcase_gate.check_doc_links` |
| BC-03 | PowerShell `Test-Path` 参数陷阱 | `if (Test-Path $p -and -not $x)` 把 `-and` 当 `Test-Path` 的参数，脚本行为异常（`scripts/flash_latest.ps1:70`） | 禁止 `if (Test-Path … -and …)` 无括号写法，必须 `if ((Test-Path $p) -and (…))` | `badcase_gate.check_ps1_pattern` |
| BC-04 | 仓库 `core.bare` 被误置 true | 执行后所有 `git status / commit / stash` 报 `this operation must be run in a work tree`，一度阻塞提交 | `git config --get core.bare` 必须不是 `true`；命中时提示修复命令 | `badcase_gate.check_git_bare` |

运行方式：

```
python scripts/tools/badcase_gate.py          # 单独跑
python scripts/tools/pre_push_gate.py run     # 本地门禁（含本约束）
python scripts/tools/quality_gate.py --level docs   # CI docs job 同款
```

## 流程纪律（无法自动判定，但必须遵守）

| 编号 | 事故 | 规则 |
|---|---|---|
| BC-05 | 一个提交把共享工作区里未提交的售卖面板标记与 PROGRESS 段落一起带进仓库（提交 `136f7c0`），随后又用 `6f3abe7` 回退 | 只用**显式文件白名单** `git add -- <path>`；禁止 `git add -A` / `git add <dir>`；提交前 `git diff --cached --stat` 与逐文件 diff 核对一遍「这些文件是否都属于本任务」 |
| BC-06 | 修复时直接覆盖了用户本地正在改的 `tools/web/ble-monitor.html` | 动共享工作区里别人/自己未提交的文件前，先复制到 `%TEMP%` 或 `git stash` 留底并说明路径；更优先的做法是在独立 `git worktree` 里改，主工作区只做展示 |
| BC-07 | 本地门禁因未提交 wip 漂移而「本地红/CI 绿」或反向，反复浪费轮次 | push 前在**索引干净状态**跑门禁（临时 stash wip 或独立 worktree）；结论只认远端 run 结果，本地绿不等于 CI 绿 |
| BC-08 | ubuntu 虚拟板 `bridge-switch: TimeoutError` 偶发失败，与当次改动无关 | 先判断失败项是否被本次改动触及；无关的偶发失败重跑并记录 run id，不为了「让它绿」去改代码或跳过检查 |

## 维护约定

- 新增一条 badcase 时：先在本文登记事故与规则，再在 `badcase_gate.py` 增加检查（附正例/反例单测），最后接进 `quality_gate.py` 与 `pre_push_gate.py`，三处缺一不可。
- 规则只写**可判定**的内容；主观判断（例如「这个提交是否越界」）留在流程纪律里，不伪装成自动检查。
- 删除或放宽任何一条规则，必须在本文留下原因与替代检查，不能静默移除。
