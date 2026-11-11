#!/usr/bin/env python3
"""核心真机编排负例，不触碰硬件；控制略过不等于完整验收。"""
import asyncio
import copy
import json
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch
import core_device_e2e as e2e
from test_ble_config_e2e import fixture_groups

class Session:
    def __init__(self, *a, **kw):
        self.original = fixture_groups()
        self.cfg = copy.deepcopy(self.original)
        self.client = None
        self.handlers = {}
        self.commits = []
        self.failure = None
        self.fail_restore = False
        self.control_writes = []

    async def connect(self):
        mask = self.cfg['core'][14]
        chars = [e2e.CPS, e2e.HRS, e2e.CP]
        if mask == 7: chars.append('00002a5b-0000-1000-8000-00805f9b34fb')
        self.handlers = {}
        self.client = types.SimpleNamespace(is_connected=True,
            services=[types.SimpleNamespace(characteristics=[types.SimpleNamespace(uuid=u) for u in chars])],
            start_notify=self.notify, write_gatt_char=self.write, read_gatt_char=self.read)

    async def disconnect(self): self.client = None
    async def notify(self, u, cb): self.handlers[u] = cb
    async def read(self, u): return b'not-used'
    async def write(self, u, data, response):
        self.control_writes.append(bytes(data))
        if u == e2e.CP:
            code = 3 if data == b'\x05\xff\xff' else 1
            self.handlers[u](None, bytes([128, data[0], code]))
    async def read_snapshot(self): return {'revision': 1}, copy.deepcopy(self.cfg)
    async def commit_all(self, target, revision):
        self.commits.append(copy.deepcopy(target))
        if target == self.original and self.fail_restore: raise RuntimeError('restore fault')
        self.cfg = copy.deepcopy(target)
        return {'result': 0, 'restart_flags': 1}

class DeviceHarnessTests(unittest.TestCase):
    def exercise(self, data_only=True, failure=None, restore_failure=False, expected_git=None):
        session = Session()
        session.failure = failure
        session.fail_restore = restore_failure
        async def sleep(seconds):
            if seconds == 10:
                if session.failure:
                    error = session.failure
                    session.failure = None
                    raise error
                for i in range(10):
                    session.handlers[e2e.CPS](None, b'\x20\x00\x40\x01\x01\x00\x01\x00')
                    session.handlers[e2e.HRS](None, b'\x00\x96')
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)/'report.json'
            args = types.SimpleNamespace(name='test',timeout=1,address='test',board='generic-c3',
                original=None,firmware=None,out=str(out),rounds=2,seconds=10,settle=1,port='test',
                data_only=data_only,expected_git=expected_git)
            with patch.object(e2e,'DeviceSession',return_value=session), \
                 patch.object(e2e,'missing_device_dependencies',return_value=[]), \
                 patch.object(e2e,'control_preflight',new=async_noop), \
                 patch.object(e2e,'verify_firmware',new=async_noop), \
                 patch.object(e2e,'decode_meta',return_value={'version':'4.4.0','board':'generic-c3','git':'current'}), \
                 patch.object(e2e,'reset_device'), \
                 patch.object(e2e.asyncio,'sleep',new=sleep), \
                 patch.object(e2e,'validate_samples',return_value={'errors': []}):
                rc = asyncio.run(e2e.exercise(args))
            report = json.loads(out.read_text(encoding='utf-8'))
        return rc, report, session

    def test_two_round_data_only_not_full_acceptance(self):
        rc, report, session = self.exercise()
        self.assertEqual(rc, 0)
        self.assertEqual(len(report['rounds']), 2)
        self.assertEqual(report['control_status'], 'NOT_RUN')
        self.assertFalse(report['full_acceptance'])
        self.assertEqual(session.control_writes, [])
        self.assertEqual(session.cfg, session.original)
        self.assertTrue(report['released'])

    def test_wrong_build_identity_never_writes(self):
        rc, report, session = self.exercise(expected_git='other')
        self.assertEqual(rc, 1)
        self.assertEqual(session.commits, [])
        self.assertFalse(report['full_acceptance'])
        self.assertIn('Git身份不匹配', report['main_errors'][0])

    def test_control_exact_packets_and_success(self):
        rc, report, session = self.exercise(data_only=False)
        self.assertEqual(rc, 0)
        self.assertTrue(report['full_acceptance'])
        self.assertEqual(len(session.control_writes), 10)
        self.assertEqual(report['rounds'][0]['control'], ['800001','800701','800503','800501','800801'])

    def test_cancel_after_change_restores_and_fails(self):
        rc, report, session = self.exercise(failure=asyncio.CancelledError())
        self.assertEqual(rc, 1)
        self.assertTrue(report['restored'])
        self.assertTrue(report['released'])
        self.assertEqual(session.cfg, session.original)
        self.assertIn('CancelledError', report['main_errors'][0])

    def test_preflight_failure_never_connects_or_writes(self):
        async def unpaired(address):
            raise RuntimeError('Windows目标未配对')
        for missing in ([], ['esptool']):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as tmp:
                session = Session()
                args = types.SimpleNamespace(name='test', timeout=1, address='test', board='generic-c3',
                    original=None, firmware=None, out=str(Path(tmp)/'report.json'), rounds=1,
                    seconds=10, settle=1, port='test', data_only=False, expected_git=None)
                with patch.object(e2e, 'DeviceSession', return_value=session), \
                     patch.object(e2e, 'missing_device_dependencies', return_value=missing), \
                     patch.object(e2e, 'control_preflight', new=unpaired), \
                     patch.object(session, 'connect', wraps=session.connect) as connect, \
                     patch.object(e2e, 'reset_device') as reset:
                    rc = asyncio.run(e2e.exercise(args))
                report = json.loads(Path(args.out).read_text(encoding='utf-8'))
                self.assertEqual(rc, 1)
                connect.assert_not_called()
                reset.assert_not_called()
                self.assertEqual(session.commits, [])
                self.assertEqual(report['restore_errors'], [])
                self.assertEqual(report['restore_status'], 'NOT_REQUIRED')
                self.assertEqual(report['control_status'], 'NOT_RUN')
                self.assertTrue(report['precondition_errors'])
                self.assertFalse(report['configuration_written'])
                self.assertFalse(report['full_acceptance'])

    def test_report_and_snapshot_cannot_be_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)/'report.json'
            for path in e2e.artifact_paths(out):
                path.write_text('existing', encoding='utf-8')
                with self.assertRaises(FileExistsError):
                    e2e.check_output_paths(out)
                self.assertEqual(path.read_text(encoding='utf-8'), 'existing')
                path.unlink()
            e2e.check_output_paths(out)

    def test_device_dependency_missing_nested_namespace_is_reported(self):
        with patch.object(e2e.importlib.util, 'find_spec', side_effect=ModuleNotFoundError):
            missing = e2e.missing_device_dependencies()
        self.assertIn('esptool', missing)
        self.assertIn('bleak', missing)

    def test_primary_and_restore_errors_kept_separate(self):
        rc, report, session = self.exercise(failure=RuntimeError('main fault'), restore_failure=True)
        self.assertEqual(rc, 1)
        self.assertTrue(report['main_errors'])
        self.assertTrue(report['restore_errors'])
        self.assertFalse(report['restored'])
        self.assertTrue(report['released'])

async def async_noop(*a, **kw): pass
if __name__ == '__main__': unittest.main()
