#!/usr/bin/env python3
"""提交前快速门禁：把 CI 里最便宜的检查搬到本地，避免推送后等远端报错。

默认只读执行，不修改仓库；安装钩子后才在 git push 前自动运行。
"""
import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HOOK_MARKER = '# bikepower-pre-push-gate'

HOOK_BODY = """#!/bin/sh
{marker}
cd "$(git rev-parse --show-toplevel)" || exit 1
if command -v python3 >/dev/null 2>&1; then
  exec python3 scripts/tools/pre_push_gate.py run
elif command -v python >/dev/null 2>&1; then
  exec python scripts/tools/pre_push_gate.py run
else
  echo "[pre-push] 未找到python，跳过本地门禁" >&2
  exit 0
fi
""".format(marker=HOOK_MARKER)


def checks():
    return [
        ('commit 空白错误', ['git', 'diff', '--check']),
        ('暂存区空白错误', ['git', 'diff', '--cached', '--check']),
        ('Markdown 链接', [sys.executable, 'scripts/tools/check_markdown_links.py']),
        ('文档质量门禁', [sys.executable, 'scripts/tools/quality_gate.py', '--level', 'docs']),
        ('工作区审计单测', [sys.executable, 'tests/test_workspace_audit.py']),
        ('证据包维护单测', [sys.executable, 'tests/test_report_maintenance.py']),
    ]


def run_checks(root=ROOT):
    failures = []
    total = time.time()
    for name, command in checks():
        started = time.time()
        result = subprocess.run(command, cwd=str(root), capture_output=True, text=True)
        elapsed = time.time() - started
        status = 'PASS' if result.returncode == 0 else 'FAIL'
        print(f'[{status}] {name} ({elapsed:.1f}s)')
        if result.returncode != 0:
            failures.append(name)
            tail = (result.stdout + result.stderr).strip().splitlines()[-8:]
            for line in tail:
                print(f'    {line}')
    print(f'[pre-push] {len(checks()) - len(failures)}/{len(checks())} 通过，用时 {time.time() - total:.1f}s')
    return failures


def install(root=ROOT):
    hook = root / '.git' / 'hooks' / 'pre-push'
    if not (root / '.git').is_dir():
        print('[pre-push] 当前目录不是git仓库，跳过安装')
        return 1
    hook.parent.mkdir(parents=True, exist_ok=True)
    if hook.exists():
        content = hook.read_text(encoding='utf-8', errors='replace')
        if HOOK_MARKER not in content:
            backup = hook.with_suffix('.pre-bikepower')
            backup.write_text(content, encoding='utf-8')
            print(f'[pre-push] 已有钩子已备份到 {backup.name}')
    hook.write_text(HOOK_BODY, encoding='utf-8', newline='\n')
    os.chmod(hook, 0o755)
    print(f'[pre-push] 已安装 {hook}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['run', 'install'])
    args = parser.parse_args()
    if args.command == 'install':
        return install()
    if os.environ.get('BIKEPOWER_SKIP_PREPUSH'):
        print('[pre-push] BIKEPOWER_SKIP_PREPUSH 已设置，跳过')
        return 0
    return 1 if run_checks() else 0


if __name__ == '__main__':
    raise SystemExit(main())
