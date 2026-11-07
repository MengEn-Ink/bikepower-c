#!/usr/bin/env python3
"""证据包清点与瘦身：只删除可重建的固件镜像，报告与日志一律保留。

tests/reports 未入库，长期累积会到几百 MB；合并镜像每份约 4 MB，
report.json 已记录固件 SHA256，镜像本身不是必须留存的事实。
"""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPORTS = ROOT / 'tests' / 'reports'
PROTECTED_SUFFIXES = ('.json', '.md', '.log', '.txt', '.xml', '.zip')


def sessions(root=REPORTS):
    base = root / 'virtual-board'
    if not base.is_dir():
        return []
    return sorted((p for p in base.iterdir() if p.is_dir()), key=lambda p: p.stat().st_mtime, reverse=True)


def size_of(paths):
    return sum(p.stat().st_size for p in paths if p.is_file())


def plan(root=REPORTS, keep=3):
    """返回可删除的镜像文件；最近 keep 个会话完整保留。"""
    removable = []
    for index, session in enumerate(sessions(root)):
        if index < keep:
            continue
        report = session / 'report.json'
        if not report.is_file():
            continue
        for item in session.rglob('*.bin'):
            if item.is_file() and item.suffix.lower() not in PROTECTED_SUFFIXES:
                removable.append(item)
    return removable


def summary(root=REPORTS):
    if not root.is_dir():
        return {'total_mb': 0.0, 'files': 0, 'by_suffix': {}, 'sessions': 0}
    files = [p for p in root.rglob('*') if p.is_file()]
    by_suffix = {}
    for item in files:
        suffix = item.suffix.lower() or '(无扩展名)'
        by_suffix[suffix] = by_suffix.get(suffix, 0) + item.stat().st_size
    return {
        'total_mb': round(size_of(files) / 1024 / 1024, 1),
        'total_bytes': size_of(files),
        'files': len(files),
        'by_suffix': {k: round(v / 1024 / 1024, 1) for k, v in sorted(by_suffix.items(), key=lambda kv: kv[1], reverse=True)},
        'sessions': len(sessions(root)),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true', help='实际删除，默认只预览')
    parser.add_argument('--keep', type=int, default=3, help='完整保留的最近会话数')
    args = parser.parse_args()
    info = summary()
    print(f"证据包：{info['total_mb']} MB / {info['files']} 文件 / 虚拟板会话 {info['sessions']} 个")
    for suffix, mb in list(info['by_suffix'].items())[:6]:
        print(f'  {suffix}: {mb} MB')
    targets = plan(keep=args.keep)
    freed = size_of(targets)
    print(f'可清理镜像：{len(targets)} 个 / {freed / 1024 / 1024:.1f} MB（保留最近 {args.keep} 个会话）')
    if not args.apply:
        for item in targets[:10]:
            print(f'  预览 {item.relative_to(ROOT)}')
        return 0
    removed = 0
    for item in targets:
        try:
            item.unlink()
            removed += 1
        except OSError as error:
            print(f'  跳过 {item}: {error}')
    print(f'已删除 {removed} 个镜像，释放约 {freed / 1024 / 1024:.1f} MB；报告与日志未改动')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
