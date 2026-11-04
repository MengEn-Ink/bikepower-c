#!/usr/bin/env python3
"""编译生产快照发布与打包函数，验证更新不会污染已采集的一轮通知。"""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from check_wifi_done_contract import extract_function


def main():
    source = (ROOT / "main/ble_bikepower.c").read_text()
    start = source.index("// 当前通知数据")
    end = source.index("// FTMS 静态特征值")
    functions = ["output_snapshot", "pack_all", "pack_running", "ble_bikepower_set_data",
                 "ble_bikepower_set_bridge_data", "ble_bikepower_set_running_data",
                 "ble_bikepower_set_broadcast_mask"]
    extracted = [extract_function(source, name) for name in functions]
    if not all(extracted):
        raise AssertionError("生产快照采集函数缺失")
    extracted = [("static ble_output_snapshot_t " if name == "output_snapshot" else "void ") + body
                 for name, body in zip(functions, extracted)]
    with tempfile.TemporaryDirectory(prefix="bikepower-ble-runtime-") as temp:
        folder = Path(temp)
        (folder / "ble_runtime_under_test.c").write_text(
            '#include <stdatomic.h>\n#include "bridge_stubs.h"\n'
            '#include "bikepower_config.h"\n#include "ble_payload_codec.h"\n'
            '#include "ble_bikepower.h"\n' + source[start:end] + "\n" + "\n".join(extracted))
        (folder / "esp_err.h").write_text('#include "bridge_stubs.h"\n')
        binary = folder / "runtime"
        subprocess.run([
            "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
            "-fsanitize=address,undefined", "-I", str(folder), "-I", str(ROOT / "test"),
            "-I", str(ROOT / "main"), "-I", str(ROOT / "components/bikepower_domain/include"),
            str(ROOT / "test/test_ble_runtime.c"),
            str(ROOT / "components/bikepower_domain/ble_payload_codec.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
