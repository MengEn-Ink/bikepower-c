#!/usr/bin/env python3
"""虚拟板耗时基线：汇总历史会话的总耗时与阶段耗时，用于判断优化是否真的变快。"""
import argparse
import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPORTS = ROOT / 'tests' / 'reports' / 'virtual-board'


def load_sessions(root=REPORTS):
    if not root.is_dir():
        return []
    records = []
    for report in sorted(root.glob('*/report.json'), key=lambda p: p.stat().st_mtime):
        try:
            data = json.loads(report.read_text(encoding='utf-8'))
        except (OSError, json.JSONDecodeError):
            continue
        records.append({
            'session': report.parent.name,
            'status': data.get('status', '?'),
            'duration': data.get('duration_seconds'),
            'git': data.get('expected_git', '?'),
            'security': bool(data.get('security_requested')),
            'running': bool(data.get('running_requested')),
            'mtime': report.stat().st_mtime,
        })
    return records


def summarize(records):
    durations = [r['duration'] for r in records if isinstance(r['duration'], (int, float))]
    if not durations:
        return {}
    return {
        'count': len(durations),
        'min': round(min(durations), 3),
        'median': round(statistics.median(durations), 3),
        'max': round(max(durations), 3),
        'latest': round(durations[-1], 3),
    }


def render(records):
    lines = ['会话                 状态 秒     安全 跑步 git']
    for r in records[-12:]:
        seconds = f"{r['duration']:.3f}" if isinstance(r['duration'], (int, float)) else '-'
        lines.append(f"{r['session']:<20} {r['status']:<4} {seconds:<6} {'Y' if r['security'] else 'N'}    {'Y' if r['running'] else 'N'}    {r['git']}")
    stats = summarize(records)
    if stats:
        lines.append('')
        lines.append(f"样本 {stats['count']}：中位 {stats['median']}s，最快 {stats['min']}s，最慢 {stats['max']}s，最近 {stats['latest']}s")
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    records = load_sessions()
    if args.json:
        print(json.dumps({'records': records, 'stats': summarize(records)}, ensure_ascii=False, indent=2))
        return 0
    print(render(records))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
