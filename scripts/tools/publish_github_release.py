#!/usr/bin/env python3
"""使用 GitHub CLI 发布经过严格门禁的 BikePower C Release。"""

import argparse
import re
import shutil
import subprocess
from pathlib import Path

from check_release_artifacts import validate_release_artifacts


ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_REPOSITORY = "MengEn-Ink/bikepower-c"
REPOSITORY_RE = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")


def git_output(args):
    return subprocess.check_output(
        ["git", "-C", str(ROOT), *args], text=True
    ).strip()


def firmware_version():
    source = (ROOT / "main" / "bikepower_config.h").read_text(encoding="utf-8")
    match = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', source)
    if not match:
        raise RuntimeError("无法从 main/bikepower_config.h 读取固件版本")
    return match.group(1)


def validate_repository(repository):
    if not REPOSITORY_RE.fullmatch(repository):
        raise RuntimeError("GitHub 仓库必须使用 owner/repo 格式")


def require_pushed_tag(tag, expected_commit):
    local_commit = git_output(["rev-list", "-n", "1", tag])
    if local_commit != expected_commit:
        raise RuntimeError(f"{tag} 未指向当前 HEAD，拒绝发布")

    result = subprocess.run(
        [
            "git",
            "-C",
            str(ROOT),
            "ls-remote",
            "--exit-code",
            "origin",
            f"refs/tags/{tag}",
            f"refs/tags/{tag}^{{}}",
        ],
        capture_output=True,
        text=True,
        timeout=30,
    )
    if result.returncode != 0:
        raise RuntimeError(f"origin 不存在已推送的 {tag}")
    remote_commits = {
        line.split()[0] for line in result.stdout.splitlines() if line.split()
    }
    if expected_commit not in remote_commits:
        raise RuntimeError(f"origin/{tag} 与当前 HEAD 不一致，拒绝发布")


def release_body(version, git_sha, artifacts):
    artifact_lines = "\n".join(f"- `{path.name}`" for path in artifacts)
    return "\n".join(
        [
            f"BikePower C v{version}",
            "",
            "## 产物",
            "",
            artifact_lines,
            "",
            "## 网页刷写",
            "",
            "使用 GitHub Pages：<https://mengen-ink.github.io/bikepower-c/>",
            "",
            "支持 generic-c3 与 m5stamp-c3。请使用桌面版 Chrome 或 Edge，",
            "确认板型并主动选择串口后再开始刷写。merged bin 从 `0x0` 写入。",
            "",
            "## 开发者命令行兜底",
            "",
            "```bash",
            "esptool.py --chip esp32c3 -b 460800 write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x0 <merged-bin>",
            "```",
            "",
            "## 验证",
            "",
            "- 发布前已通过 10 个顶层附件、SHA256、manifest、ZIP 和双板 P0 真机证据门禁。",
            "- `test-report.md`：发版测试报告。",
            "- `core-flow-endurance.md`：核心流程耐久测试明细。",
            "- `user-manual.md`：用户手册。",
            f"- Git ID：`{git_sha}`。",
        ]
    )


def main():
    parser = argparse.ArgumentParser(description="发布 BikePower C GitHub Release")
    parser.add_argument("--repository", default=DEFAULT_REPOSITORY)
    parser.add_argument("--version", default="")
    parser.add_argument("--tag", default="")
    parser.add_argument("--release-dir", default=str(ROOT / "build" / "release"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    validate_repository(args.repository)
    version = args.version or firmware_version()
    tag = args.tag or f"v{version}"
    head_commit = git_output(["rev-parse", "HEAD"])
    git_sha = git_output(["rev-parse", "--short", "HEAD"])
    require_pushed_tag(tag, head_commit)

    artifacts, messages = validate_release_artifacts(
        release_dir=Path(args.release_dir).resolve(),
        version=version,
        git_sha=git_sha,
        require_pushed=True,
        require_device_evidence=True,
    )
    for message in messages:
        print(f"[release-gate] {message}")

    print(f"GitHub Release: {args.repository} {tag} ({git_sha})")
    print("待上传附件:")
    for path in artifacts:
        print(f"  - {path.name} ({path.stat().st_size} bytes)")

    if args.dry_run:
        print("dry-run: tag、推送状态、双板证据和附件已校验；未创建 Release")
        return 0

    if shutil.which("gh") is None:
        raise RuntimeError("未找到 gh，请安装 GitHub CLI 并完成 gh auth login")
    exists = subprocess.run(
        ["gh", "release", "view", tag, "--repo", args.repository],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if exists.returncode == 0:
        raise RuntimeError(f"GitHub Release {tag} 已存在，拒绝覆盖")

    command = [
        "gh",
        "release",
        "create",
        tag,
        "--repo",
        args.repository,
        "--verify-tag",
        "--title",
        f"BikePower C {tag}",
        "--notes",
        release_body(version, git_sha, artifacts),
        *[str(path) for path in artifacts],
    ]
    subprocess.run(command, cwd=ROOT, check=True)
    print(f"已发布 GitHub Release {tag}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
