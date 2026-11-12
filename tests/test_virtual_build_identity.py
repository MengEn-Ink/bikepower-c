#!/usr/bin/env python3
"""虚拟候选实际 SDK 身份回归，无设备依赖。"""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('virtual_board', ROOT/'scripts/tools/virtual_board.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class IdentityTests(unittest.TestCase):
    def test_sdk_is_from_app_descriptor_not_install_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'flash.bin'
            image = bytearray(0x10200)
            image[0x10020:0x10024] = b'\x32\x54\xcd\xab'
            image[0x10090:0x10090+6] = b'v5.5.6'
            path.write_bytes(image)
            self.assertEqual(tool.firmware_sdk_version(path), 'v5.5.6')
            self.assertEqual(tool.LOCK['esp_idf'], '5.5.4')
            path.write_bytes(image[0x10000:])
            self.assertEqual(tool.firmware_sdk_version(path, 0), 'v5.5.6')

    def test_missing_or_truncated_descriptor_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'flash.bin'
            for data in (b'', bytes(0x10200)):
                path.write_bytes(data)
                with self.assertRaises(RuntimeError):
                    tool.firmware_sdk_version(path)


if __name__ == '__main__':
    unittest.main()
