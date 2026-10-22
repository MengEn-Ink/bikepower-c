#!/usr/bin/env python3
"""Offline fake harness tests for the two-cycle BLE reconnect gate."""
import asyncio
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests"))

from stage3_conn_test import run_reconnect_harness  # noqa: E402


class FakeDevice:
    def __init__(self, address, scan_index):
        self.address = address
        self.scan_index = scan_index
        self.name = "BikePower"


class FakeScanner:
    scans = 0
    devices = []

    @classmethod
    async def find_device_by_name(cls, name, timeout):
        del name, timeout
        cls.scans += 1
        device = FakeDevice("fake-device", cls.scans)
        cls.devices.append(device)
        return device

    @classmethod
    async def discover(cls, timeout):
        del timeout
        raise AssertionError("精确扫描已成功，不应进入 fallback discover")


class FakeClient:
    instances = []

    def __init__(self, device):
        self.device = device
        self.is_connected = False
        self.connect_calls = 0
        self.disconnect_calls = 0
        self.__class__.instances.append(self)

    async def connect(self):
        self.connect_calls += 1
        self.is_connected = True

    async def disconnect(self):
        self.disconnect_calls += 1
        self.is_connected = False


async def no_sleep(_seconds):
    return None


def main():
    FakeScanner.scans = 0
    FakeScanner.devices = []
    FakeClient.instances = []
    result = asyncio.run(
        run_reconnect_harness(
            scanner=FakeScanner,
            client_factory=FakeClient,
            hold_seconds=0,
            readvertise_wait=0,
            sleep_fn=no_sleep,
        )
    )
    assert FakeScanner.scans == 2, "必须执行两次独立扫描"
    assert len(FakeScanner.devices) == 2
    assert FakeScanner.devices[0] is not FakeScanner.devices[1], "两轮必须返回独立扫描对象"
    assert result["addresses"] == ["fake-device", "fake-device"], "两轮必须命中同一设备"
    assert len(FakeClient.instances) == 2, "必须创建两个独立连接客户端"
    for client in FakeClient.instances:
        assert client.connect_calls == 1
        assert client.disconnect_calls == 1
        assert client.is_connected is False
    assert all(item == {"connected": True, "disconnected": True} for item in result["cycles"])
    print("[ble-reconnect-harness] PASS: two independent scan/connect/disconnect cycles")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
