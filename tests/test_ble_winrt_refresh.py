#!/usr/bin/env python3
"""缓存刷新需识别当前双板固件并释放读取失败的连接。"""
import asyncio
import struct
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock

from ble_config_e2e import expected_firmware_name
from ble_winrt_refresh import StaleCacheError, connect_validated


META = struct.pack("<BBBBIHH", 1, 14, 255, 15, 1, 48, 0)


class RefreshTests(unittest.TestCase):
    def session(self, reads):
        return SimpleNamespace(
            connect_uncached=AsyncMock(),
            disconnect=AsyncMock(),
            client=SimpleNamespace(services=[], read_gatt_char=AsyncMock(side_effect=reads)),
        )

    def test_current_boards_and_exact_version(self):
        for board in ("generic-c3", "m5stamp-c3"):
            session = self.session([META, expected_firmware_name(board).encode()])
            diagnostics = asyncio.run(connect_validated(session))
            self.assertEqual(diagnostics, [])
            session.connect_uncached.assert_awaited_once_with(False)
            session.disconnect.assert_not_awaited()

    def test_read_failure_refreshes_and_releases(self):
        session = self.session([
            RuntimeError("读取失败"), META, expected_firmware_name("generic-c3").encode()
        ])
        diagnostics = asyncio.run(connect_validated(session))
        self.assertTrue(diagnostics)
        self.assertEqual(session.connect_uncached.await_args_list[1].args, (True,))
        session.disconnect.assert_awaited_once()

    def test_stale_or_failed_attempts_are_bounded_and_released(self):
        for reads in (
            [META, b"generic-c3-v4.3.3", META, b"generic-c3-v4.4.0-invalid"],
            [RuntimeError("失败"), RuntimeError("失败")],
        ):
            session = self.session(reads)
            with self.assertRaises(StaleCacheError):
                asyncio.run(connect_validated(session))
            self.assertEqual(session.connect_uncached.await_count, 2)
            self.assertEqual(session.disconnect.await_count, 2)


if __name__ == "__main__":
    unittest.main()
