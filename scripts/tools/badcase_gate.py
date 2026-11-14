#!/usr/bin/env python3
"""Badcase 门禁：把已经发生过的事故固化成可执行约束。

每条检查对应一次真实事故，规则必须能在提交前或 CI 里自动判定，
避免同类问题再次流到远端。详细背景见 docs/dev/73-badcase-gates.md。

BC-01 提交/引用缺失资源：HTML 里 ./x.css、./x.js 必须存在且已被 git 跟踪。
BC-02 文档链接指向未跟踪文件：本地能打开、CI 才报链接不存在。
BC-03 PowerShell `Test-Path $p -and ...`：-and 会被当成 Test-Path 的参数。
BC-04 仓库 core.bare 被误置 true：本地 git 命令全部报 not a work tree。
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HTML_ASSET_RE = re.compile(r'(?:href|src)="\./([^"#?]+\.(?:css|js))"')
PS1_TESTPATH_AND_RE = re.compile(r"if\s*\(\s*Test-Path[^()]*?\s+-and\b")
MARKDOWN_TARGET_RE = re.compile(r'!?\[[^\]]*\]\(\s*(<[^>]+>|[^\s)]+)')
FENCE_RE = re.compile(r"^\s{0,3}(?:`{3,}|~{3,})")


def git(root, *args):
    result = subprocess.run(
        ['git', *args], cwd=str(root), capture_output=True, text=True
    )
    return result.returncode, (result.stdout + result.stderr).strip()


def tracked_files(root):
    rc, out = git(root, 'ls-files')
    return set(out.splitlines()) if rc == 0 else set()


def dirty_files(root):
    """工作区里有未提交改动的文件不会进 CI，检查时跳过，避免误报。"""
    rc, out = git(root, 'status', '--porcelain')
    if rc != 0:
        return set()
    dirty = set()
    for line in out.splitlines():
        path = line[2:].lstrip()
        if ' -> ' in path:
            path = path.split(' -> ', 1)[1]
        dirty.add(path.strip('"'))
    return dirty


def without_code(text):
    lines, fence = [], None
    for line in text.splitlines():
        if FENCE_RE.match(line):
            fence = not fence
            lines.append("")
        else:
            lines.append("" if fence else line)
    return "\n".join(lines)


def check_web_assets(root, tracked=None):
    errors = []
    tracked = tracked if tracked is not None else tracked_files(root)
    dirty = dirty_files(root)
    for page in sorted((root / 'tools' / 'web').glob('*.html')):
        rel_page = page.relative_to(root).as_posix()
        if rel_page in dirty:
            continue
        text = page.read_text(encoding='utf-8', errors='replace')
        for asset in sorted(set(HTML_ASSET_RE.findall(text))):
            target = (page.parent / asset).resolve()
            rel = target.relative_to(root).as_posix() if target.is_relative_to(root) else asset
            if not target.is_file():
                errors.append(f"BC-01 {page.name} 引用不存在的资源 ./{asset}")
            elif rel not in tracked:
                errors.append(f"BC-01 {page.name} 引用未跟踪资源 ./{asset}（CI 会缺文件）")
    return errors


def check_doc_links(root, tracked=None):
    errors = []
    tracked = tracked if tracked is not None else tracked_files(root)
    dirty = dirty_files(root)
    docs = sorted((root / 'docs').rglob('*.md'))
    docs += [root / name for name in ('README.md', 'AGENTS.md', 'PROGRESS.md')]
    for doc in docs:
        if not doc.is_file():
            continue
        rel_doc = doc.relative_to(root).as_posix()
        if rel_doc in dirty:
            continue
        text = without_code(doc.read_text(encoding='utf-8', errors='replace'))
        text = re.sub(r"(`+).*?\1", "", text)
        for raw in MARKDOWN_TARGET_RE.findall(text):
            target = raw.strip('<>').split('#')[0].split('?')[0]
            if not target or re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', target):
                continue
            dest = (root / target.lstrip('/')) if target.startswith('/') else (doc.parent / target)
            dest = dest.resolve()
            if not dest.is_file() or not dest.is_relative_to(root):
                continue  # 不存在交给 check_markdown_links 报
            rel = dest.relative_to(root).as_posix()
            if rel not in tracked:
                errors.append(f"BC-02 {rel_doc} 链接指向未跟踪文件 {rel}")
    return errors


def check_ps1_pattern(root):
    errors = []
    for script in sorted(root.glob('scripts/**/*.ps1')):
        for number, line in enumerate(script.read_text(encoding='utf-8', errors='replace').splitlines(), 1):
            if PS1_TESTPATH_AND_RE.search(line):
                errors.append(
                    f"BC-03 {script.relative_to(root).as_posix()}:{number} "
                    "`Test-Path ... -and ...` 缺少括号，改为 (Test-Path $p) -and (...)"
                )
    return errors


def check_git_bare(root):
    rc, out = git(root, 'config', '--get', 'core.bare')
    if rc == 0 and out.strip().lower() == 'true':
        return ["BC-04 仓库 core.bare=true，git 会报 must be run in a work tree；执行 git config core.bare false"]
    return []


def run(root):
    tracked = tracked_files(root)
    errors = []
    errors += check_web_assets(root, tracked)
    errors += check_doc_links(root, tracked)
    errors += check_ps1_pattern(root)
    errors += check_git_bare(root)
    for error in errors:
        print(f"[badcase] FAIL {error}")
    print(f"[badcase] {'FAIL' if errors else 'PASS'} {len(errors)} 错误")
    return 1 if errors else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    return run(args.root.resolve())


if __name__ == '__main__':
    raise SystemExit(main())
