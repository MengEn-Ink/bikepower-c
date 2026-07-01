import os
import sys
import tempfile
import unittest
from collections import deque
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import serial_daemon


class SerialDaemonLogProcessingTest(unittest.TestCase):
    def test_reboot_banner_adds_separator_without_splitting_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            log_path = os.path.join(tmp, "device_20260701_120000.log")
            recent = deque(maxlen=serial_daemon.CRASH_CONTEXT_LINES)
            crash_keys = set()

            with mock.patch.object(serial_daemon, "timestamp", return_value="2026-07-01 12:00:00"):
                new_path = serial_daemon.process_serial_line(
                    log_path,
                    "ESP-ROM:esp32c3-api1-20210207",
                    False,
                    True,
                    recent,
                    crash_keys,
                    False,
                )

            self.assertEqual(new_path, log_path)
            with open(log_path, encoding="utf-8") as f:
                content = f.read()
            self.assertIn("----- DEVICE REBOOT", content)
            self.assertIn("ESP-ROM:esp32c3-api1-20210207", content)

    def test_reboot_banner_splits_file_when_enabled(self):
        with tempfile.TemporaryDirectory() as tmp:
            old_path = os.path.join(tmp, "device_20260701_120000.log")
            new_path = os.path.join(tmp, "device_20260701_120001.log")
            recent = deque(maxlen=serial_daemon.CRASH_CONTEXT_LINES)
            crash_keys = set()

            with mock.patch.object(serial_daemon, "timestamp", return_value="2026-07-01 12:00:01"), \
                    mock.patch.object(serial_daemon, "get_log_filepath", return_value=new_path):
                result = serial_daemon.process_serial_line(
                    old_path,
                    "rst:0x1 (POWERON),boot:0xc (SPI_FAST_FLASH_BOOT)",
                    False,
                    True,
                    recent,
                    crash_keys,
                    True,
                )

            self.assertEqual(result, new_path)
            with open(old_path, encoding="utf-8") as f:
                old_content = f.read()
            with open(new_path, encoding="utf-8") as f:
                new_content = f.read()
            self.assertIn("DEVICE REBOOT", old_content)
            self.assertIn("rst:0x1", new_content)

    def test_crash_line_writes_context_snapshot(self):
        with tempfile.TemporaryDirectory() as tmp:
            log_path = os.path.join(tmp, "device_20260701_120000.log")
            snapshot_path = os.path.join(tmp, "crash_20260701_120002.log")
            recent = deque(["I (10) APP: before"], maxlen=serial_daemon.CRASH_CONTEXT_LINES)
            crash_keys = set()

            with mock.patch.object(serial_daemon, "timestamp", return_value="2026-07-01 12:00:02"), \
                    mock.patch.object(serial_daemon, "get_crash_snapshot_filepath", return_value=snapshot_path):
                serial_daemon.process_serial_line(
                    log_path,
                    "E (20) task_wdt: Task watchdog got triggered",
                    False,
                    True,
                    recent,
                    crash_keys,
                    False,
                )

            self.assertTrue(os.path.exists(snapshot_path))
            with open(snapshot_path, encoding="utf-8") as f:
                content = f.read()
            self.assertIn("CRASH SNAPSHOT", content)
            self.assertIn("I (10) APP: before", content)
            self.assertIn("Task watchdog got triggered", content)


if __name__ == "__main__":
    unittest.main()
