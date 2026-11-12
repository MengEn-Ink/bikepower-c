#!/usr/bin/env python3
"""私有虚拟 Flash 故障注入：真实固件 panic 后验证 flash dump CRC/ELF。
不访问设备、不改变源码/生产 defaults，不上传 runtime Flash 或 dump。
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts/tools'))
from test_process_runtime import prepare_test_signals, stop_process
from virtual_board import digest, verify_version


def main():
    prepare_test_signals()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('emulator', 'firmware', 'elf', 'gdb', 'report-dir'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    args.report_dir.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix='panic-', dir=args.report_dir))
    process = None
    error = None
    details = {}
    started = time.monotonic()
    source_hash = digest(args.firmware)
    try:
        verify_version(args.emulator)
        image = folder/'runtime-private.bin'
        shutil.copyfile(args.firmware, image)
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            port = listener.getsockname()[1]
        with (folder/'emulator.log').open('w', encoding='utf-8') as log:
            process = subprocess.Popen([str(args.emulator), '--chip', 'esp32c3', '--firmware', str(image),
                '--elf', str(args.elf), '--gdb', str(port), '--gdb-halt', '--timeout', '30s',
                '--save-state', '--no-panic-intercept', '--net', 'user,restrict=yes'], stdout=log, stderr=log)
            deadline = time.monotonic()+10
            while 'GDB stub listening' not in (folder/'emulator.log').read_text(encoding='utf-8'):
                if process.poll() is not None or time.monotonic()>deadline:
                    raise RuntimeError('GDB stub 未就绪')
                time.sleep(0.1)
            commands = ['set pagination off', 'set confirm off', 'set remotetimeout 10',
                f'target remote 127.0.0.1:{port}', 'tbreak power_engine_snapshot', 'continue', 'bt',
                'set $pc = (void *)abort', 'detach']
            invocation = [str(args.gdb), '-nx', '-batch', str(args.elf)]
            for command in commands:
                invocation.extend(['-ex', command])
            result = subprocess.run(invocation, capture_output=True, text=True, timeout=25)
            output = result.stdout+result.stderr
            (folder/'gdb.log').write_text(output, encoding='utf-8')
            if result.returncode or not re.search(r'Temporary breakpoint \d+, power_engine_snapshot', output):
                raise RuntimeError('未命中业务初始化后的真实断点')
            process.wait(timeout=40)
        raw = image.read_bytes()[0x3f0000:0x400000]
        length = struct.unpack_from('<I', raw)[0]
        if not 24 <= length <= len(raw):
            raise RuntimeError(f'有效 dump 未取得：length={length}')
        dump = raw[:length]
        expected = struct.unpack_from('<I', dump, length-4)[0]
        actual = zlib.crc32(dump[:-4]) & 0xffffffff
        if actual != expected:
            raise RuntimeError('dump CRC32 不匹配')
        if dump[24:28] != b'\x7fELF':
            raise RuntimeError('dump ELF 头未取得')
        details = {'dump_bytes': length, 'crc32': f'{actual:08x}', 'elf': True, 'symbolicated': False}
        core = folder/'core-private.elf'
        core.write_bytes(dump[24:-4])
        result = subprocess.run([str(args.gdb), '-nx', '-batch', str(args.elf), '-c', str(core),
            '-ex', 'info threads', '-ex', 'thread apply all bt'], capture_output=True, text=True, timeout=25)
        decoded = result.stdout+result.stderr
        (folder/'decoded.log').write_text(decoded, encoding='utf-8')
        if result.returncode or 'abort' not in decoded or 'main_task' not in decoded:
            raise RuntimeError('dump 符号化/主任务证据不足')
        details = {'dump_bytes': length, 'crc32': f'{actual:08x}', 'elf': True, 'symbolicated': True}
    except (Exception, KeyboardInterrupt) as exc:
        error = f'{type(exc).__name__}: {exc}'
    finally:
        if process is not None:
            try:
                stop_process(process, 10)
            except RuntimeError as exc:
                error = (error+'; ' if error else '')+str(exc)
    if digest(args.firmware) != source_hash:
        error = '输入镜像被修改'
    report = {'status': 'FAIL' if error else 'PASS', 'error': error, 'checks': details,
        'firmware_sha256': source_hash, 'elf_sha256': digest(args.elf),
        'duration_seconds': round(time.monotonic()-started,3),
        'scope': '私有虚拟副本 GDB 重定向到 abort；不证明实物 flash、加密取证或所有 panic 情境'}
    (folder/'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))
    print('REPORT', folder/'report.json')
    return int(bool(error))


if __name__ == '__main__':
    raise SystemExit(main())
