#!/usr/bin/env python3
"""Functional tests for the release artifact validator."""
import hashlib
import tempfile
import zipfile
from pathlib import Path

import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts" / "tools"))

from check_release_artifacts import (  # noqa: E402
    ReleaseGateError,
    expected_attachment_names,
    validate_release_artifacts,
)

TAG = "[release-artifacts-test]"
VERSION = "9.8.7"
GIT_SHA = "abc1234"


def sha256_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_sidecar(release_dir, name):
    target = release_dir / name
    (release_dir / f"{name}.sha256").write_text(
        f"{sha256_file(target)}  {name}\n",
        encoding="utf-8",
    )


def create_release_dir():
    temp = tempfile.TemporaryDirectory()
    release_dir = Path(temp.name)

    firmware_names = [
        f"bikepower-c-generic-c3-v{VERSION}-stable-fix-g{GIT_SHA}.bin",
        f"bikepower-c-m5stamp-c3-v{VERSION}-stable-fix-g{GIT_SHA}.bin",
    ]
    for name in firmware_names:
        (release_dir / name).write_bytes(f"firmware:{name}".encode("utf-8"))
        write_sidecar(release_dir, name)

    for name in ("test-report.md", "core-flow-endurance.md", "user-manual.md"):
        (release_dir / name).write_text(f"# {name}\n\nok\n", encoding="utf-8")

    image_dir = release_dir / "images"
    image_dir.mkdir()
    (image_dir / "web-flash-serial-port.jpg").write_bytes(b"fake-image")

    package_name = f"bikepower-c-release-v{VERSION}-stable-fix-g{GIT_SHA}.zip"
    manifest_lines = [
        "# BikePower C Release 产物清单",
        f"# 版本: v{VERSION}",
        f"# Git ID: {GIT_SHA}",
    ]

    pre_zip_manifest_targets = [
        *firmware_names,
        *(f"{name}.sha256" for name in firmware_names),
        "test-report.md",
        "core-flow-endurance.md",
        "user-manual.md",
        "images/web-flash-serial-port.jpg",
    ]
    for rel in pre_zip_manifest_targets:
        path = release_dir / rel
        manifest_lines.append(f"{rel}  {path.stat().st_size}  {sha256_file(path)}")
    (release_dir / "manifest.txt").write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")

    zip_entries = [
        *firmware_names,
        *(f"{name}.sha256" for name in firmware_names),
        "manifest.txt",
        "test-report.md",
        "core-flow-endurance.md",
        "user-manual.md",
        "images/web-flash-serial-port.jpg",
    ]
    with zipfile.ZipFile(release_dir / package_name, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for rel in zip_entries:
            archive.write(release_dir / rel, rel)
    write_sidecar(release_dir, package_name)
    for rel in (package_name, f"{package_name}.sha256"):
        path = release_dir / rel
        manifest_lines.append(f"{rel}  {path.stat().st_size}  {sha256_file(path)}")
    (release_dir / "manifest.txt").write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")

    return temp, release_dir


def expect_error(message, fn):
    try:
        fn()
    except ReleaseGateError as exc:
        if message in str(exc):
            print(f"{TAG} PASS {message}")
            return
        print(f"{TAG} FAIL 失败原因不匹配: {exc}")
        raise
    raise AssertionError(f"expected ReleaseGateError containing {message!r}")


def main():
    temp, release_dir = create_release_dir()
    try:
        artifacts, _ = validate_release_artifacts(
            release_dir=release_dir,
            version=VERSION,
            git_sha=GIT_SHA,
            skip_git_check=True,
        )
        assert len(artifacts) == 10
        print(f"{TAG} PASS 标准 release 样例通过")

        tampered = release_dir / f"bikepower-c-generic-c3-v{VERSION}-stable-fix-g{GIT_SHA}.bin"
        tampered.write_bytes(b"tampered")
        expect_error("SHA256 不匹配", lambda: validate_release_artifacts(
            release_dir=release_dir,
            version=VERSION,
            git_sha=GIT_SHA,
            skip_git_check=True,
        ))
    finally:
        temp.cleanup()

    temp, release_dir = create_release_dir()
    try:
        package_name = f"bikepower-c-release-v{VERSION}-stable-fix-g{GIT_SHA}.zip"
        with zipfile.ZipFile(release_dir / package_name, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.write(release_dir / "manifest.txt", "manifest.txt")
        write_sidecar(release_dir, package_name)
        manifest = release_dir / "manifest.txt"
        lines = []
        for raw in manifest.read_text(encoding="utf-8").splitlines():
            if raw.startswith(package_name + "  ") or raw.startswith(package_name + ".sha256  "):
                path = release_dir / raw.split()[0]
                lines.append(f"{path.name}  {path.stat().st_size}  {sha256_file(path)}")
            else:
                lines.append(raw)
        manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")
        expect_error("release zip 缺少文件", lambda: validate_release_artifacts(
            release_dir=release_dir,
            version=VERSION,
            git_sha=GIT_SHA,
            skip_git_check=True,
        ))
    finally:
        temp.cleanup()

    print(f"{TAG} PASS: release 产物校验功能测试通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
