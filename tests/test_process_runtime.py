#!/usr/bin/env python3
"""测试进程退出与信号前置回归，不访问实体设备。"""
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import unittest
from unittest.mock import Mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('test_process_runtime', ROOT / 'scripts/tools/test_process_runtime.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class ProcessRuntimeTests(unittest.TestCase):
    def test_already_finished_does_not_signal(self):
        process = Mock()
        process.poll.return_value = 0
        process.wait.return_value = 0
        self.assertEqual(tool.stop_process(process), 0)
        process.terminate.assert_not_called()
        process.kill.assert_not_called()

    def test_graceful_exit_is_reaped(self):
        process = Mock()
        process.poll.return_value = None
        process.wait.return_value = -15
        self.assertEqual(tool.stop_process(process, 2), -15)
        process.terminate.assert_called_once()
        process.kill.assert_not_called()
        process.wait.assert_called_once_with(timeout=2)

    def test_timeout_reaps_but_does_not_claim_state_saved(self):
        process = Mock()
        process.poll.return_value = None
        process.wait.side_effect = [subprocess.TimeoutExpired('fixture', 1), -9]
        with self.assertRaisesRegex(RuntimeError, '状态落盘未获证明'):
            tool.stop_process(process, 1)
        process.kill.assert_called_once()
        self.assertEqual(process.wait.call_count, 2)

    @unittest.skipUnless(os.name == 'posix', 'POSIX信号继承')
    def test_ignored_and_blocked_term_is_normalized_only_in_child(self):
        script = "import sys,signal; sys.path.insert(0,sys.argv[1]); from test_process_runtime import prepare_test_signals; signal.signal(signal.SIGTERM,signal.SIG_IGN); signal.pthread_sigmask(signal.SIG_BLOCK,{signal.SIGTERM}); prepare_test_signals(); assert signal.getsignal(signal.SIGTERM)==signal.SIG_DFL; assert signal.SIGTERM not in signal.pthread_sigmask(signal.SIG_BLOCK,set())"
        before = signal.getsignal(signal.SIGTERM)
        mask = signal.pthread_sigmask(signal.SIG_BLOCK, set())
        subprocess.run([sys.executable, '-c', script, str(ROOT / 'scripts/tools')], check=True, timeout=5)
        self.assertEqual(signal.getsignal(signal.SIGTERM), before)
        self.assertEqual(signal.pthread_sigmask(signal.SIG_BLOCK, set()), mask)


if __name__ == '__main__':
    unittest.main()
