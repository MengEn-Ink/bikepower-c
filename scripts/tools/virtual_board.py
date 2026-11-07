#!/usr/bin/env python3
"""本地虚拟板工具：固定版本安装、只读输入镜像合并与依赖自检。"""
import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOCK = json.loads((Path(__file__).with_name('virtual_board_lock.json')).read_text(encoding='utf-8'))


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest() if hasattr(hashlib, 'file_digest') else _digest(source)


def _digest(source):
    result = hashlib.sha256()
    for block in iter(lambda: source.read(1024 * 1024), b''):
        result.update(block)
    return result.hexdigest()


def target_platform():
    machine = platform.machine().lower()
    if sys.platform == 'darwin' and machine in ('arm64', 'aarch64'):
        return 'aarch64-apple-darwin'
    if sys.platform.startswith('linux') and machine in ('x86_64', 'amd64', 'aarch64', 'arm64'):
        return ('aarch64' if machine in ('aarch64', 'arm64') else 'x86_64') + '-unknown-linux-gnu'
    raise RuntimeError('Windows请使用WSL2入口；固定版不提供Intel Mac原生包，WASM/容器替代路径尚未验证')


def install(destination):
    destination = destination.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    target = target_platform()
    version = LOCK['esp_emulator']
    filename = f'esp-emu-{version}-{target}.tar.gz'
    archive = destination / filename
    if not archive.exists():
        temporary = archive.with_suffix('.partial')
        urllib.request.urlretrieve(f'https://github.com/espressif/esp-emulator/releases/download/v{version}/{filename}', temporary)
        if digest(temporary) != LOCK['archives'][target]:
            raise RuntimeError('模拟器下载SHA256不匹配，拒绝安装')
        temporary.replace(archive)
    if digest(archive) != LOCK['archives'][target]:
        raise RuntimeError('缓存模拟器SHA256不匹配，拒绝安装')
    executable = destination / 'esp-emu'
    with tarfile.open(archive) as bundle:
        members = [entry for entry in bundle.getmembers() if entry.isfile() and Path(entry.name).name == 'esp-emu']
        if len(members) != 1:
            raise RuntimeError('模拟器包必须只有一个可执行文件候选')
        with bundle.extractfile(members[0]) as source, executable.with_suffix('.new').open('wb') as output:
            shutil.copyfileobj(source, output)
    executable.with_suffix('.new').chmod(0o755)
    executable.with_suffix('.new').replace(executable)
    verify_version(executable)
    print(executable)


def verify_version(executable):
    result = subprocess.run([str(executable), '--version'], capture_output=True, text=True, timeout=10, check=True)
    if f'esp-emu {LOCK["esp_emulator"]}' not in result.stdout:
        raise RuntimeError('模拟器版本不匹配')
    return result.stdout.strip().splitlines()[-1]


def merge(build, output):
    build, output = build.resolve(), output.resolve()
    config = json.loads((build / 'flasher_args.json').read_text(encoding='utf-8'))
    if config['extra_esptool_args']['chip'] != 'esp32c3':
        raise RuntimeError('仅接受ESP32-C3产物')
    if output.exists():
        raise RuntimeError('输出已存在，使用新的会话文件，禁止覆盖基线镜像')
    output.parent.mkdir(parents=True, exist_ok=True)
    from esptool import main as esptool_main
    settings = config['flash_settings']
    arguments = ['--chip', 'esp32c3', 'merge_bin', '-o', str(output), '--fill-flash-size', settings['flash_size']]
    for option, key in (('--flash_mode','flash_mode'),('--flash_freq','flash_freq'),('--flash_size','flash_size')):
        arguments += [option, settings[key]]
    for offset, relative in sorted(config['flash_files'].items(), key=lambda item: int(item[0], 0)):
        source = (build / relative).resolve()
        if not source.is_relative_to(build) or not source.is_file():
            raise RuntimeError('镜像路径越界或缺失')
        arguments += [offset, str(source)]
    esptool_main(arguments)
    print(f'SHA256 {digest(output)} {output}')


def check_dependencies():
    import importlib.metadata
    for line in (ROOT / 'requirements-virtual-board.txt').read_text(encoding='utf-8').splitlines():
        if not line.strip() or line.startswith('#'):
            continue
        name, expected = line.split('==')
        actual = importlib.metadata.version(name)
        if actual != expected:
            raise RuntimeError(f'{name}版本不匹配: {actual}, 需要{expected}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    p = sub.add_parser('install'); p.add_argument('--destination', type=Path, default=ROOT/'build/virtual-board-tools')
    p = sub.add_parser('merge'); p.add_argument('--build-dir', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('doctor'); p.add_argument('--emulator', type=Path, required=True)
    args = parser.parse_args()
    if args.action == 'install':
        install(args.destination)
    elif args.action == 'merge':
        merge(args.build_dir, args.output)
    else:
        check_dependencies()
        verify_version(args.emulator)
        for package in ('bumble', 'esptool'):
            print(f'{package} {LOCK[package]}')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f'[virtual-board] FAIL {type(error).__name__}: {error}', file=sys.stderr)
        raise SystemExit(1)
