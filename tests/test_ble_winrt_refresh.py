#!/usr/bin/env python3
"""WinRT 刷新门禁的 Host 负例；不访问硬件。"""
import unittest
from ble_winrt_refresh import EXPECTED_FW_PREFIXES, _looks_stale


class WinrtRefreshTests(unittest.TestCase):
    def test_current_config_version_is_accepted(self):
        self.assertTrue(EXPECTED_FW_PREFIXES)
        fw = next(iter(EXPECTED_FW_PREFIXES)).encode()
        self.assertEqual(_looks_stale(b"0" * 12, fw), [])

    def test_stale_meta_or_old_firmware_is_rejected(self):
        self.assertTrue(_looks_stale(b"short", b"generic-c3-v4.4.0"))
        self.assertTrue(any("DIS固件异常" in reason for reason in
                            _looks_stale(b"0" * 12, b"generic-c3-v4.3.3")))


if __name__ == "__main__":
    unittest.main()
