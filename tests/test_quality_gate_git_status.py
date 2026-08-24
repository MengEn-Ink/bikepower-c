#!/usr/bin/env python3
"""质量门禁必须区分已推送与未配置 upstream。"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = (ROOT / "scripts" / "tools" / "quality_gate.py").read_text(encoding="utf-8")

for token in ('"rev-parse"', '"--abbrev-ref"', '"--symbolic-full-name"', '"@{u}"'):
    assert token in SOURCE
assert "未配置 upstream" in SOURCE
assert '"全部已推送（或无上游）"' not in SOURCE
assert "if upstream_rc != 0:" in SOURCE
print("[quality-git-status] PASS: 已推送与无 upstream 状态明确区分")
