#!/usr/bin/env python3
"""本地虚拟控制器门禁：密钥不匹配、缺失、重复回复与未知命令必须失败。"""
import sys
import unittest
from pathlib import Path
from bumble import hci
from virtual_ble_controller import CheckedController

sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'scripts/tools'))
from virtual_board import target_platform, LOCK


class ControllerTests(unittest.IsolatedAsyncioTestCase):
    def make_pair(self):
        central=CheckedController('central')
        peripheral=CheckedController('peripheral')
        central.events=[]; peripheral.events=[]
        central.send_hci_packet=central.events.append
        peripheral.send_hci_packet=peripheral.events.append
        peripheral.pending_encryption[2]=(central,1,bytes(range(16)))
        return central,peripheral

    async def test_matching_key_only(self):
        central,peripheral=self.make_pair()
        self.assertEqual(peripheral.complete_encryption(2,bytes(range(16))),0)
        self.assertEqual(central.encryption_checks,1)
        self.assertEqual(peripheral.encryption_checks,1)
        self.assertEqual(central.events[-1].status,0)
        self.assertEqual(central.events[-1].encryption_enabled,1)
        self.assertEqual(peripheral.complete_encryption(2,bytes(range(16))),hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR)

    async def test_mismatch_not_success(self):
        central,peripheral=self.make_pair()
        peripheral.complete_encryption(2,b'\xff'*16)
        self.assertEqual(central.encryption_checks,0)
        self.assertNotEqual(central.events[-1].status,0)
        self.assertEqual(central.events[-1].encryption_enabled,0)

    async def test_missing_key_not_success(self):
        central,peripheral=self.make_pair()
        peripheral.complete_encryption(2,None)
        self.assertEqual(peripheral.encryption_checks,0)
        self.assertNotEqual(central.events[-1].status,0)

    async def test_unknown_handle_no_event(self):
        central,peripheral=self.make_pair()
        self.assertEqual(peripheral.complete_encryption(99,b'\0'*16),hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR)
        self.assertEqual(central.events,[])

    async def test_unknown_command_not_acked_ok(self):
        controller=CheckedController('controller')
        command=hci.HCI_Read_RSSI_Command(handle=1)
        result=controller.on_hci_command(command)
        self.assertEqual(result,bytes([hci.HCI_UNKNOWN_HCI_COMMAND_ERROR]))
        self.assertIn(command.name,controller.unsupported)

    async def test_invalid_key_length(self):
        central, peripheral = self.make_pair()
        peripheral.complete_encryption(2, b'\x00')
        self.assertNotEqual(central.events[-1].status, 0)
        self.assertFalse(peripheral.encrypted_handles)

    async def test_reset_drops_pending_encryption(self):
        central, peripheral = self.make_pair()
        peripheral.on_hci_reset_command(hci.HCI_Reset_Command())
        self.assertEqual(peripheral.complete_encryption(2, bytes(range(16))), hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR)
        self.assertEqual(central.events, [])

    async def test_key_size_requires_encryption(self):
        from unittest.mock import patch
        _, peripheral = self.make_pair()
        with patch.object(peripheral, 'find_central_connection_by_handle', return_value=object()):
            result = peripheral.on_hci_read_encryption_key_size_command(hci.HCI_Read_Encryption_Key_Size_Command(connection_handle=2))
        self.assertNotEqual(result[0], 0)
        self.assertEqual(result[-1], 0)

    async def test_archive_hashes_are_pinned(self):
        self.assertEqual(LOCK['esp_emulator'],'0.51.0')
        self.assertTrue(all(len(v)==64 for v in LOCK['archives'].values()))


if __name__=='__main__':
    unittest.main()
