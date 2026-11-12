#!/usr/bin/env python3
"""工作区审计分类规则的回归测试。"""
import unittest
import subprocess
import tempfile

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts' / 'tools'))

import workspace_audit


class ClassifyTest(unittest.TestCase):
    def test_tracked_modification_requires_confirmation(self):
        self.assertEqual(workspace_audit.classify(' M', 'main/ble_bikepower.c'), 'tracked-change')
        self.assertEqual(workspace_audit.classify('A ', 'docs/dev/49-new.md'), 'tracked-change')

    def test_scratch_is_archivable(self):
        self.assertEqual(workspace_audit.classify('??', 'sdkconfig.board.merged.tmp'), 'scratch')
        self.assertEqual(workspace_audit.classify('??', 'sdkconfig.stale.4.2.1.bak'), 'scratch')
        self.assertEqual(workspace_audit.classify('??', 'final-status.md'), 'scratch')

    def test_tooling_is_ignorable(self):
        self.assertEqual(workspace_audit.classify('??', '.agents/skills/x/SKILL.md'), 'tooling')
        self.assertEqual(workspace_audit.classify('??', 'skills-lock.json'), 'tooling')

    def test_generated_is_ignorable(self):
        self.assertEqual(workspace_audit.classify('??', '__pycache__/x.cpython-311.pyc'), 'generated')

    def test_unknown_untracked_needs_review(self):
        self.assertEqual(workspace_audit.classify('??', 'design-demos/bikepower-ui/pages-refresh/index.html'), 'review')
        self.assertEqual(workspace_audit.classify('??', 'tests/test_sc_send_timing.c'), 'review')

    def test_classify_is_not_destructive(self):
        imports = Path(workspace_audit.__file__).read_text(encoding='utf-8')
        for forbidden in ('unlink(', 'rmtree(', 'shutil.move'):
            self.assertNotIn(forbidden, imports)


class GitStatusTest(unittest.TestCase):
    def test_rename_consumes_source_path_without_phantom_entry(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            def git(*args):
                return subprocess.run(['git', '-C', str(root), *args],
                                      check=True, capture_output=True)
            git('init', '-q')
            (root / 'old name.c').write_text('source\n', encoding='utf-8')
            git('add', 'old name.c')
            git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                'commit', '-qm', 'initial')
            git('mv', 'old name.c', 'new name.c')
            (root / 'unknown.c').write_text('draft\n', encoding='utf-8')
            self.assertEqual(workspace_audit.git_status(root),
                             [('R ', 'new name.c'), ('??', 'unknown.c')])


if __name__ == '__main__':
    unittest.main()
