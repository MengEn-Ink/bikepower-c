#!/usr/bin/env python3
"""构建脚本不得用临时板型配置污染默认 build 缓存。"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FILES = {
    "build_all.sh": ROOT / "scripts" / "build_all.sh",
    "flash_latest.sh": ROOT / "scripts" / "flash_latest.sh",
    "build_all.ps1": ROOT / "scripts" / "build_all.ps1",
    "flash_latest.ps1": ROOT / "scripts" / "flash_latest.ps1",
    "quality_gate.py": ROOT / "scripts" / "tools" / "quality_gate.py",
}

sources = {name: path.read_text(encoding="utf-8-sig") for name, path in FILES.items()}
checks = [
    ('-B "$BUILD_DIR"' in sources["build_all.sh"], "build_all.sh 使用板型专用构建目录"),
    ('-B "$FLASH_BUILD_DIR"' in sources["flash_latest.sh"], "flash_latest.sh 使用烧录专用构建目录"),
    ('"build\\release-work"' in sources["build_all.ps1"]
     and '"-B", $buildDir' in sources["build_all.ps1"],
     "build_all.ps1 使用板型专用构建目录"),
    ('"build-flash-" + $Board' in sources["flash_latest.ps1"], "flash_latest.ps1 使用烧录专用构建目录"),
    ('"build", "quality-gate"' in sources["quality_gate.py"], "quality_gate.py 使用专用构建目录"),
]

failures = [message for condition, message in checks if not condition]
for failure in failures:
    print(f"[build-isolation-check] FAIL {failure}")
if failures:
    raise SystemExit(1)

print("[build-isolation-check] PASS: 临时构建配置与默认 build 缓存隔离")
