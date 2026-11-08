#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""版本一致性静态检查：所有对外可见的版本标识必须等于固件 FIRMWARE_VERSION。

检查范围（线上/对外资产，故意只扫这些，避免历史文档与 IP 地址误报）：
  - tools/web/*.html              （Pages 主站，含手册标题与"手册源码版本"）
  - docs/user/*.md                （用户手册源）
  - README.md                     （仓库首页版本声明）
  - build/release/<version>/manifest.txt（当前版本发行清单）

只认「v<主>.<次>.<修订>」且主次版本与当前固件一致的写法，
因此 ESP-IDF v5.5.4、IP 127.0.0.1、历史版本目录都不会误报。

退出码：0 一致；1 不一致（CI 应拦截）。
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import List, Tuple

ROOT = Path(__file__).resolve().parents[2]


def firmware_version() -> str:
    src = (ROOT / "main" / "bikepower_config.h").read_text(encoding="utf-8")
    m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', src)
    if not m:
        raise SystemExit("[version-check] 无法读取 FIRMWARE_VERSION")
    return m.group(1)


def version_pattern(expected: str) -> re.Pattern:
    prefix = ".".join(expected.split(".")[:2])
    return re.compile(rf"(?<![\w.])v{re.escape(prefix)}\.\d+(?![\d.])")


def scan_file(path: Path, expected: str, allow: set, pattern: re.Pattern) -> List[Tuple[int, str, str]]:
    problems: List[Tuple[int, str, str]] = []
    for lineno, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        for m in pattern.finditer(line):
            found = m.group(0).lstrip("v")
            if found == expected or found in allow:
                continue
            problems.append((lineno, found, line.strip()[:120]))
    return problems


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="check_release_version_consistency")
    p.add_argument("--allow", action="append", default=[],
                   help="允许出现的旧版本号（可多次），如 --allow 4.3.0")
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args(argv)

    expected = firmware_version()
    pattern = version_pattern(expected)
    allow = set(args.allow)

    targets = [
        "tools/web/*.html",
        "docs/user/*.md",
        "README.md",
        f"build/release/{expected}/manifest.txt",
    ]
    files: List[Path] = []
    for t in targets:
        files.extend(sorted(ROOT.glob(t)))

    total = 0
    bad = 0
    for f in files:
        if not f.is_file():
            continue
        total += 1
        problems = scan_file(f, expected, allow, pattern)
        if problems:
            bad += 1
            print(f"[FAIL] {f.relative_to(ROOT)}")
            for lineno, found, text in problems:
                print(f"   L{lineno}: v{found}  ←  {text}")
        elif not args.quiet:
            print(f"[OK]   {f.relative_to(ROOT)}")

    print(f"\n[version-check] FIRMWARE_VERSION={expected}  扫描 {total} 个文件，不一致 {bad} 个")
    if bad:
        print("[version-check] 先同步版本标识再发版；确实在讲旧版本时用 --allow 显式放行。")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
