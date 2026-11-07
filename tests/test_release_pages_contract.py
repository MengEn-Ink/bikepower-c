#!/usr/bin/env python3
"""v4.3.3 发版/Pages 契约的独立回归（发行方视角，fixture 非真实固件，不交付）。

锁定三条本版必须成立、而现有测试未直接覆盖的契约：

1. generic-only 正式产物（8 个上传附件 + generic-c3 P0 证据）能通过严格门禁，
   且 m5stamp-c3 不混入上传清单。
2. Pages 站点生成在 ``--require-firmware`` 下，无合法 Release 时 fail-closed
   （main 非零退出），不得把"无固件"站点部署成可刷写。
3. 历史版本索引只采用正式 Release，draft / prerelease 不进入回滚列表。

复用 scripts/tools 下的真实校验函数；不复制其实现。
"""

import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts" / "tools"))

WEB_PATH = ROOT / "scripts" / "tools" / "prepare_web_flash_site.py"
_spec = importlib.util.spec_from_file_location("prepare_web_flash_site", WEB_PATH)
web_flash = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(web_flash)

from check_release_artifacts import validate_release_artifacts  # noqa: E402
from device_acceptance import (  # noqa: E402
    P0_ITEM_IDS,
    new_record,
    set_item,
    write_record,
)

VERSION = "4.3.3"
GIT_SHA = "1a2b3c4"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def build_generic_only_release_dir(base: Path) -> Path:
    """构造仅 generic-c3 的合法 release 目录（8 个上传附件 + evidence + images）。"""
    rd = base / "release"
    rd.mkdir()

    firmware_name = f"generic-c3-v{VERSION}.bin"
    firmware = f"generic merged fixture {VERSION}".encode()
    (rd / firmware_name).write_bytes(firmware)
    (rd / f"{firmware_name}.sha256").write_text(
        f"{sha256_bytes(firmware)}  {firmware_name}\n", encoding="utf-8"
    )

    docs = ["test-report.md", "core-flow-endurance.md", "user-manual.md"]
    for name in docs:
        (rd / name).write_text(f"# {name}\n\nfixture\n", encoding="utf-8")

    image_dir = rd / "images"
    image_dir.mkdir()
    (image_dir / "generic-c3-board.jpg").write_bytes(b"fake-image")

    evidence_rel = f"evidence/device-acceptance-generic-c3.json"
    evidence = new_record(VERSION, GIT_SHA, "generic-c3", "fixture-tester")
    for item_id in P0_ITEM_IDS:
        set_item(evidence, item_id, "PASS", f"{item_id} fixture verified")
    write_record(rd / evidence_rel, evidence)

    manifest_lines = [
        "# v4.3.3 generic-only release fixture",
        f"# version: {VERSION}",
        f"# git: {GIT_SHA}",
    ]
    tracked = [firmware_name, f"{firmware_name}.sha256", *docs,
               "images/generic-c3-board.jpg", evidence_rel]
    for rel in tracked:
        p = rd / rel
        manifest_lines.append(f"{rel}  {p.stat().st_size}  {sha256_bytes(p.read_bytes())}")

    zip_name = f"bikepower-c-v{VERSION}.zip"
    with zipfile.ZipFile(rd / zip_name, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for rel in tracked:
            z.write(rd / rel, rel)

    # manifest.txt 先写盘，再连同其本身打进 zip，最后补 zip 两条记录并重写。
    (rd / "manifest.txt").write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")
    with zipfile.ZipFile(rd / zip_name, "a", compression=zipfile.ZIP_DEFLATED) as z:
        z.write(rd / "manifest.txt", "manifest.txt")

    (rd / f"{zip_name}.sha256").write_text(
        f"{sha256_bytes((rd / zip_name).read_bytes())}  {zip_name}\n", encoding="utf-8"
    )
    for rel in (zip_name, f"{zip_name}.sha256"):
        p = rd / rel
        manifest_lines.append(f"{rel}  {p.stat().st_size}  {sha256_bytes(p.read_bytes())}")
    (rd / "manifest.txt").write_text("\n".join(manifest_lines) + "\n", encoding="utf-8")
    return rd


class GenericOnlyGateTest(unittest.TestCase):
    def test_generic_only_release_passes_strict_gate_and_excludes_m5stamp(self):
        with tempfile.TemporaryDirectory() as tmp:
            rd = build_generic_only_release_dir(Path(tmp))
            artifacts, messages = validate_release_artifacts(
                release_dir=rd,
                version=VERSION,
                git_sha=GIT_SHA,
                skip_git_check=True,
                require_device_evidence=True,
                boards=("generic-c3",),
            )
            names = {p.name for p in artifacts}
            self.assertEqual(len(artifacts), 8, "generic-only 上传附件必须为 8 个")
            self.assertIn(f"generic-c3-v{VERSION}.bin", names)
            self.assertNotIn(
                "m5stamp-c3", " ".join(names), "m5stamp-c3 不得混入 generic-only 发布"
            )
            self.assertTrue(
                any("generic-c3 真机验收证据通过" in m for m in messages)
            )


class RequireFirmwareFailClosedTest(unittest.TestCase):
    def _site(self, base: Path) -> Path:
        src = base / "src"
        src.mkdir()
        (src / "ble-monitor.html").write_text(
            "<!doctype html><title>x</title>\n", encoding="utf-8"
        )
        return base / "site"

    def test_no_release_fails_closed_when_require_firmware(self):
        with tempfile.TemporaryDirectory() as tmp:
            site = self._site(Path(tmp))
            argv = ["--site-dir", str(site), "--require-firmware"]
            with patch.object(web_flash, "fetch_github_release", return_value=None):
                # 无合法 Release 且 --require-firmware：必须抛错 fail-closed，
                # 不允许生成可刷写站点（main 在 __main__ 包装层转成 SystemExit(1)）。
                with self.assertRaises(web_flash.ReleaseUnavailable):
                    web_flash.main(argv)
            self.assertFalse((site / "firmware" / "latest.json").exists(),
                             "fail-closed 不得写出 latest.json")

    def test_draft_or_prerelease_excluded_from_rollback_index(self):
        releases = [
            {"tag_name": "v4.3.3", "draft": False, "prerelease": False},
            {"tag_name": "v4.3.4-rc1", "draft": False, "prerelease": True},
            {"tag_name": "v4.4.0-draft", "draft": True, "prerelease": False},
            {"tag_name": "v4.3.2", "draft": False, "prerelease": False},
        ]
        with patch.object(web_flash, "request_json", return_value=releases):
            kept = web_flash.fetch_github_release_list("MengEn-Ink/bikepower-c", "", 6)
        kept_tags = [r["tag_name"] for r in kept]
        self.assertEqual(kept_tags, ["v4.3.3", "v4.3.2"])
        self.assertNotIn("v4.3.4-rc1", kept_tags)
        self.assertNotIn("v4.4.0-draft", kept_tags)


if __name__ == "__main__":
    unittest.main(verbosity=2)
