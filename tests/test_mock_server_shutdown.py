#!/usr/bin/env python3
"""验证 mock 即使继承忽略 SIGTERM 的父进程也可正常退出。"""
import os
from pathlib import Path
import signal
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


class MockShutdownTest(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix", "仅 POSIX 支持继承 SIGTERM 忽略状态")
    def test_inherited_ignored_sigterm_is_restored(self):
        launcher = (
            "import os,signal,sys; signal.signal(signal.SIGTERM, signal.SIG_IGN); "
            "signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGTERM}); "
            "os.execv(sys.executable, [sys.executable, sys.argv[1], '--port', '0'])"
        )
        proc = subprocess.Popen(
            [sys.executable, "-c", launcher, str(ROOT / "tests/mock_server.py")],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        try:
            self.assertIn("Mock server:", proc.stdout.readline())
            proc.terminate()
            self.assertEqual(proc.wait(timeout=5), 0)
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main()
