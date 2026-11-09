#!/usr/bin/env python3
"""Offline regression checks for the staged running BLE harness helpers."""

import asyncio
import struct
import sys
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests"))

from ble_selftest import (  # noqa: E402
    CP_RESULT_OPERATION_FAILED,
    CP_RESULT_SUCCESS,
    FTMS_CP_UUID,
    values_are_frozen,
    values_strictly_grow,
    write_ftms_cp_and_wait,
)
import ble_selftest as harness


class FakeClient:
    def __init__(self, responses, result):
        self.responses = responses
        self.result = result
        self.writes = []

    async def write_gatt_char(self, uuid, payload, response):
        self.writes.append((uuid, bytes(payload), response))
        self.responses.append(bytes([0x80, payload[0], self.result]))


async def run_response_case(result):
    responses = []
    client = FakeClient(responses, result)
    actual = await write_ftms_cp_and_wait(
        client,
        responses,
        0x07,
        payload=b"\x34\x12",
        timeout=0.2,
    )
    assert actual == result
    assert client.writes == [(FTMS_CP_UUID, b"\x07\x34\x12", True)]


class RunningClient:
    """按当前 FTMS 契约模拟控制与真实字节通知。"""

    def __init__(self, cadence=90):
        self.cadence = cadence
        self.owner = False
        self.moving = True
        self.speed = 1000
        self.distance = 0
        self.elapsed = 0
        self.handlers = {}
        self.writes = []
        self.values = {
            harness.FTMS_FEATURE_UUID: struct.pack("<II", 0x1404, 1),
            harness.FTMS_SPEED_RANGE_UUID: struct.pack("<HHH", 300, 2500, 1),
            harness.RSC_FEATURE_UUID: b"\x07\x00",
            harness.SENSOR_LOCATION_UUID: b"\x01",
            **{uuid: expected.encode() for uuid, (_, expected) in harness.DIS_EXPECT.items()},
        }
        self.properties = {
            harness.RSC_UUID: ["notify"],
            harness.HR_UUID: ["notify"],
            harness.FTMS_TREADMILL_UUID: ["notify"],
            harness.FTMS_CP_UUID: ["write", "indicate"],
            **{uuid: ["read"] for uuid in self.values},
        }
        self.services = SimpleNamespace(
            get_characteristic=lambda uuid: SimpleNamespace(properties=self.properties[uuid])
        )

    async def read_gatt_char(self, uuid):
        return self.values[uuid]

    async def start_notify(self, uuid, callback):
        self.handlers[uuid] = callback

    async def write_gatt_char(self, uuid, payload, response):
        assert uuid == harness.FTMS_CP_UUID and response
        op = payload[0]
        self.writes.append(op)
        result = harness.CP_RESULT_SUCCESS
        if op == harness.OP_REQUEST_CONTROL:
            self.owner = True
        elif not self.owner:
            result = harness.CP_RESULT_OPERATION_FAILED
        elif op == harness.OP_RESET:
            self.distance = self.elapsed = 0
            self.speed = 1000
            self.owner = False
        elif op == harness.OP_START_RESUME:
            self.moving = True
        elif op == harness.OP_STOP_PAUSE:
            self.moving = False
        elif op == harness.OP_SET_TARGET_SPEED:
            self.speed = struct.unpack_from("<H", payload, 1)[0]
        else:
            result = harness.CP_RESULT_NOT_SUPPORTED
        self.handlers[uuid](None, bytes([0x80, op, result]))

    async def advance(self, seconds):
        for _ in range(max(1, int(seconds))):
            if self.moving:
                self.distance += 10
                self.elapsed += 1
            speed = self.speed if self.moving else 0
            rsc = struct.pack(
                "<BH BHI", 7 if self.moving else 3,
                round(speed / 100 / 3.6 * 256),
                self.cadence if self.moving else 0,
                100 if self.moving else 0, self.distance * 10,
            )
            treadmill = (
                struct.pack("<HH", 0x504, speed)
                + self.distance.to_bytes(3, "little") + b"\x78"
                + struct.pack("<H", self.elapsed)
            )
            self.handlers[harness.RSC_UUID](None, rsc)
            self.handlers[harness.FTMS_TREADMILL_UUID](None, treadmill)
            self.handlers[harness.HR_UUID](None, b"\x00\x78")


def test_running_profile_reset_and_cadence():
    for cadence, expected in ((90, 0), (180, 1)):
        client = RunningClient(cadence)
        with patch.object(harness, "RUN_CONTROL", True), patch.object(harness, "PROFILE", "running"):
            with patch.object(asyncio, "sleep", client.advance), redirect_stdout(StringIO()):
                result = asyncio.run(harness.run_running_profile(client, set(client.properties)))
        assert result == expected, f"单脚步频={cadence}，四阶段判定={result}"
        reset_at = client.writes.index(harness.OP_RESET)
        assert client.writes[reset_at + 1:] == [
            harness.OP_REQUEST_CONTROL, harness.OP_START_RESUME
        ], "Reset 释放控制后必须重新申请控制"


def main():
    asyncio.run(run_response_case(CP_RESULT_SUCCESS))
    asyncio.run(run_response_case(CP_RESULT_OPERATION_FAILED))

    growing = [{"distance": 10}, {"distance": 11}, {"distance": 14}]
    flat = [{"distance": 10}, {"distance": 10}, {"distance": 10}]
    regressed = [{"distance": 1}, {"distance": 100}, {"distance": 2}]
    assert values_strictly_grow(growing, "distance")
    assert not values_strictly_grow(flat, "distance")
    assert not values_strictly_grow(regressed, "distance")
    assert values_are_frozen(flat, "distance")
    assert not values_are_frozen(growing, "distance")
    assert not values_strictly_grow(growing[:1], "distance")
    assert not values_are_frozen(flat[:1], "distance")
    test_running_profile_reset_and_cadence()

    source = (ROOT / "tests" / "ble_selftest.py").read_text(encoding="utf-8")
    for token in (
        "moving_rsc",
        "paused_rsc",
        "reset_rsc",
        "resumed_rsc",
        "write_ftms_cp_and_wait",
        "values_are_frozen(paused_treadmill, \"elapsed_time_s\")",
        "item[\"total_distance_dm\"] == 0",
        "values_strictly_grow(resumed_treadmill, \"elapsed_time_s\")",
    ):
        assert token in source, f"running staged harness missing token: {token}"

    print("[ble-running-harness] PASS: response wait and moving/pause/reset/resume contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
