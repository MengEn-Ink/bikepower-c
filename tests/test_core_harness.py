#!/usr/bin/env python3
"""核心采样与独占门禁负例；不访问硬件。"""
import tempfile
import unittest
from unittest.mock import patch
from core_harness import DeviceLease, aggregate, validate_samples
from core_suite import CASES, missing_dependencies

def frames(power=320):
    return {'CPS': [{'time': i, 'value': {'power_w': power, 'crank_revolutions': i * 2,
                                        'crank_event_time_1024': int(i * 60 * 1024 / 45)}} for i in range(10)],
            'HRS': [{'time': i, 'value': {'heart_rate_bpm': 150}} for i in range(10)]}

class CoreHarnessTests(unittest.TestCase):
    def test_valid_and_wraparound(self):
        f = frames()
        for v in f['CPS']:
            v['value']['crank_revolutions'] = (v['value']['crank_revolutions'] + 65530) & 65535
            v['value']['crank_event_time_1024'] = (v['value']['crank_event_time_1024'] + 65500) & 65535
        self.assertEqual(validate_samples(f, 10, 8, (318, 324))['errors'], [])

    def test_zero_power_hr_and_no_cadence_fail(self):
        f = frames(0)
        for v in f['HRS']: v['value']['heart_rate_bpm'] = 0
        for v in f['CPS']: v['value']['crank_revolutions'] = 0
        errors = validate_samples(f, 10, 8)['errors']
        self.assertTrue(any('功率' in e for e in errors))
        self.assertTrue(any('心率' in e for e in errors))
        self.assertTrue(any('踏频' in e for e in errors))

    def test_erg_incorrect_cannot_pass(self):
        self.assertTrue(any('ERG' in e for e in validate_samples(frames(100), 10, 8, (318, 324))['errors']))

    def test_bursts_and_notification_gaps_fail(self):
        f = frames()
        for values in f.values():
            for v in values: v['time'] *= .001
        self.assertTrue(validate_samples(f, 10, 8)['errors'])
        f = frames()
        for v in f['CPS'][5:]: v['time'] += 5
        self.assertTrue(any('停更' in e for e in validate_samples(f, 10, 8)['errors']))

    def test_failed_round_or_cleanup_not_hidden_by_success(self):
        self.assertEqual(aggregate([{'status': 'PASS'}] * 3, [], [], 3), 'PASS')
        self.assertEqual(aggregate([{'status': 'FAIL'}, {'status': 'PASS'}], [], [], 2), 'FAIL')
        self.assertEqual(aggregate([{'status': 'PASS'}], [], ['restore error'], 1), 'FAIL')
        self.assertEqual(aggregate([{'status': 'PASS'}], [], [], 3), 'FAIL')

    def test_dependency_preflight_before_execution(self):
        with patch('core_suite.importlib.util.find_spec', return_value=None):
            self.assertEqual(missing_dependencies({'transport': CASES['transport']}), ['bleak', 'serial'])
            self.assertEqual(missing_dependencies({'mobile': CASES['mobile']}), ['playwright'])
            self.assertEqual(missing_dependencies({'core-model': CASES['core-model']}), [])

    def test_target_lease_and_release(self):
        with tempfile.TemporaryDirectory() as tmp:
            with DeviceLease(tmp, 'target'):
                with self.assertRaises(RuntimeError):
                    with DeviceLease(tmp, 'TARGET'): pass
                with DeviceLease(tmp, 'another'): pass
            with DeviceLease(tmp, 'target'): pass

if __name__ == '__main__': unittest.main()
