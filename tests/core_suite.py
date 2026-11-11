#!/usr/bin/env python3
"""核心用例登记及重复执行；argv直传，保存每次失败，实物资源串行。"""
import argparse
import importlib.util
import os
import subprocess
import sys
import time
from pathlib import Path
from core_harness import DeviceLease, write_json

ROOT = Path(__file__).resolve().parents[1]
CASES = {
    'core-model': ('host', 'test_core_harness.py', 30),
    'core-orchestration': ('host', 'test_core_device_e2e.py', 30),
    'transport': ('host', 'test_ble_e2e_transport.py', 30),
    'soak-negative': ('host', 'test_soak_harness_fake.py', 180),
    'mobile': ('browser', 'test_portal_mobile_layout.py', 180),
    'portal-exception': ('browser', 'portal_exception_browser.py', 180),
    'portal-bridge': ('browser', 'portal_bridge_browser.py', 180),
}

def missing_dependencies(selected):
    required = set()
    if 'transport' in selected:
        required.update(('serial', 'bleak'))
    if any(value[0] == 'browser' for value in selected.values()):
        required.add('playwright')
    return sorted(name for name in required if importlib.util.find_spec(name) is None)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--layer', choices=('host','browser','all'), default='host')
    p.add_argument('--case', choices=tuple(CASES))
    p.add_argument('--repeat', type=int, default=2)
    p.add_argument('--report-dir', type=Path, required=True)
    args = p.parse_args()
    if args.repeat < 1: p.error('repeat must be >=1')
    selected = {args.case: CASES[args.case]} if args.case else {
        key:value for key,value in CASES.items() if args.layer == 'all' or value[0] == args.layer}
    args.report_dir.mkdir(parents=True, exist_ok=True)
    if (args.report_dir/'suite.json').exists():
        p.error('报告目录已有suite.json；请选择新目录，保留历史失败')
    report = {'scope': 'host/browser; not device or target-app acceptance', 'attempts': [], 'status': 'FAIL'}
    missing = missing_dependencies(selected)
    if missing:
        report['precondition_errors'] = ['缺少依赖: ' + ', '.join(missing)]
        write_json(args.report_dir/'suite.json', report)
        print(report['precondition_errors'][0], flush=True)
        return 2
    with DeviceLease(ROOT/'build/harness-locks', 'core-suite'):
        for iteration in range(1, args.repeat+1):
            for case_id, (layer, filename, timeout) in selected.items():
                command = [sys.executable, str(ROOT/'tests'/filename)]
                log = args.report_dir / f'{case_id}-{iteration}.log'
                started = time.monotonic()
                row = {'case': case_id, 'round': iteration, 'layer': layer, 'status': 'FAIL',
                       'command': command, 'log': log.name}
                try:
                    with log.open('w', encoding='utf-8') as stream:
                        result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                                timeout=timeout, env=dict(os.environ))
                    row['returncode'] = result.returncode
                    row['status'] = 'PASS' if result.returncode == 0 else 'FAIL'
                except (OSError, subprocess.TimeoutExpired) as exc:
                    row['error'] = type(exc).__name__ + ': ' + str(exc)
                row['seconds'] = round(time.monotonic()-started, 3)
                report['attempts'].append(row)
                write_json(args.report_dir/'suite.json', report)
                print(f'{case_id} R{iteration}: {row["status"]}', flush=True)
        report['status'] = 'PASS' if report['attempts'] and all(
            row['status']=='PASS' for row in report['attempts']) else 'FAIL'
        write_json(args.report_dir/'suite.json', report)
    return int(report['status'] != 'PASS')

if __name__ == '__main__': raise SystemExit(main())
