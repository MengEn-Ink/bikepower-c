#!/usr/bin/env python3
"""工作区审计：把未提交改动按处置建议分类，避免每次任务重新猜哪些能提交。

分类规则只做提示，不删除、不移动任何文件。
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

SCRATCH_SUFFIXES = ('.tmp', '.bak', '.partial', '.orig', '.rej')
SCRATCH_NAMES = {'final-status.md', '=0.22,'}
TOOLING_PREFIXES = ('.agents/', '.claude/', 'agent/', '.trae/skills/')
TOOLING_NAMES = {'skills-lock.json', '.DS_Store'}
GENERATED_PREFIXES = ('__pycache__/', 'build/', 'tests/reports/', 'node_modules/')
GENERATED_SUFFIXES = ('.pyc', '.log')


def git_status(root=ROOT):
    result = subprocess.run(
        ['git', '-C', str(root), 'status', '--porcelain=v1', '-z'],
        capture_output=True, text=True, check=True,
    )
    entries = []
    chunks = iter(result.stdout.split('\0'))
    for chunk in chunks:
        if not chunk:
            continue
        code, path = chunk[:2], chunk[3:]
        if 'R' in code or 'C' in code:
            next(chunks, None)
        entries.append((code, path.replace('\\', '/')))
    return entries


def classify(code, path):
    if code != '??':
        return 'tracked-change'
    if path.endswith(SCRATCH_SUFFIXES) or path in SCRATCH_NAMES:
        return 'scratch'
    if path.startswith(TOOLING_PREFIXES) or path in TOOLING_NAMES:
        return 'tooling'
    if path.startswith(GENERATED_PREFIXES) or path.endswith(GENERATED_SUFFIXES):
        return 'generated'
    return 'review'


def audit(root=ROOT):
    groups = {'tracked-change': [], 'scratch': [], 'tooling': [], 'generated': [], 'review': []}
    for code, path in git_status(root):
        groups[classify(code, path)].append({'code': code, 'path': path})
    return groups


def render(groups):
    labels = {
        'tracked-change': '已跟踪文件的改动（需确认是否本轮提交）',
        'scratch': '临时/残渣文件（可归档，不提交）',
        'tooling': '本机工具目录（建议忽略）',
        'generated': '生成物（应由 .gitignore 覆盖）',
        'review': '待判断（可能是其他在途工作）',
    }
    lines = []
    total = sum(len(items) for items in groups.values())
    lines.append(f'工作区条目：{total}')
    for key in ('tracked-change', 'review', 'scratch', 'tooling', 'generated'):
        items = groups[key]
        lines.append(f'\n[{key}] {len(items)} 项 - {labels[key]}')
        for item in items[:12]:
            lines.append(f'  {item["code"]} {item["path"]}')
        if len(items) > 12:
            lines.append(f'  ... 其余 {len(items) - 12} 项')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json', action='store_true', help='输出JSON')
    parser.add_argument('--strict', action='store_true', help='存在待提交或待判断条目时以非零退出')
    args = parser.parse_args()
    groups = audit()
    if args.json:
        print(json.dumps(groups, ensure_ascii=False, indent=2))
    else:
        print(render(groups))
    if args.strict:
        blocking = groups['tracked-change'] + groups['review'] + groups['generated']
        if blocking:
            print(f'\n[workspace-audit] FAIL 仍有 {len(blocking)} 项需要处理', file=sys.stderr)
            return 1
        print('\n[workspace-audit] PASS 工作区达到可提交基线')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
