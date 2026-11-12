#!/usr/bin/env python3
"""依赖诊断缺失/超时回归，不安装依赖或操作设备。"""
import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('doctor', ROOT/'scripts/tools/test_doctor.py')
doctor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(doctor)


class DoctorTests(unittest.TestCase):
    def diagnose(self, command=None, result=None):
        with patch.object(doctor.importlib.util, 'find_spec', return_value=None), \
             patch.object(doctor.shutil, 'which', return_value=command), \
             patch.object(doctor.subprocess, 'run', **result) as run:
            checks = {item['name']: item for item in doctor.diagnose()}
        return checks, run

    def test_missing_tools_are_explicit(self):
        checks, run = self.diagnose(result={'return_value': None})
        for name in ('python.bleak', 'node', 'node.playwright', 'node.chromium', 'python.chromium', 'host.openssl'):
            self.assertEqual(checks[name]['status'], 'MISSING')
        run.assert_not_called()

    def test_probe_errors_do_not_hide_other_results(self):
        for error in (OSError('missing executable'), subprocess.TimeoutExpired('probe', 10)):
            checks, _ = self.diagnose('probe', {'side_effect': error})
            self.assertEqual(checks['node.playwright']['status'], 'MISSING')
            self.assertEqual(checks['node.jsdom']['status'], 'MISSING')
            self.assertEqual(checks['host.openssl']['status'], 'MISSING')

    def test_compile_failure_remains_missing(self):
        checks, run = self.diagnose('probe', {'return_value': subprocess.CompletedProcess([], 1, '', '')})
        self.assertEqual(checks['host.openssl']['status'], 'MISSING')
        for call in run.call_args_list:
            self.assertIsInstance(call.args[0], list)
            self.assertNotIn('shell', call.kwargs)


if __name__ == '__main__':
    unittest.main()
