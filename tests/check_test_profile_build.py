#!/usr/bin/env python3
"""校验真机验收固件可显式覆盖运动 profile，且与正式构建隔离。"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FLASH = (ROOT / "scripts" / "flash_latest.sh").read_text(encoding="utf-8")
FLASH_PS1 = (ROOT / "scripts" / "flash_latest.ps1").read_text(encoding="utf-8-sig")
CMAKE = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
APP_MAIN = (ROOT / "main" / "app_main.c").read_text(encoding="utf-8")

checks = {
    "烧录脚本提供 test-profile 参数": "--test-profile" in FLASH,
    "测试 profile 仅允许 cycling/running": 'cycling|running' in FLASH,
    "测试 profile 使用独立构建目录": 'flash-$BOARD_TARGET-$TEST_PROFILE' in FLASH,
    "CMake 校验测试 profile": 'BIKEPOWER_TEST_PROFILE STREQUAL \"running\"' in CMAKE,
    "固件启动时覆盖 profile": "BIKEPOWER_TEST_SPORT_PROFILE" in APP_MAIN,
    "正式构建默认不覆盖 profile": 'set(BIKEPOWER_TEST_PROFILE \"\"' in CMAKE,
    "Windows 烧录脚本提供 TestProfile 参数": '[ValidateSet("", "cycling", "running")]' in FLASH_PS1
    and '[string]$TestProfile' in FLASH_PS1,
    "Windows 测试 profile 使用独立构建目录": '"build-flash-" + $Board + "-" + $TestProfile' in FLASH_PS1,
    "Windows 烧录脚本透传测试 profile": '"BIKEPOWER_TEST_PROFILE=$TestProfile"' in FLASH_PS1,
}

failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(f"[test-profile-build] {'PASS' if ok else 'FAIL'} {name}")
if failed:
    raise SystemExit(1)
print("[test-profile-build] PASS: 测试 profile 构建隔离完整")
