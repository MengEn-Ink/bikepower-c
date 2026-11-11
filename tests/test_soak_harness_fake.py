#!/usr/bin/env python3
"""耐久门禁的确定性设备桩：真实字节、逐路帧、功率与最低堆。"""
import asyncio
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import ble_release_control_soak as soak


class FakeSession:
    def __init__(self, options):
        self.options = options
        self.connects = self.commits = 0
        self.connected = False
        self.cfg = {"core": bytes([0] * 14 + [options.get("mask", 3)]),
                    "bridge": bytes([1, options.get("bridge", 1)] + [0] * 14)}
        self.original = dict(self.cfg)
        self.handlers = {}
        self.power = 100
        all_chars = list(soak.REQUIRED.values()) + soak.TELE_UUIDS
        if options.get("missing_cp"):
            all_chars.remove(soak.FTMS_CP_UUID)
        self.client = SimpleNamespace(
            services=[SimpleNamespace(characteristics=[SimpleNamespace(uuid=u) for u in all_chars])],
            start_notify=self.start_notify, write_gatt_char=self.write,
        )

    async def connect(self):
        self.connects += 1
        if self.options.get("boom") or (
            self.options.get("fail_round") and self.connects == 3
        ):
            raise RuntimeError("连接失败")
        self.connected = True
        self.handlers.clear()

    async def disconnect(self):
        self.connected = False
        self.handlers.clear()

    async def read_snapshot(self):
        return {"protocol": 1, "schema": 14, "revision": 1, "group_mask": 255,
                "capabilities": 15, "max_len": 48}, dict(self.cfg)

    async def commit_all(self, groups, revision):
        assert revision == 1
        self.commits += 1
        changed = self.cfg != groups
        if not self.options.get("ignore_bridge"):
            self.cfg.update(groups)
        else:
            self.cfg["core"] = groups["core"]
        return {"result": 0, "revision": 1, "restart_flags": int(changed)}

    async def start_notify(self, uuid, callback):
        self.handlers[uuid] = callback

    async def write(self, uuid, data, response):
        assert uuid == soak.FTMS_CP_UUID and response
        code = 1
        if data[0] == 5:
            requested = int.from_bytes(data[1:3], "little", signed=True)
            if requested < 0:
                code = 3
            else:
                self.power = self.options.get("power", requested)
        self.handlers[uuid](None, bytes([0x80, data[0], code]))

    async def advance(self, seconds):
        if not self.connected or self.options.get("zero_frame"):
            return
        count = max(1, int(seconds * 5))
        if self.options.get("sparse") and seconds >= 1:
            count = 2
        for seq in range(count):
            minimum = self.options.get("minimum_heap", 100000)
            packets = {
                soak.CPS_UUID: struct.pack("<HhHH", 0x20, self.power, seq, seq * 1024),
                soak.HR_UUID: b"\x00\x78",
                soak.CSC_UUID: struct.pack("<BHH", 2, seq, seq * 1024),
                soak.FTMS_BIKE_UUID: struct.pack("<HHHh", 0x44, 0, 180, self.power),
                soak.TELE_UUIDS[0]: struct.pack("<BBHIIII", 1, 1, seq, 150000, minimum, 100000, 200000),
                soak.TELE_UUIDS[1]: struct.pack("<BBHIHHHIBB", 1, 1, seq, 10, 100, 160, 50, 2048, 0, 0),
                soak.TELE_UUIDS[2]: struct.pack("<BBHbBHHHH", 1, 1, seq, 0, 1, 23, 24, 0, 100) + bytes(6),
                soak.TELE_UUIDS[3]: struct.pack("<BBHIIII", 1, 1, seq, 10, 0, 0, 0),
            }
            if self.options.get("parse_err"):
                packets[soak.HR_UUID] = b"\x00"
            if self.options.get("missing_tele_frame"):
                packets.pop(soak.TELE_UUIDS[2])
            for uuid, data in packets.items():
                if uuid in self.handlers:
                    self.handlers[uuid](None, data)


class SoakTests(unittest.TestCase):
    def run_scenario(self, **options):
        session = FakeSession(options)
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "report.json"
            argv = ["soak", "--address", "target", "--port", "test-port",
                    "--duration", "1", "--rounds", "2", "--min-frames", "1", "--out", str(out)]
            with patch.object(soak, "DeviceSession", return_value=session), \
                    patch.object(soak, "reset_device"), \
                    patch.object(asyncio, "sleep", session.advance), \
                    patch.object(sys, "argv", argv), redirect_stdout(StringIO()):
                rc = asyncio.run(soak.main())
            report = json.loads(out.read_text())
        return rc, session, report

    def test_power_and_heap_boundaries(self):
        for power, minimum, expected in (
            (315, 81921, 0), (325, 100000, 0),
            (314, 100000, 1), (326, 100000, 1),
            (320, 81920, 1), (320, 81919, 1),
        ):
            with self.subTest(power=power, minimum=minimum):
                rc, session, report = self.run_scenario(power=power, minimum_heap=minimum)
                self.assertEqual(rc, expected, report)
                self.assertEqual(session.cfg, session.original)
                self.assertGreaterEqual(session.connects, 5)
                for item in report["rounds"]:
                    self.assertEqual(item["power_tail_w"], [power] * 3)
                    self.assertEqual(item["minimum_heap_bytes"], minimum)

    def test_missing_frames_and_bad_payloads_fail(self):
        for option in ("sparse", "zero_frame", "parse_err", "missing_cp", "missing_tele_frame"):
            with self.subTest(option=option):
                rc, session, report = self.run_scenario(**{option: True})
                self.assertEqual(rc, 1, report)
                self.assertEqual(session.cfg, session.original)

    def test_same_config_is_valid_and_failure_restores(self):
        rc, _, report = self.run_scenario(mask=7, bridge=0)
        self.assertEqual(rc, 0, report)
        rc, session, report = self.run_scenario(fail_round=True)
        self.assertEqual(rc, 1, report)
        self.assertEqual(session.cfg, session.original)
        self.assertGreaterEqual(session.commits, 2)
        rc, session, report = self.run_scenario(boom=True)
        self.assertEqual(rc, 1, report)
        self.assertEqual(session.commits, 0)

    def test_unapplied_precondition_is_separate(self):
        rc, session, report = self.run_scenario(ignore_bridge=True)
        self.assertEqual(rc, 2, report)
        self.assertEqual(report["rounds"], [])
        self.assertEqual(session.handlers, {})

    def test_round_requires_actual_erg_tail(self):
        for power, expected_errors in ((100, True), (320, False)):
            session = FakeSession({"power": power})
            args = SimpleNamespace(address="target", duration=1, min_frames=1)
            run = soak.SoakRun(args)

            async def exercise():
                await session.connect()
                context, ok = await soak.subscribe_all(session, run)
                self.assertTrue(ok)
                await soak.run_round(session, context, run, 1)

            with patch.object(asyncio, "sleep", session.advance):
                asyncio.run(exercise())
            self.assertEqual(bool(run.result["main_failures"]), expected_errors,
                             run.result["main_failures"])


if __name__ == "__main__":
    unittest.main()
