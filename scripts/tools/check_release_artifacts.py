#!/usr/bin/env python3
"""Validate BikePower C release artifacts before publication."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
import zipfile
from pathlib import Path

from device_acceptance import AcceptanceError, validate_record

ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_RELEASE_DIR = ROOT / "build" / "release"
DEVICE_EVIDENCE_RELS = {
    "generic-c3": "evidence/device-acceptance-generic-c3.json",
    "m5stamp-c3": "evidence/device-acceptance-m5stamp-c3.json",
}
GIT_SHA_COMMAND = "git rev-parse --short HEAD"
GIT_STATUS_COMMAND = "git status --short"

EXPECTED_ATTACHMENTS = (
    "bikepower-c-generic-c3-v{version}-stable-fix-g{git_sha}.bin",
    "bikepower-c-generic-c3-v{version}-stable-fix-g{git_sha}.bin.sha256",
    "bikepower-c-m5stamp-c3-v{version}-stable-fix-g{git_sha}.bin",
    "bikepower-c-m5stamp-c3-v{version}-stable-fix-g{git_sha}.bin.sha256",
    "bikepower-c-release-v{version}-stable-fix-g{git_sha}.zip",
    "bikepower-c-release-v{version}-stable-fix-g{git_sha}.zip.sha256",
    "manifest.txt",
    "test-report.md",
    "core-flow-endurance.md",
    "user-manual.md",
)


class ReleaseGateError(RuntimeError):
    """Raised when a release artifact invariant is violated."""


def git_output(args):
    return subprocess.check_output(["git", "-C", str(ROOT)] + args, text=True).strip()


def read_firmware_version():
    config = (ROOT / "main" / "bikepower_config.h").read_text(encoding="utf-8")
    match = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', config)
    if not match:
        raise ReleaseGateError("无法从 main/bikepower_config.h 读取 FIRMWARE_VERSION")
    return match.group(1)


def current_git_sha():
    return git_output(["rev-parse", "--short", "HEAD"])


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def expected_attachment_names(version, git_sha):
    return [name.format(version=version, git_sha=git_sha) for name in EXPECTED_ATTACHMENTS]


def parse_sha256_sidecar(path):
    text = path.read_text(encoding="utf-8").strip()
    parts = text.split()
    if len(parts) < 2:
        raise ReleaseGateError(f"{path.name} 格式错误，应为 '<sha256>  <filename>'")
    digest, filename = parts[0], parts[1]
    if not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
        raise ReleaseGateError(f"{path.name} 中 SHA256 格式错误: {digest}")
    return digest.lower(), filename


def parse_manifest(path):
    entries = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 3:
            raise ReleaseGateError(f"manifest.txt 行格式错误: {raw}")
        filename, size_text, digest = parts
        if not size_text.isdigit():
            raise ReleaseGateError(f"manifest.txt 大小字段错误: {raw}")
        if not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
            raise ReleaseGateError(f"manifest.txt SHA256 字段错误: {raw}")
        if filename in entries:
            raise ReleaseGateError(f"manifest.txt 重复记录: {filename}")
        entries[filename] = (int(size_text), digest.lower())
    return entries


def iter_image_files(release_dir):
    image_dir = release_dir / "images"
    if not image_dir.is_dir():
        return []
    return sorted(path for path in image_dir.rglob("*") if path.is_file())


def check_git_state(git_sha, allow_dirty, require_pushed, skip_git_check):
    if skip_git_check:
        return []
    messages = []
    current = current_git_sha()
    if current != git_sha:
        raise ReleaseGateError(f"产物 Git ID g{git_sha} 与当前 HEAD g{current} 不一致")

    dirty = git_output(["status", "--short"])
    if dirty and not allow_dirty:
        raise ReleaseGateError("工作区存在未提交改动；正式发版请先提交，或显式使用 --allow-dirty")
    if dirty:
        messages.append("工作区 dirty，已按 --allow-dirty 放行")

    if require_pushed:
        unpushed = git_output(["rev-list", "--count", "@{u}..HEAD"])
        if unpushed.isdigit() and int(unpushed) > 0:
            raise ReleaseGateError(f"当前 HEAD 有 {unpushed} 个未推送 commit；请先 push 再发布")
    return messages


def validate_release_artifacts(
    release_dir=DEFAULT_RELEASE_DIR,
    version=None,
    git_sha=None,
    allow_dirty=False,
    require_pushed=False,
    skip_git_check=False,
    require_device_evidence=False,
):
    release_dir = Path(release_dir).resolve()
    version = version or read_firmware_version()
    git_sha = git_sha or current_git_sha()
    messages = []

    if not release_dir.is_dir():
        raise ReleaseGateError(f"release 目录不存在: {release_dir}")

    messages.extend(check_git_state(git_sha, allow_dirty, require_pushed, skip_git_check))

    expected = expected_attachment_names(version, git_sha)
    expected_set = set(expected)
    missing = [name for name in expected if not (release_dir / name).is_file()]
    if missing:
        raise ReleaseGateError("release 产物缺失: " + ", ".join(missing))

    top_level_files = {path.name for path in release_dir.iterdir() if path.is_file()}
    unexpected = sorted(top_level_files - expected_set)
    if unexpected:
        raise ReleaseGateError("release 目录存在非标准顶层文件: " + ", ".join(unexpected))

    evidence_rel_names = []
    missing_evidence_boards = [
        board
        for board, rel_name in DEVICE_EVIDENCE_RELS.items()
        if not (release_dir / rel_name).is_file()
    ]
    if require_device_evidence and missing_evidence_boards:
        raise ReleaseGateError(
            "缺少板型真机验收证据: " + ", ".join(missing_evidence_boards)
        )
    for board, rel_name in DEVICE_EVIDENCE_RELS.items():
        evidence_path = release_dir / rel_name
        if not evidence_path.is_file():
            continue
        try:
            record = json.loads(evidence_path.read_text(encoding="utf-8"))
            validate_record(
                record,
                expected_version=version,
                expected_git_sha=git_sha,
                expected_board=board,
                strict=require_device_evidence,
            )
        except (AcceptanceError, json.JSONDecodeError) as exc:
            raise ReleaseGateError(f"真机验收证据无效 ({board}): {exc}") from exc
        evidence_rel_names.append(rel_name)
        messages.append(
            f"{board} 真机验收证据通过"
            + ("（P0 完整）" if require_device_evidence else "（结构与版本匹配）")
        )

    for name in expected:
        path = release_dir / name
        if path.stat().st_size <= 0:
            raise ReleaseGateError(f"{name} 为空文件")

    for name in expected:
        if not name.endswith(".sha256"):
            continue
        sidecar = release_dir / name
        digest, filename = parse_sha256_sidecar(sidecar)
        target = release_dir / filename
        if not target.is_file():
            raise ReleaseGateError(f"{name} 指向的文件不存在: {filename}")
        actual = sha256_file(target)
        if actual != digest:
            raise ReleaseGateError(f"{filename} SHA256 不匹配: sidecar={digest} actual={actual}")

    manifest = parse_manifest(release_dir / "manifest.txt")
    manifest_required = [name for name in expected if name != "manifest.txt"]
    image_rel_names = [str(path.relative_to(release_dir)) for path in iter_image_files(release_dir)]
    for rel_name in manifest_required + image_rel_names + evidence_rel_names:
        if rel_name not in manifest:
            raise ReleaseGateError(f"manifest.txt 缺少记录: {rel_name}")
        path = release_dir / rel_name
        size, digest = manifest[rel_name]
        actual_size = path.stat().st_size
        actual_digest = sha256_file(path)
        if size != actual_size or digest != actual_digest:
            raise ReleaseGateError(
                f"manifest.txt 记录与文件不一致: {rel_name} "
                f"manifest=({size},{digest}) actual=({actual_size},{actual_digest})"
            )

    manifest_extra = sorted(
        set(manifest)
        - set(manifest_required)
        - set(image_rel_names)
        - set(evidence_rel_names)
    )
    if manifest_extra:
        raise ReleaseGateError("manifest.txt 存在非标准记录: " + ", ".join(manifest_extra))

    zip_name = f"bikepower-c-release-v{version}-stable-fix-g{git_sha}.zip"
    zip_path = release_dir / zip_name
    expected_zip_entries = {
        name for name in expected
        if name not in {"manifest.txt", zip_name, f"{zip_name}.sha256"}
    }
    expected_zip_entries.update({"manifest.txt", "test-report.md", "core-flow-endurance.md", "user-manual.md"})
    expected_zip_entries.update(image_rel_names)
    expected_zip_entries.update(evidence_rel_names)

    with zipfile.ZipFile(zip_path) as archive:
        entries = {name for name in archive.namelist() if not name.endswith("/")}
    missing_zip = sorted(expected_zip_entries - entries)
    extra_zip = sorted(entries - expected_zip_entries)
    if missing_zip:
        raise ReleaseGateError("release zip 缺少文件: " + ", ".join(missing_zip))
    if extra_zip:
        raise ReleaseGateError("release zip 存在非标准文件: " + ", ".join(extra_zip))

    upload_count = len(expected)
    if upload_count != 10:
        raise ReleaseGateError(f"上传附件数量应为 10，实际 {upload_count}")

    messages.append(f"release 产物门禁通过: {upload_count} 个上传附件，zip 内 {len(entries)} 个文件")
    return [release_dir / name for name in expected], messages


def main():
    parser = argparse.ArgumentParser(description="校验 BikePower C release 产物完整性")
    parser.add_argument("--release-dir", default=str(DEFAULT_RELEASE_DIR))
    parser.add_argument("--version", default="")
    parser.add_argument("--git-sha", default="")
    parser.add_argument("--allow-dirty", action="store_true",
                        help="允许工作区存在未提交改动，仅用于本地预检，不应用于正式发布")
    parser.add_argument("--require-pushed", action="store_true",
                        help="要求当前 HEAD 已推送到上游分支")
    parser.add_argument("--skip-git-check", action="store_true",
                        help="跳过 Git 状态检查，用于离线历史产物或测试样例")
    parser.add_argument("--require-device-evidence", action="store_true",
                        help="要求 release 内含当前版本且 P0 全通过的真机验收证据")
    args = parser.parse_args()

    try:
        artifacts, messages = validate_release_artifacts(
            release_dir=args.release_dir,
            version=args.version or None,
            git_sha=args.git_sha or None,
            allow_dirty=args.allow_dirty,
            require_pushed=args.require_pushed,
            skip_git_check=args.skip_git_check,
            require_device_evidence=args.require_device_evidence,
        )
    except (ReleaseGateError, subprocess.CalledProcessError, zipfile.BadZipFile) as exc:
        print(f"[release-gate] FAIL {exc}")
        return 1

    for message in messages:
        print(f"[release-gate] PASS {message}")
    print("[release-gate] 待发布附件:")
    for path in artifacts:
        print(f"  - {path.name} ({path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
