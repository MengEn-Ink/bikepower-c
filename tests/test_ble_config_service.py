#!/usr/bin/env python3
"""执行实际配置 GATT 回调，协议栈与锁使用最小宿主机替身。"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="bikepower-config-service-") as temp:
        folder = Path(temp)
        for name in ("esp_err.h", "esp_log.h", "freertos/FreeRTOS.h", "host/ble_gatt.h", "host/ble_hs.h"):
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "ble_service_stubs.h"\n')
        binary = folder / "config-service"
        sources = sorted((ROOT / "components/bikepower_domain").glob("*.c"))
        subprocess.run([
            "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
            "-fsanitize=address,undefined", "-I", str(folder), "-I", str(ROOT / "test"),
            "-I", str(ROOT / "components/bikepower_domain/include"),
            str(ROOT / "test/test_ble_config_service.c"), *map(str, sources),
            "-lm", "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
