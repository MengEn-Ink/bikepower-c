#!/usr/bin/env python3
"""使用 BLE/时钟替身执行生产会话 C 文件，验证实际回调与主循环状态变化。"""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADERS = [
    "esp_err.h", "esp_timer.h", "freertos/FreeRTOS.h",
    "host/ble_gap.h", "host/ble_gatt.h", "host/ble_hs.h", "host/ble_hs_adv.h", "os/os_mbuf.h",
]


def main():
    with tempfile.TemporaryDirectory(prefix="bikepower-bridge-") as temp:
        folder = pathlib.Path(temp)
        for name in HEADERS:
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "bridge_stubs.h"\n')
        binary = folder / "bridge-session"
        subprocess.run([
            "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-I", str(folder), "-I", str(ROOT / "test"),
            "-I", str(ROOT / "components/bikepower_domain/include"),
            str(ROOT / "test/test_bridge_session.c"),
            str(ROOT / "components/bikepower_domain/power_bridge.c"),
            str(ROOT / "components/bikepower_domain/cps_input_codec.c"),
            str(ROOT / "components/bikepower_domain/device_identity.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
        scan_binary = folder / "bridge-scan"
        subprocess.run([
            "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-I", str(folder), "-I", str(ROOT / "test"),
            "-I", str(ROOT / "components/bikepower_domain/include"),
            str(ROOT / "test/test_bridge_scan.c"),
            str(ROOT / "components/bikepower_domain/power_bridge.c"),
            str(ROOT / "components/bikepower_domain/device_identity.c"),
            "-o", str(scan_binary),
        ], check=True)
        subprocess.run([str(scan_binary)], check=True)
        runtime_binary = folder / "bridge-runtime"
        subprocess.run([
            "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-I", str(folder), "-I", str(ROOT / "test"),
            "-I", str(ROOT / "main"),
            "-I", str(ROOT / "components/bikepower_domain/include"),
            str(ROOT / "test/test_bridge_runtime.c"),
            str(ROOT / "main/bridge_runtime.c"),
            str(ROOT / "components/bikepower_domain/bridge_failover.c"),
            str(ROOT / "components/bikepower_domain/power_bridge.c"),
            str(ROOT / "components/bikepower_domain/device_identity.c"),
            "-o", str(runtime_binary),
        ], check=True)
        subprocess.run([str(runtime_binary)], check=True)


if __name__ == "__main__":
    main()
