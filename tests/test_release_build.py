#!/usr/bin/env python3
"""发行入口回归：固定证据、构建缓存和产物；不烧录、不发布。"""
import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "tools"))

import dev_workflow as dw
import release_build as rb
from check_release_artifacts import validate_release_artifacts
from device_acceptance import AcceptanceError, P0_ITEM_IDS, new_record, set_item

SHA = "a" * 40
VERSION = "4.4.0"


class ReleaseBuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        (self.repo / "main").mkdir(parents=True)
        (self.repo / "main" / "bikepower_config.h").write_text(
            f'#define FIRMWARE_VERSION "{VERSION}"\n', encoding="utf-8")
        (self.repo / "sdkconfig.defaults").write_text("", encoding="utf-8")
        idf = self.root / "idf"
        (idf / "tools").mkdir(parents=True)
        (idf / "tools" / "idf.py").touch()
        tools = self.root / "tools"
        tools.mkdir()
        self.ctx = dw.Context(repo=self.repo, config={
            "cache_root": str(self.root / "cache"), "idf_path": str(idf),
            "tools_root": str(tools)})
        self.state = self.root / "state.json"
        self.record = new_record(VERSION, SHA[:7], "generic-c3", "fixture")
        for item in P0_ITEM_IDS:
            set_item(self.record, item, "PASS", "固定主机夹具，非真实验收")

    def test_empty_or_not_run_evidence_rejected(self):
        empty = copy.deepcopy(self.record)
        empty["items"] = {}
        pending = new_record(VERSION, SHA[:7], "generic-c3", "fixture")
        for record in (empty, pending):
            with self.subTest(record=record["items"]), patch.object(rb, "stage"):
                with self.assertRaises((SystemExit, AcceptanceError)):
                    rb.validate_evidence(record, VERSION, self.state)

    def test_identity_evidence_rejected(self):
        for field, value in (("git_sha", "deadbee"), ("board", "m5stamp-c3")):
            record = copy.deepcopy(self.record)
            record[field] = value
            with self.subTest(field=field), patch.object(rb, "stage"):
                with self.assertRaises((SystemExit, AcceptanceError)):
                    rb.validate_evidence(record, VERSION, self.state,
                                         git_sha=SHA[:7], board="generic")

    def test_first_dirty_line_and_generator(self):
        for rel in ("main/led.c", "scripts/inline_static.py"):
            raw = f" M {rel}\n"
            with self.subTest(rel=rel), patch.object(subprocess, "check_output", return_value=raw):
                self.assertIn(rel, rb.dirty_source_files())

    def cache(self):
        cache = self.ctx.mode_dir("production")
        cache.mkdir(parents=True)
        for rel in dw.ARTIFACT_FILES:
            path = cache / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(rel.encode())
        merged = dw._merged_defaults(self.ctx, "generic")
        snap, _ = dw.sync_source_snapshot(self.ctx, "production", dw.snapshot_files(self.ctx))
        (snap / "sdkconfig").write_text("固定夹具配置\n", encoding="utf-8")
        dw.write_state(self.ctx, "production", stage="done", status="PASS",
                       board="generic", git_head=SHA,
                       source_fingerprint=dw.source_fingerprint(self.ctx)[0],
                       config_bucket=dw._config_bucket("production", "generic", merged, {}),
                       artifact_hashes=dw.artifact_hashes(cache),
                       sdkconfig_hash=dw._hash_file(snap / "sdkconfig"))
        return cache

    def test_cache_rejects_all_drift(self):
        cache = self.cache()
        good = dw.read_state(self.ctx, "production")
        good.pop("mode")
        with patch.object(dw, "git_identity", return_value={"head": SHA, "dirty": "clean"}):
            self.assertEqual(rb.validate_cache(self.ctx, "generic"), cache)
            for key, value in (("status", "FAIL"), ("board", "m5stamp"),
                               ("git_head", "b" * 40), ("source_fingerprint", "bad"),
                               ("config_bucket", "bad"), ("artifact_hashes", {})):
                with self.subTest(key=key):
                    dw.write_state(self.ctx, "production", **{**good, key: value})
                    with self.assertRaises(SystemExit):
                        rb.validate_cache(self.ctx, "generic")
            dw.write_state(self.ctx, "production", **good)
            source = self.repo / "main" / "bikepower_config.h"
            snapshot = cache / "source-snapshot" / "main" / "bikepower_config.h"
            config = cache / "source-snapshot" / "sdkconfig"
            for path in (source, snapshot, config):
                before = path.read_bytes()
                path.write_bytes(b"tampered")
                with self.subTest(path=str(path)), self.assertRaises(SystemExit):
                    rb.validate_cache(self.ctx, "generic")
                path.write_bytes(before)
            (cache / dw.ARTIFACT_FILES[0]).write_bytes(b"tampered")
            with self.assertRaises(SystemExit):
                rb.validate_cache(self.ctx, "generic")

    def test_package_matches_authoritative_validator(self):
        out = self.root / "external-output"
        out.mkdir()
        name = f"generic-c3-v{VERSION}.bin"
        (out / name).write_bytes(b"merged fixture")
        for rel in ("test-report.md", "core-flow-endurance.md", "user-manual.md"):
            (out / rel).write_text("主机夹具\n", encoding="utf-8")
        (out / "images").mkdir()
        (out / "images" / "guide.svg").write_text("<svg/>", encoding="utf-8")
        (out / "evidence").mkdir()
        (out / "evidence" / "device-acceptance-generic-c3.json").write_text(
            json.dumps(self.record), encoding="utf-8")
        rb.write_manifest(out, VERSION, "generic", self.state)
        rb.make_zip(out, VERSION, self.state)
        attachments, _ = validate_release_artifacts(
            out, VERSION, SHA[:7], skip_git_check=True,
            require_device_evidence=True, boards=("generic-c3",))
        self.assertEqual(len(attachments), 8)

    def test_context_and_candidate_isolation(self):
        self.cache()
        with patch.object(dw, "git_identity", return_value={"head": SHA, "dirty": "clean"}), \
                patch.object(rb, "dirty_source_files", return_value=["main/led.c"]), \
                patch.object(rb, "merge_image"), patch.object(rb, "generate_docs"), \
                patch.object(rb, "copy_endurance"), patch.object(rb, "write_manifest"), \
                patch.object(rb, "make_zip"), patch.object(rb, "print_next_steps"), \
                patch.object(rb, "idf_python", return_value=Path(sys.executable)), \
                patch("check_release_artifacts.validate_release_artifacts"):
            rb.main(["--version", VERSION, "--release-root", str(self.root / "output"),
                     "--allow-dirty-source", "--skip-build"], ctx=self.ctx)
        self.assertFalse((self.root / "output" / VERSION).exists())
        candidates = self.root / "output" / "candidates"
        self.assertTrue(candidates.exists())
        self.assertFalse(any(p.name == "release-state.json" for p in candidates.rglob("*")))

    def test_invalid_evidence_stops_before_build(self):
        for defect in ("git", "board", "missing", "pending"):
            record = copy.deepcopy(self.record)
            if defect == "git":
                record["git_sha"] = "deadbee"
            elif defect == "board":
                record["board"] = "m5stamp-c3"
            elif defect == "missing":
                del record["items"]["ble_cycling"]
            else:
                record["items"]["ble_cycling"]["status"] = "NOT_RUN"
            evidence = self.root / "evidence.json"
            evidence.write_text(json.dumps(record), encoding="utf-8")
            with self.subTest(defect=defect), \
                    patch.object(dw, "git_identity", return_value={"head": SHA, "dirty": "clean"}), \
                    patch.object(rb, "dirty_source_files", return_value=[]), \
                    patch.object(rb, "build") as build:
                with self.assertRaises(SystemExit):
                    rb.main(["--version", VERSION, "--evidence", str(evidence),
                             "--release-root", str(self.root / "output")], ctx=self.ctx)
                build.assert_not_called()
                self.assertFalse((self.root / "output").exists())

    def test_dev_entry_passes_context(self):
        with patch.object(rb, "main", return_value=0) as main:
            self.assertEqual(dw.cmd_release(self.ctx, ["--version", VERSION]), 0)
        main.assert_called_once_with(["--version", VERSION], ctx=self.ctx)


if __name__ == "__main__":
    unittest.main(verbosity=2)
