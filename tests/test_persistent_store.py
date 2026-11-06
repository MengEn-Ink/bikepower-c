#!/usr/bin/env python3
"""执行生产 NVS 和快照代码的 immediate-write、截断及重启故障回归。"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="bikepower-nvs-") as temp:
        folder = Path(temp)
        for name in ("nvs.h", "esp_log.h", "esp_err.h", "freertos/FreeRTOS.h", "freertos/semphr.h"):
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "nvs_stubs.h"\n', encoding="utf-8")
        binary = folder / "persistent-test"
        sources = sorted((ROOT / "components/bikepower_domain").glob("*.c"))
        subprocess.run([
            "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-I", str(folder), "-I", str(ROOT / "test"), "-I", str(ROOT / "main"),
            "-I", str(ROOT / "components/bikepower_domain/include"),
            str(ROOT / "test/test_persistent_store.c"), str(ROOT / "main/persistent_store.c"),
            str(ROOT / "main/nvs_store.c"), str(ROOT / "main/device_identity_store.c"),
            *map(str, sources), "-lm", "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary), *sys.argv[1:]], check=True)


if __name__ == "__main__":
    main()
