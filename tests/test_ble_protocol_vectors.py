#!/usr/bin/env python3
"""用固定协议向量校验 Python 解析器和纯 C codec。"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "fixtures" / "ble_protocol_vectors.json"
DOMAIN = ROOT / "components" / "bikepower_domain"
BLE_SOURCE = ROOT / "main" / "ble_bikepower.c"
BLE_CONFIG = ROOT / "main" / "bikepower_config.h"
sys.path.insert(0, str(ROOT / "tests"))

from ble_protocol import PARSERS  # noqa: E402


def load_vectors():
    return json.loads(FIXTURE.read_text(encoding="utf-8"))["vectors"]


def build_codec(output):
    sources = [
        ROOT / "test" / "ble_codec_cli.c",
        DOMAIN / "ble_payload_codec.c",
    ]
    command = [
        "cc",
        "-std=c11",
        "-Wall",
        "-Wextra",
        f"-I{DOMAIN / 'include'}",
        "-o",
        str(output),
        *(str(path) for path in sources),
    ]
    subprocess.run(command, cwd=ROOT, check=True)


def check_python(vectors):
    failures = []
    for vector in vectors:
        parser = PARSERS[vector["protocol"]]
        actual = parser(bytes.fromhex(vector["expected_hex"]))
        if actual != vector["expected"]:
            failures.append(
                f"{vector['name']} Python 解析不符 actual={actual} expected={vector['expected']}"
            )
    return failures


def check_codec(vectors):
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        codec_bin = Path(tmp) / "ble_codec_cli"
        build_codec(codec_bin)
        for vector in vectors:
            command = [
                str(codec_bin),
                vector["protocol"],
                *(str(value) for value in vector["args"]),
            ]
            result = subprocess.run(
                command,
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            actual = result.stdout.strip()
            if result.returncode != 0 or actual != vector["expected_hex"]:
                failures.append(
                    f"{vector['name']} C 编码不符 rc={result.returncode} "
                    f"actual={actual!r} expected={vector['expected_hex']!r}"
                )
    return failures


def check_firmware_uses_codec():
    source = BLE_SOURCE.read_text(encoding="utf-8")
    config = BLE_CONFIG.read_text(encoding="utf-8")
    required_calls = [
        "ble_payload_encode_cps(",
        "ble_payload_encode_csc(",
        "ble_payload_encode_hrs(",
        "ble_payload_encode_indoor_bike(",
        "ble_payload_encode_treadmill(",
        "ble_payload_encode_rsc(",
    ]
    failures = []
    if '#include "ble_payload_codec.h"' not in source:
        failures.append("ble_bikepower.c 未包含 ble_payload_codec.h")
    for call in required_calls:
        if call not in source:
            failures.append(f"ble_bikepower.c 未调用 {call}")
    if "static uint8_t g_cps_buf[8]" not in source:
        failures.append("CPS 生产缓冲区不是标准 8 字节")
    if "snapshot.total_distance_x10_m" not in source:
        failures.append("RSC 生产路径未使用 0.1m 分辨率累计距离")
    for token, message in [
        ("CYCLING_POWER_FEATURE_CHAR_UUID", "缺少 Cycling Power Feature UUID"),
        ("CSC_FEATURE_CHAR_UUID", "缺少 CSC Feature UUID"),
    ]:
        if token not in config:
            failures.append(message)
    for token, message in [
        ("g_cps_feature_buf[4] = { 0x08, 0x00, 0x00, 0x00 }",
         "Cycling Power Feature 必须声明 crank revolution data"),
        ("g_csc_feature_buf[2] = { 0x02, 0x00 }",
         "CSC Feature 必须声明 crank revolution data"),
        ("g_rsc_feature_buf[2] = { 0x07, 0x00 }",
         "RSC Feature 必须声明步幅、距离和 running status"),
        ("CYCLING_POWER_FEATURE_CHAR_UUID", "cycling GATT 缺少 Cycling Power Feature"),
        ("CSC_FEATURE_CHAR_UUID", "cycling GATT 缺少 CSC Feature"),
        ("SENSOR_LOCATION_CHAR_UUID", "cycling GATT 缺少 Sensor Location"),
    ]:
        if token not in source:
            failures.append(message)
    return failures


def main():
    vectors = load_vectors()
    failures = check_python(vectors)
    failures.extend(check_codec(vectors))
    failures.extend(check_firmware_uses_codec())
    if failures:
        for failure in failures:
            print(f"[ble-protocol] FAIL {failure}")
        return 1
    print(f"[ble-protocol] PASS: {len(vectors)} 个协议向量双路径通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
