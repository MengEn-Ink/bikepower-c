#!/usr/bin/env python3
"""Offline regression checks for the staged running BLE harness helpers."""

import asyncio
import sys
from pathlib import Path


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
