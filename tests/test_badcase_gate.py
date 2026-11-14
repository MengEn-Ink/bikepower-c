#!/usr/bin/env python3
"""badcase 门禁功能样例：每条规则至少一个正例和一个反例。"""
import subprocess
import tempfile
import unittest
from pathlib import Path

import importlib.util

GATE = Path(__file__).resolve().parents[1] / "scripts/tools/badcase_gate.py"
spec = importlib.util.spec_from_file_location("badcase_gate", GATE)
badcase = importlib.util.module_from_spec(spec)
spec.loader.exec_module(badcase)


def run(cwd, *args):
    subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True)


class Repo:
    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        run(self.root, "init", "-q")
        run(self.root, "config", "user.email", "test@example.com")
        run(self.root, "config", "user.name", "test")
        run(self.root, "config", "commit.gpgsign", "false")

    def write(self, rel, text):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def commit_all(self):
        run(self.root, "add", "-A")
        run(self.root, "commit", "-q", "-m", "fixture")

    def commit(self, *paths):
        run(self.root, "add", "--", *paths)
        run(self.root, "commit", "-q", "-m", "fixture")

    def close(self):
        self.tmp.cleanup()


class BadcaseGateTests(unittest.TestCase):
    def setUp(self):
        self.repo = Repo()
        self.repo.write("README.md", "# fixture\n")
        self.repo.commit_all()

    def tearDown(self):
        self.repo.close()

    def test_web_asset_untracked_is_flagged(self):
        self.repo.write("tools/web/page.html", '<link rel="stylesheet" href="./x.css">')
        self.repo.write("tools/web/x.css", "body{}\n")
        self.repo.commit("tools/web/page.html")
        errors = badcase.check_web_assets(self.repo.root)
        self.assertTrue(any("BC-01" in e and "未跟踪" in e for e in errors), errors)

    def test_web_asset_missing_is_flagged(self):
        self.repo.write("tools/web/page.html", '<script src="./y.js"></script>')
        self.repo.commit_all()
        errors = badcase.check_web_assets(self.repo.root)
        self.assertTrue(any("BC-01" in e and "不存在" in e for e in errors), errors)

    def test_web_asset_tracked_passes(self):
        self.repo.write("tools/web/page.html", '<script src="./y.js"></script>')
        self.repo.write("tools/web/y.js", "void 0;\n")
        self.repo.commit_all()
        self.assertEqual([], badcase.check_web_assets(self.repo.root))

    def test_doc_link_to_untracked_file_is_flagged(self):
        self.repo.write("docs/a.md", "见[方案](b.md)。\n")
        self.repo.write("docs/b.md", "# b\n")
        self.repo.commit("docs/a.md")
        errors = badcase.check_doc_links(self.repo.root)
        self.assertTrue(any("BC-02" in e and "docs/b.md" in e for e in errors), errors)

    def test_ps1_testpath_and_is_flagged(self):
        self.repo.write("scripts/x.ps1", "if (Test-Path $p -and -not $x) { 1 }\n")
        self.repo.write("scripts/good.ps1", "if ((Test-Path $p) -and (-not $x)) { 1 }\n")
        self.repo.commit_all()
        errors = badcase.check_ps1_pattern(self.repo.root)
        self.assertEqual(1, len(errors), errors)
        self.assertIn("BC-03", errors[0])

    def test_core_bare_true_is_flagged(self):
        run(self.repo.root, "config", "core.bare", "true")
        errors = badcase.check_git_bare(self.repo.root)
        self.assertTrue(errors and "BC-04" in errors[0], errors)
        run(self.repo.root, "config", "core.bare", "false")
        self.assertEqual([], badcase.check_git_bare(self.repo.root))

    def test_dirty_worktree_is_skipped_to_avoid_false_positive(self):
        self.repo.write("docs/a.md", "见[方案](b.md)。\n")
        self.repo.commit_all()
        self.repo.write("docs/a.md", "见[方案](b.md)。\n未提交改动\n")
        self.assertEqual([], badcase.check_doc_links(self.repo.root))


if __name__ == "__main__":
    unittest.main(verbosity=2)
