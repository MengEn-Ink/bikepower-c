#!/usr/bin/env python3
"""BLE 自测辅助函数回归测试。"""
import sys
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests"))

import ble_selftest  # noqa: E402


def main():
    cycling_location = SimpleNamespace(uuid=ble_selftest.SENSOR_LOCATION_UUID, handle=20)
    csc_location = SimpleNamespace(uuid=ble_selftest.SENSOR_LOCATION_UUID, handle=32)
    services = [
        SimpleNamespace(
            uuid="00001818-0000-1000-8000-00805f9b34fb",
            characteristics=[cycling_location],
        ),
        SimpleNamespace(
            uuid="00001816-0000-1000-8000-00805f9b34fb",
            characteristics=[csc_location],
        ),
    ]

    selected = ble_selftest.find_characteristic_in_service(
        services,
        "00001818-0000-1000-8000-00805f9b34fb",
        ble_selftest.SENSOR_LOCATION_UUID,
    )
    assert selected is cycling_location, "重复 UUID 必须按所属 service 定位"
    print("[ble-selftest-helper] PASS: 重复特征按所属 service 定位")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
