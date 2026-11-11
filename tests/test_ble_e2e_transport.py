#!/usr/bin/env python3
"""BLE真机测试传输层离线回归；不连接设备、不修改配置。"""
import asyncio
import os
import struct
import sys
import types
import unittest
from unittest.mock import AsyncMock, Mock, patch

import ble_config_e2e as config
import stage3_conn_test as reconnect
from test_ble_config_e2e import fixture_groups


class TransportTests(unittest.TestCase):
    def test_profile_child_uses_identified_address_and_restores(self):
        import device_profile_ble_harness as profile
        original = fixture_groups()
        current = dict(original)
        session = types.SimpleNamespace(
            address="target", connect=AsyncMock(), disconnect=AsyncMock(),
            client=types.SimpleNamespace(read_gatt_char=AsyncMock(
                return_value=config.expected_firmware_name("generic-c3").encode())),
        )

        async def read_snapshot():
            return {"revision": 1}, dict(current)

        async def commit(groups, revision):
            self.assertEqual(revision, 1)
            current.update(groups)
            return {"result": 0, "restart_flags": 1}

        session.read_snapshot = read_snapshot
        session.commit_all = commit
        args = types.SimpleNamespace(device_name="BikePower", scan_timeout=1, board="generic-c3",
                                     profile="running", settle_seconds=0, duration=3, target_power=250)
        with patch.object(profile, "DeviceSession", return_value=session) as factory, \
                patch.object(profile.subprocess, "run", return_value=types.SimpleNamespace(returncode=0)) as run, \
                patch.object(asyncio, "sleep", AsyncMock()), \
                patch.dict(os.environ, {"BIKEPOWER_BLE_ADDRESS": "target"}):
            asyncio.run(profile.run_harness(args))
        self.assertEqual(factory.call_args.kwargs["address"], "target")
        self.assertEqual(run.call_args.kwargs["env"]["BIKEPOWER_BLE_ADDRESS"], "target")
        self.assertEqual(current, original)

    def test_selftest_explicit_miss_never_uses_name(self):
        import ble_selftest as selftest
        scanner = types.SimpleNamespace(find_device_by_filter=AsyncMock(return_value=None),
                                        find_device_by_name=AsyncMock(), discover=AsyncMock())
        with patch.object(selftest, "BleakScanner", scanner), \
                patch.dict(os.environ, {"BIKEPOWER_BLE_ADDRESS": "target"}):
            self.assertEqual(asyncio.run(selftest.main()), 1)
        scanner.find_device_by_name.assert_not_awaited()
        scanner.discover.assert_not_awaited()

    def test_explicit_address_never_falls_back(self):
        scanner = types.SimpleNamespace(find_device_by_filter=AsyncMock(return_value=None))

        async def exercise():
            session = config.DeviceSession("BikePower", 1, "target")
            await session._find_device()

        with patch.dict(sys.modules, {"bleak": types.SimpleNamespace(BleakScanner=scanner)}):
            with self.assertRaises(config.ConfigE2EError):
                asyncio.run(exercise())
        self.assertEqual(scanner.find_device_by_filter.await_count, 1)
        predicate = scanner.find_device_by_filter.call_args.args[0]
        self.assertTrue(predicate(types.SimpleNamespace(address="target"), None))
        self.assertFalse(predicate(types.SimpleNamespace(address="other"), None))

    def test_config_client_disables_cached_services_and_releases(self):
        device = types.SimpleNamespace(address="target")
        client = types.SimpleNamespace(is_connected=False,
                                       services=[types.SimpleNamespace(uuid=config.CONFIG_SERVICE_UUID)],
                                       connect=AsyncMock(), disconnect=AsyncMock())
        factory = Mock(return_value=client)
        async def exercise():
            session = config.DeviceSession("BikePower", 1, "target")
            session._find_device = AsyncMock(return_value=device)
            await session.connect()
            client.is_connected = True
            await session.disconnect()
            self.assertIsNone(session.client)
        with patch.dict(sys.modules, {"bleak": types.SimpleNamespace(BleakClient=factory)}):
            asyncio.run(exercise())
        factory.assert_called_once_with(device, timeout=20.0, winrt={"use_cached_services": False})
        client.disconnect.assert_awaited_once()

    def test_meta_fence_rejects_mixed_snapshot(self):
        groups = fixture_groups()
        meta = struct.pack("<BBBBIHH", 1, 14, 0xFF, 0x0F, 1, 48, 0)
        changed = struct.pack("<BBBBIHH", 1, 14, 0xFF, 0x0F, 2, 48, 0)
        for final_meta, valid in [(meta, True), (changed, False)]:
            async def exercise():
                session = config.DeviceSession("BikePower", 1)
                session.client = types.SimpleNamespace(read_gatt_char=AsyncMock(
                    side_effect=[meta, *groups.values(), final_meta]))
                if valid:
                    actual_meta, actual_groups = await session.read_snapshot()
                    self.assertEqual(actual_meta["revision"], 1)
                    self.assertEqual(actual_groups, groups)
                else:
                    with self.assertRaisesRegex(config.ConfigE2EError, "Meta改变"):
                        await session.read_snapshot()
                self.assertEqual(session.client.read_gatt_char.await_count, 10)
            asyncio.run(exercise())

    def test_cancelled_gatt_runs_original_restore_and_disconnect(self):
        args = types.SimpleNamespace(device_name="BikePower", scan_timeout=1, board="",
                                     port="COM-test", settle_seconds=0)
        session = types.SimpleNamespace(connect=AsyncMock(), disconnect=AsyncMock(),
                                        read_snapshot=AsyncMock(return_value=({"revision": 1, "schema": 14}, fixture_groups())),
                                        control=AsyncMock(side_effect=asyncio.CancelledError()))
        restore = AsyncMock()
        with patch.object(config, "DeviceSession", return_value=session), \
                patch.object(config, "restore_original", restore):
            with self.assertRaisesRegex(config.ConfigE2EError, "CancelledError"):
                asyncio.run(config.run_e2e(args))
        restore.assert_awaited_once_with(session, fixture_groups(), "COM-test", 0)
        session.disconnect.assert_awaited_once()

    def test_reconnect_drop_during_hold_is_not_pass(self):
        client = types.SimpleNamespace(is_connected=True, connect=AsyncMock(), disconnect=AsyncMock())
        async def dropped(_seconds):
            client.is_connected = False
        with self.assertRaisesRegex(RuntimeError, "意外断开"):
            asyncio.run(reconnect.connect_cycle(None, client_factory=lambda _: client,
                                               sleep_fn=dropped))

    def test_rom_reset_uses_same_interpreter_without_flash(self):
        result = types.SimpleNamespace(returncode=0, stdout="run", stderr="")
        with patch.object(config.subprocess, "run", return_value=result) as run:
            config.reset_device("COM-test", "rom")
        self.assertEqual(run.call_args.args[0],
                         [sys.executable, "-m", "esptool", "--chip", "esp32c3",
                          "--port", "COM-test", "run"])
        self.assertEqual(run.call_args.kwargs["timeout"], 45)
        result.returncode = 1
        with patch.object(config.subprocess, "run", return_value=result):
            with self.assertRaises(config.ConfigE2EError):
                config.reset_device("COM-test", "rom")

    def test_reconnect_exact_target_miss_does_not_use_name(self):
        scanner = types.SimpleNamespace(find_device_by_address=AsyncMock(return_value=None),
                                        find_device_by_name=AsyncMock(), discover=AsyncMock())
        with self.assertRaisesRegex(RuntimeError, "未发现"):
            asyncio.run(reconnect.run_reconnect_harness(scanner=scanner, address="target"))
        scanner.find_device_by_address.assert_awaited_once_with("target", timeout=10.0)
        scanner.find_device_by_name.assert_not_awaited()
        scanner.discover.assert_not_awaited()

    def test_explicit_reconnect_two_new_scans_and_clients(self):
        from test_ble_reconnect_harness import FakeClient, no_sleep
        devices = [types.SimpleNamespace(address="target"), types.SimpleNamespace(address="target")]
        scanner = types.SimpleNamespace(find_device_by_address=AsyncMock(side_effect=devices))
        FakeClient.instances = []
        result = asyncio.run(reconnect.run_reconnect_harness(
            scanner=scanner, client_factory=FakeClient, address="target", hold_seconds=0,
            readvertise_wait=0, sleep_fn=no_sleep))
        self.assertEqual(scanner.find_device_by_address.await_count, 2)
        self.assertEqual(result["addresses"], ["target", "target"])
        self.assertEqual(len(FakeClient.instances), 2)
        self.assertTrue(all(not client.is_connected and client.disconnect_calls == 1
                            for client in FakeClient.instances))


if __name__ == "__main__":
    unittest.main()
