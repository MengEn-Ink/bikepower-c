#!/usr/bin/env python3
"""网页刷写站点生成器的离线回归测试。"""

import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MODULE_PATH = ROOT / "scripts" / "tools" / "prepare_web_flash_site.py"
SPEC = importlib.util.spec_from_file_location("prepare_web_flash_site", MODULE_PATH)
web_flash = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(web_flash)


class WebFlashSiteTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.base = Path(self.temp_dir.name)
        self.source = self.base / "source"
        self.source.mkdir()
        (self.source / "ble-monitor.html").write_text(
            "<!doctype html><title>BikePower</title>\n", encoding="utf-8"
        )
        self.assets = self.base / "assets"
        self.assets.mkdir()

    def tearDown(self):
        self.temp_dir.cleanup()

    def make_asset(self, name, payload):
        path = self.assets / name
        path.write_bytes(payload)
        return {"name": name, "browser_download_url": path.as_uri()}

    def make_release(self, bad_board=""):
        assets = []
        for board, payload in (
            ("generic-c3", b"generic merged firmware"),
            ("m5stamp-c3", b"m5stamp merged firmware"),
        ):
            name = f"bikepower-c-{board}-v4.1.1-stable-fix-gabcdef1.bin"
            assets.append(self.make_asset(name, payload))
            digest = hashlib.sha256(payload).hexdigest()
            if board == bad_board:
                digest = "0" * 64
            assets.append(self.make_asset(name + ".sha256", f"{digest}  {name}\n".encode()))

        old_name = "bikepower-c-generic-c3-v4.1.0-stable-fix-gabcdef0.bin"
        assets.extend(
            [
                self.make_asset(old_name, b"old firmware"),
                self.make_asset(
                    old_name + ".sha256",
                    (hashlib.sha256(b"old firmware").hexdigest() + f"  {old_name}\n").encode(),
                ),
                self.make_asset("bikepower-c-release-v4.1.1.zip", b"not a flash image"),
            ]
        )
        return {
            "_provider": "fixture",
            "tag_name": "v4.1.1",
            "published_at": "2026-08-11T08:00:00Z",
            "html_url": "https://example.invalid/releases/v4.1.1",
            "assets": assets,
        }

    def test_generates_verified_dual_board_manifests(self):
        site = web_flash.initialize_site(self.source, self.base / "site")
        latest = web_flash.materialize_release(
            site, self.make_release(), allow_file=True
        )

        self.assertTrue((site / "index.html").is_file())
        self.assertTrue(latest["available"])
        self.assertEqual(latest["version"], "4.1.1")
        self.assertEqual(set(latest["boards"]), {"generic-c3", "m5stamp-c3"})
        self.assertFalse(any("v4.1.0" in path.name for path in (site / "firmware").iterdir()))

        disk_latest = json.loads((site / "firmware" / "latest.json").read_text())
        self.assertEqual(disk_latest["provider"], "fixture")
        for board in ("generic-c3", "m5stamp-c3"):
            board_info = disk_latest["boards"][board]
            firmware = site / board_info["download"]
            self.assertTrue(firmware.is_file())
            self.assertEqual(hashlib.sha256(firmware.read_bytes()).hexdigest(), board_info["sha256"])
            manifest = json.loads((site / board_info["manifest"]).read_text())
            self.assertEqual(manifest["builds"][0]["chipFamily"], "ESP32-C3")
            self.assertEqual(manifest["builds"][0]["parts"][0]["offset"], 0)
            self.assertEqual(manifest["builds"][0]["parts"][0]["path"], firmware.name)

    def test_rejects_sha256_mismatch(self):
        site = web_flash.initialize_site(self.source, self.base / "site")
        with self.assertRaisesRegex(web_flash.ReleaseValidationError, "SHA256 校验失败"):
            web_flash.materialize_release(
                site, self.make_release(bad_board="m5stamp-c3"), allow_file=True
            )

    def test_missing_release_keeps_monitor_and_disables_flash(self):
        site = web_flash.initialize_site(self.source, self.base / "site")
        latest = web_flash.write_unavailable(site, ["GitHub 没有可用 Release"])

        self.assertFalse(latest["available"])
        self.assertTrue((site / "ble-monitor.html").is_file())
        disk_latest = json.loads((site / "firmware" / "latest.json").read_text())
        self.assertEqual(disk_latest["boards"], {})
        self.assertIn("GitHub", disk_latest["reason"])

    def test_cli_is_github_only_and_can_require_firmware(self):
        args = web_flash.parse_args(
            ["--site-dir", str(self.base / "site"), "--require-firmware"]
        )

        self.assertEqual(args.github_repo, "MengEn-Ink/bikepower-c")
        self.assertTrue(args.require_firmware)
        self.assertEqual(
            set(vars(args)),
            {"source_dir", "site_dir", "github_repo", "require_firmware", "release_json"},
        )

    def test_rejects_path_traversal_and_nested_output(self):
        with self.assertRaises(web_flash.ReleaseValidationError):
            web_flash.safe_filename("../firmware.bin")
        with self.assertRaises(RuntimeError):
            web_flash.initialize_site(self.source, self.source / "site")

        existing = self.base / "existing-output"
        existing.mkdir()
        sentinel = existing / "keep.txt"
        sentinel.write_text("do not delete", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "拒绝递归覆盖"):
            web_flash.initialize_site(self.source, existing)
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "do not delete")

    def test_requires_both_current_board_assets(self):
        release = self.make_release()
        release["assets"] = [
            asset for asset in release["assets"] if "m5stamp-c3" not in asset["name"]
        ]
        with self.assertRaisesRegex(web_flash.ReleaseUnavailable, "m5stamp-c3"):
            web_flash.select_firmware_assets(release)


if __name__ == "__main__":
    unittest.main(verbosity=2)
