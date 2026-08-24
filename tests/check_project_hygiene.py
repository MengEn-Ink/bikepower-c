#!/usr/bin/env python3
"""Project structure and generated-artifact hygiene checks."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print(f"[project-hygiene] FAIL {message}")


for removed_dir in (
    ROOT / "docs" / "archive",
    ROOT / "docs" / "wiki",
    ROOT / "docs" / "superpowers",
    ROOT / ".trae" / "templates",
):
    check(not removed_dir.exists(), f"禁止恢复目录 {removed_dir.relative_to(ROOT)}")

expected_docs = {
    "01-technical-reference.md",
    "02-firmware-security.md",
    "03-running-mode-compatibility.md",
    "04-operation-test-gate.md",
    "05-windows-build-guide.md",
}
actual_docs = {path.name for path in (ROOT / "docs" / "dev").glob("*.md")}
check(actual_docs == expected_docs, f"docs/dev 文件集合异常: {sorted(actual_docs)}")

report_dir = ROOT / "tests" / "reports"
markdown_reports = sorted(path.name for path in report_dir.glob("*.md"))
check(not markdown_reports, f"tests/reports 不应保留 Markdown 历史报告: {markdown_reports}")
check(not (ROOT / "tests" / "logs").exists(), "tests/logs 不应进入源码树")
check(not (ROOT / "test" / "ble_codec_cli").exists(), "host 测试二进制不应留在源码树")

svg_files = sorted(path.name for path in (ROOT / "docs" / "images").glob("*.svg"))
check(not svg_files, f"未引用 SVG 不应保留: {svg_files}")

if failures:
    raise SystemExit(1)

print("[project-hygiene] PASS: 文档与生成物结构清晰")
