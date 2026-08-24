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

    pause_samples = [
        {"running": True, "speed": 850, "cadence": 160},
        {"running": False, "speed": 0, "cadence": 0},
        {"running": False, "speed": 0, "cadence": 0},
    ]
    assert ble_selftest.has_stable_tail(
        pause_samples,
        lambda sample: not sample["running"] and sample["speed"] == 0 and sample["cadence"] == 0,
        minimum=2,
    ), "允许命令响应后的首帧仍是过渡帧"
    assert not ble_selftest.has_stable_tail(
        pause_samples[:2],
        lambda sample: not sample["running"],
        minimum=2,
    ), "稳定态必须至少连续两帧"
    assert ble_selftest.has_reset_sample(
        [{"distance": 143}, {"distance": 1}, {"distance": 2}],
        maximum_distance=2,
    ), "Reset 校验应接受窗口内任一清零后的样本"
    print("[ble-selftest-helper] PASS: 控制命令忽略单帧过渡并要求稳定尾帧")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
