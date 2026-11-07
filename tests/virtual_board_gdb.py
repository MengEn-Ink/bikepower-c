#!/usr/bin/env python3
"""本机虚拟板 GDB 冒烟：真实 ELF app_main 断点、调用栈与单步；不使用硬件。"""
import argparse
import json
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'scripts/tools'))
from virtual_board import digest, verify_version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('emulator', 'firmware', 'elf'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--gdb', default='riscv32-esp-elf-gdb')
    parser.add_argument('--report-dir', type=Path, default=ROOT / 'tests/reports/virtual-board')
    args = parser.parse_args()
    args.report_dir.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix='gdb-', dir=args.report_dir))
    process = None
    source_hash = None
    error = None
    started = time.monotonic()
    with (folder / 'emulator.log').open('w', encoding='utf-8') as log:
        try:
            verify_version(args.emulator)
            source_hash = digest(args.firmware)
            image = folder / 'runtime.bin'
            shutil.copyfile(args.firmware, image)
            with socket.socket() as listener:
                listener.bind(('127.0.0.1', 0))
                port = listener.getsockname()[1]
            process = subprocess.Popen([str(args.emulator), '--chip', 'esp32c3', '--firmware', str(image),
                '--elf', str(args.elf), '--gdb', str(port), '--gdb-halt', '--timeout', '75s',
                '--net', 'user,restrict=yes'], stdout=log, stderr=log)
            deadline = time.monotonic() + 10
            while 'GDB stub listening' not in (folder / 'emulator.log').read_text(encoding='utf-8'):
                if process.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('GDB stub未就绪')
                time.sleep(0.1)
            commands = ['set pagination off', 'set confirm off', 'set remotetimeout 10',
                f'target remote 127.0.0.1:{port}', 'tbreak app_main', 'continue', 'bt',
                'info registers pc sp', 'stepi', 'info registers pc', 'detach']
            invocation = [args.gdb, '-nx', '-batch', str(args.elf)]
            for command in commands:
                invocation += ['-ex', command]
            result = subprocess.run(invocation, capture_output=True, text=True, timeout=60)
            output = result.stdout + result.stderr
            (folder / 'gdb.log').write_text(output, encoding='utf-8')
            if result.returncode or not re.search(r'Temporary breakpoint \d+, app_main', output):
                raise RuntimeError('未命中真实app_main断点，见gdb.log')
            pcs = re.findall(r'^pc\s+(0x[0-9a-fA-F]+)', output, re.MULTILINE)
            if len(pcs) != 2 or pcs[0] == pcs[1] or '#0 ' not in output or 'main_task' not in output:
                raise RuntimeError('调用栈/寄存器/单步证据不足')
        except (Exception, KeyboardInterrupt) as exc:
            error = f'{type(exc).__name__}: {exc}'
        finally:
            if process is not None:
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)
    if source_hash is not None and digest(args.firmware) != source_hash:
        error = '输入镜像被改变'
    report = {'status': 'FAIL' if error else 'PASS', 'error': error,
        'duration_seconds': round(time.monotonic() - started, 3), 'firmware_sha256': source_hash,
        'checks': ['app_main breakpoint', 'backtrace', 'pc/sp', 'instruction step'],
        'scope': '本机虚拟CPU调试；不是JTAG、射频或实物断点验证'}
    (folder / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (folder / 'report.md').write_text('# 本地虚拟板 GDB 验证\n\n' + json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'GDB {report["status"]} REPORT {folder / "report.md"}')
    return int(bool(error))


if __name__ == '__main__':
    raise SystemExit(main())
