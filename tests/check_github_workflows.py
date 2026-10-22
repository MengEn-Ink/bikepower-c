#!/usr/bin/env python3
"""GitHub-only 仓库治理与 Actions 工作流契约。"""

from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
WORKFLOWS = ROOT / ".github" / "workflows"
TAG = "[github-contract]"


def check(condition, message, failures):
    print(f"{TAG} {'PASS' if condition else 'FAIL'} {message}")
    if not condition:
        failures.append(message)


def main():
    failures = []
    required = [
        ".github/CODEOWNERS",
        ".github/PULL_REQUEST_TEMPLATE.md",
        ".github/ISSUE_TEMPLATE/bug.yml",
        ".github/ISSUE_TEMPLATE/feature.yml",
        ".github/ISSUE_TEMPLATE/config.yml",
        ".github/dependabot.yml",
        ".github/release.yml",
        ".github/workflows/ci.yml",
        ".github/workflows/deploy-web-tools.yml",
        "requirements-test.txt",
        "SECURITY.md",
    ]
    for relative in required:
        check((ROOT / relative).is_file(), f"治理资产存在：{relative}", failures)

    workflow_files = sorted(WORKFLOWS.glob("*.yml")) + sorted(WORKFLOWS.glob("*.yaml"))
    workflow_source = "\n".join(path.read_text(encoding="utf-8") for path in workflow_files)
    check("pull_request_target" not in workflow_source, "工作流禁用 pull_request_target", failures)
    check("permissions:" in workflow_source, "工作流显式声明权限", failures)
    check("timeout-minutes:" in workflow_source, "工作流任务设置超时", failures)

    ci = (WORKFLOWS / "ci.yml").read_text(encoding="utf-8")
    deploy = (WORKFLOWS / "deploy-web-tools.yml").read_text(encoding="utf-8")
    check("pull_request:" in ci and "push:" in ci, "CI 覆盖 PR 与 main push", failures)
    check("tests/operation_gate.py" in ci and "--level quick" in ci,
          "CI 通过 operation harness 执行 quick 门禁", failures)
    check("pip install" in ci and "requirements-test.txt" in ci,
          "CI 安装 BLE 与串口 Harness 依赖", failures)
    check("actions/upload-artifact@" in ci and "if: always()" in ci,
          "CI 始终上传 Harness 报告", failures)
    check("workflow_run:" in deploy and "workflows: [CI]" in deploy,
          "Pages 在 CI 完成后触发", failures)
    check("workflow_run.conclusion == 'success'" in deploy,
          "Pages 只消费成功的 CI", failures)
    check("github.event.workflow_run.head_sha" in deploy
          and "github.event.repository.default_branch" in deploy,
          "Pages 对 CI 使用已验证提交，对 Release/手动任务使用默认分支", failures)
    check("release:" in deploy and "types: [published]" in deploy,
          "GitHub Release 发布后刷新 Pages", failures)
    check("--require-firmware" in deploy,
          "正式 Pages 部署要求完整双板固件", failures)
    check("pages: write" in deploy and "id-token: write" in deploy,
          "Pages 部署权限按任务最小化", failures)

    active_sources = [
        ROOT / "scripts" / "tools" / "prepare_web_flash_site.py",
        ROOT / "scripts" / "tools" / "publish_github_release.py",
        WORKFLOWS / "deploy-web-tools.yml",
    ]
    active_text = "\n".join(path.read_text(encoding="utf-8") for path in active_sources)
    legacy_marker = "gi" + "tee"
    check(legacy_marker.lower() not in active_text.lower(),
          "网页部署与发布脚本不再依赖旧托管渠道", failures)
    legacy_secret = "GIT" + "EE_TOKEN"
    legacy_repository = "GIT" + "EE_REPOSITORY"
    check(legacy_secret not in workflow_source and legacy_repository not in workflow_source,
          "Actions 不再读取旧渠道 secret/variable", failures)

    if failures:
        print(f"{TAG} FAIL: {len(failures)} 项")
        return 1
    print(f"{TAG} PASS: GitHub 仓库治理与 Actions 契约通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
