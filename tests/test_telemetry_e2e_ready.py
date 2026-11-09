#!/usr/bin/env python3
"""遥测测试需有界等待主机缓存就绪，不放宽正式采样断言。"""
import asyncio
import struct
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock
import ble_telemetry_e2e as harness

def link(mtu, connected=True):
    data = bytearray(20)
    data[:4] = bytes([1, 3 if connected else 0, 1, 0])
    struct.pack_into("<bBHHHH", data, 4, -40, connected, mtu, 24, 0, 400)
    return data

class LinkReadyTests(unittest.TestCase):
    def test_initial_cache_then_ready(self):
        client = SimpleNamespace(read_gatt_char=AsyncMock(side_effect=[link(0), link(64)]))
        result = asyncio.run(harness.wait_link_ready(client, timeout=1))
        self.assertEqual(result["mtu"], 64)
        self.assertEqual(client.read_gatt_char.await_count, 2)

    def test_never_ready_fails_bounded(self):
        for data in (link(0), link(64, False), link(22)):
            client = SimpleNamespace(read_gatt_char=AsyncMock(return_value=data))
            with self.assertRaises(TimeoutError):
                asyncio.run(harness.wait_link_ready(client, timeout=0))
            self.assertEqual(client.read_gatt_char.await_count, 1)

    def test_already_ready_returns_without_wait(self):
        client = SimpleNamespace(read_gatt_char=AsyncMock(return_value=link(23)))
        self.assertEqual(asyncio.run(harness.wait_link_ready(client, timeout=0))["mtu"], 23)
        self.assertEqual(client.read_gatt_char.await_count, 1)

if __name__ == "__main__":
    unittest.main()
