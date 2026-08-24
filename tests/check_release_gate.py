#!/usr/bin/env python3
"""Release artifact gate static checks.

The real release artifact validator lives in scripts/tools/check_release_artifacts.py.
This test keeps the validator wired to the release workflow and checks the
dry-run/sample path without requiring ESP-IDF builds or real release binaries.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "scripts" / "tools" / "check_release_artifacts.py"
BUILD_ALL = ROOT / "scripts" / "build_all.sh"
BUILD_ALL_PS1 = ROOT / "scripts" / "build_all.ps1"
PUBLISH = ROOT / "scripts" / "tools" / "publish_gitee_release.py"
README = ROOT / "README.md"
TECH_REF = ROOT / "docs" / "dev" / "01-technical-reference.md"
OP_DOC = ROOT / "docs" / "dev" / "04-operation-test-gate.md"
QUALITY_GATE = ROOT / "scripts" / "tools" / "quality_gate.py"
OP_GATE = ROOT / "tests" / "operation_gate.py"
TAG = "[release-gate-check]"


def check(condition, message, failures):
    if condition:
        print(f"{TAG} PASS {message}")
    else:
        failures.append(message)
        print(f"{TAG} FAIL {message}")


def main():
    failures = []
    check(SCRIPT.is_file(), "存在 scripts/tools/check_release_artifacts.py", failures)
    if not SCRIPT.is_file():
        print(f"{TAG} 结果: {len(failures)} 失败")
        return 1

    script_src = SCRIPT.read_text(encoding="utf-8")
    build_all_src = BUILD_ALL.read_text(encoding="utf-8")
    build_all_ps1_src = BUILD_ALL_PS1.read_text(encoding="utf-8-sig")
    publish_src = PUBLISH.read_text(encoding="utf-8")
    readme_src = README.read_text(encoding="utf-8")
    tech_ref_src = TECH_REF.read_text(encoding="utf-8")
    op_doc_src = OP_DOC.read_text(encoding="utf-8")
    quality_gate_src = QUALITY_GATE.read_text(encoding="utf-8")
    op_gate_src = OP_GATE.read_text(encoding="utf-8")

    required_tokens = [
        ("EXPECTED_ATTACHMENTS", "集中定义 10 个上传附件"),
        ("hashlib.sha256", "校验 SHA256"),
        ("zipfile.ZipFile", "校验 release zip 内容"),
        ("manifest.txt", "校验 manifest"),
        ("git rev-parse --short HEAD", "默认使用当前 Git 短 ID"),
        ("git status --short", "发布严格模式检查工作区状态"),
        ("--allow-dirty", "允许显式放宽 dirty 工作区检查"),
        ("--skip-git-check", "允许离线样例或历史产物跳过 Git 状态检查"),
        ("--require-device-evidence", "正式发布可强制校验真机验收证据"),
        ("device-acceptance-generic-c3.json", "校验 generic-c3 真机验收证据"),
        ("device-acceptance-m5stamp-c3.json", "校验 m5stamp-c3 真机验收证据"),
    ]
    for token, message in required_tokens:
        check(token in script_src, message, failures)

    check("python3 \"$PROJECT_DIR/scripts/tools/check_release_artifacts.py\"" in build_all_src,
          "build_all.sh 生成后调用 release 产物门禁", failures)
    check("device_acceptance.py\" validate" in build_all_src
          and "device-acceptance-${board_name}.json" in build_all_src,
          "build_all.sh 校验并收录各板型真机验收证据", failures)
    check("device_acceptance.py" in build_all_ps1_src
          and '"device-acceptance-" + $boardName + ".json"' in build_all_ps1_src,
          "build_all.ps1 校验并收录各板型真机验收证据", failures)
    check("check_release_artifacts" in publish_src,
          "publish_gitee_release.py 上传前复用 release 产物门禁", failures)
    check("require_device_evidence=True" in publish_src,
          "publish_gitee_release.py 正式发布强制完整 P0 证据", failures)
    check("check_release_artifacts.py" in readme_src,
          "README 记录 release 产物门禁命令", failures)
    check("check_release_artifacts.py" in tech_ref_src,
          "技术参考记录 release 产物门禁命令", failures)
    check("tests/check_release_gate.py" in quality_gate_src,
          "quality_gate.py 聚合 release 产物门禁静态检查", failures)
    check("tests/test_release_artifacts.py" in quality_gate_src,
          "quality_gate.py 聚合 release 产物门禁功能样例", failures)
    check("tests/test_release_artifacts.py" in readme_src and "tests/test_release_artifacts.py" in tech_ref_src,
          "README 和技术参考记录 release 产物门禁功能样例", failures)
    check("tests/check_release_gate.py" in op_gate_src and "tests/test_release_artifacts.py" in op_gate_src,
          "operation_gate.py full 模式聚合 release 产物门禁", failures)
    check("check_release_artifacts.py" in op_doc_src and "test_release_artifacts.py" in op_doc_src,
          "操作覆盖矩阵记录 release 产物门禁", failures)
    check("device_acceptance.py validate --strict" in op_doc_src
          and "device-acceptance-generic-c3.json" in op_doc_src
          and "device-acceptance-m5stamp-c3.json" in op_doc_src,
          "操作覆盖矩阵记录双板严格真机证据门禁", failures)

    result = subprocess.run(
        [sys.executable, str(SCRIPT), "--release-dir", str(Path(tempfile.gettempdir()) / "missing-release-dir"),
         "--version", "0.0.0", "--git-sha", "deadbee", "--skip-git-check"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=20,
    )
    check(result.returncode != 0 and "release 目录不存在" in (result.stdout + result.stderr),
          "缺失 release 目录时失败且给出明确错误", failures)

    if failures:
        print(f"{TAG} 结果: {len(failures)} 失败")
        return 1
    print(f"{TAG} PASS: Release 产物门禁静态检查通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
