#!/usr/bin/env python3
"""证据包瘦身规则的回归测试：只清理可重建镜像，报告永远保留。"""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts' / 'tools'))

import report_maintenance


class PlanTest(unittest.TestCase):
    def _session(self, base, name, with_report=True):
        session = base / 'virtual-board' / name
        session.mkdir(parents=True, exist_ok=True)
        if with_report:
            (session / 'report.json').write_text('{"status": "PASS", "firmware_sha256": "abc"}', encoding='utf-8')
        (session / 'report.md').write_text('# report', encoding='utf-8')
        (session / 'gdb.log').write_text('log', encoding='utf-8')
        (session / 'firmware.bin').write_bytes(b'x' * 2048)
        return session

    def test_recent_sessions_are_kept(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            for index in range(4):
                session = self._session(base, f'session-{index}')
                # 让目录 mtime 递增，后创建的更新
                import os
                os.utime(session, (1000 + index, 1000 + index))
            targets = report_maintenance.plan(root=base, keep=2)
            self.assertEqual(len(targets), 2)
            self.assertTrue(all(t.name == 'firmware.bin' for t in targets))

    def test_reports_are_never_selected(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            self._session(base, 'session-old')
            targets = report_maintenance.plan(root=base, keep=0)
            names = {t.name for t in targets}
            self.assertEqual(names, {'firmware.bin'})

    def test_session_without_report_is_untouched(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            self._session(base, 'session-noreport', with_report=False)
            targets = report_maintenance.plan(root=base, keep=0)
            self.assertEqual(targets, [])

    def test_summary_counts_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            self._session(base, 'session-a')
            info = report_maintenance.summary(root=base)
            self.assertGreater(info['total_bytes'], 0)
            self.assertEqual(info['sessions'], 1)


if __name__ == '__main__':
    unittest.main()
