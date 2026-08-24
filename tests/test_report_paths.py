#!/usr/bin/env python3
"""运行报告必须写入构建产物目录，不能污染源码树卫生门禁。"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests"))

import operation_gate  # noqa: E402
import test_core_flow_endurance  # noqa: E402

EXPECTED = ROOT / "build" / "test-reports"
assert operation_gate.REPORT_DIR == EXPECTED
assert test_core_flow_endurance.REPORT_DIR == EXPECTED
flash_script = (ROOT / "scripts" / "flash_latest.sh").read_text(encoding="utf-8")
assert 'mkdir -p build/test-logs' in flash_script
assert 'LOG_FILE="build/test-logs/flash_latest_' in flash_script
assert "mkdir -p tests/logs" not in flash_script
print("[report-paths] PASS: 运行报告默认写入 build/test-reports")
