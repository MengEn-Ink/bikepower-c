#!/usr/bin/env python3
"""以人工真值和 CPU 窗口验证生产遥测编码器。"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    implementation = ROOT / "components/bikepower_domain/device_telemetry.c"
    assert implementation.exists(), "缺少生产遥测编码器"
    with tempfile.TemporaryDirectory(prefix="bikepower-telemetry-") as folder:
        binary = Path(folder) / "telemetry"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined",
                        "-I", str(ROOT / "components/bikepower_domain/include"),
                        str(ROOT / "test/test_device_telemetry.c"),
                        str(implementation), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        for name in ("freertos/FreeRTOS.h", "nimble/nimble_port.h",
                     "host/ble_hs.h", "host/ble_gap.h", "host/ble_att.h"):
            header = Path(folder) / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('#include "telemetry_service_stubs.h"\n')
        service = Path(folder) / "service"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-I", folder,
                        "-I", str(ROOT / "test"),
                        "-I", str(ROOT / "components/bikepower_domain/include"),
                        str(ROOT / "test/test_ble_telemetry_service.c"),
                        str(implementation), "-o", str(service)], check=True)
        subprocess.run([str(service)], check=True)
    print("PASS: 遥测四组人工真值、边界与 CPU 窗口")

if __name__ == "__main__":
    main()
