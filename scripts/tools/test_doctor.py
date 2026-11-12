#!/usr/bin/env python3
"""离线测试前置诊断；只检查、不安装、不修改环境。"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def diagnose():
    checks = []
    def record(name, ready, detail):
        checks.append({'name': name, 'status': 'PASS' if ready else 'MISSING', 'detail': detail})
    for package in ('bleak', 'serial', 'playwright', 'fitparse'):
        record('python.' + package, importlib.util.find_spec(package) is not None, sys.executable)
    for command in ('node', 'make', 'cc'):
        found = shutil.which(command)
        record(command, bool(found), found or 'not found')
    for name, module in (('node.playwright', 'playwright'), ('node.jsdom', 'jsdom')):
        ready = False
        try:
            if shutil.which('node'):
                result = subprocess.run(['node', '-e', 'require(process.argv[1])', module],
                                        cwd=ROOT/'tests/web-flash', capture_output=True, timeout=10)
                ready = result.returncode == 0
        except (OSError, subprocess.TimeoutExpired):
            pass
        record(name, ready, 'tests/web-flash/node_modules')
    chromium_ready = False
    try:
        if shutil.which('node'):
            result = subprocess.run(['node', '-e', 'console.log(require("playwright").chromium.executablePath())'],
                                    cwd=ROOT/'tests/web-flash', capture_output=True, text=True, timeout=10)
            chromium_ready = result.returncode == 0 and Path(result.stdout.strip()).is_file()
    except (OSError, subprocess.TimeoutExpired):
        pass
    record('node.chromium', chromium_ready, 'Node Playwright browser executable')
    chromium_ready = False
    try:
        if importlib.util.find_spec('playwright'):
            from playwright.sync_api import sync_playwright
            with sync_playwright() as playwright:
                chromium_ready = Path(playwright.chromium.executable_path).is_file()
    except (ImportError, OSError, RuntimeError):
        pass
    record('python.chromium', chromium_ready, 'Python Playwright browser executable')
    openssl_ready = False
    try:
        if shutil.which('cc'):
            with tempfile.TemporaryDirectory(prefix='bikepower-doctor-') as temp:
                folder = Path(temp)
                source = folder/'probe.c'
                source.write_text('#include <openssl/sha.h>\nint main(void){unsigned char x[32]; return SHA256((const unsigned char*)"x",1,x)==0;}\n', encoding='utf-8')
                result = subprocess.run(['cc', str(source), '-lcrypto', '-o', str(folder/'probe')], capture_output=True, timeout=15)
                openssl_ready = result.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        pass
    record('host.openssl', openssl_ready,
           'compile/link ready' if openssl_ready else 'check compiler and installed OpenSSL include/library paths')
    if os.name == 'posix':
        blocked = signal.SIGTERM in signal.pthread_sigmask(signal.SIG_BLOCK, set())
        ignored = signal.getsignal(signal.SIGTERM) == signal.SIG_IGN
        record('signals', True, 'virtual runner normalizes its own SIGTERM' if blocked or ignored else 'SIGTERM ready')
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    checks = diagnose()
    if args.json:
        print(json.dumps(checks, ensure_ascii=False, indent=2))
    else:
        for item in checks:
            print(item['status'], item['name'], '-', item['detail'])
    return int(any(item['status'] != 'PASS' for item in checks))


if __name__ == '__main__':
    raise SystemExit(main())
