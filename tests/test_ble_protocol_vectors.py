#!/usr/bin/env python3
"""Verify the C encoder and Python parser against independent BLE vectors."""

import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

from ble_protocol import (
    parse_cps,
    parse_csc,
    parse_ftms_indoor_bike,
    parse_hr,
    parse_rsc,
    parse_treadmill,
)


ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "fixtures" / "ble_protocol_vectors.json"
CLI = ROOT / "test" / ("ble_codec_cli.exe" if os.name == "nt" else "ble_codec_cli")
PARSER_ONLY_FLAG = "--parser-only"
PARSER_ONLY = PARSER_ONLY_FLAG in sys.argv
if PARSER_ONLY:
    sys.argv.remove(PARSER_ONLY_FLAG)

PARSERS = {
    "cps": parse_cps,
    "csc": parse_csc,
    "hrs": parse_hr,
    "ftms_bike": parse_ftms_indoor_bike,
    "ftms_treadmill": parse_treadmill,
    "rsc": parse_rsc,
}


def cli_args(vector):
    protocol = vector["protocol"]
    fields = vector["input"]
    if protocol == "cps":
        values = [fields["power_w"], fields["crank_revolutions"], fields["crank_event_time_1024"]]
    elif protocol == "csc":
        values = [fields["crank_revolutions"], fields["crank_event_time_1024"]]
    elif protocol == "hrs":
        values = [fields["heart_rate_bpm"]]
    elif protocol == "ftms_bike":
        values = [
            int(fields["include_cadence"]),
            int(fields["include_power"]),
            fields["speed_x100_kmh"],
            fields["cadence_half_rpm"],
            fields["power_w"],
        ]
    elif protocol == "ftms_treadmill":
        values = [
            fields["speed_x100_kmh"],
            fields["total_distance_m"],
            fields["heart_rate_bpm"],
            fields["elapsed_time_s"],
        ]
    elif protocol == "rsc":
        values = [
            int(fields["running"]),
            fields["speed_1_256_mps"],
            fields["cadence_spm"],
            fields["stride_cm"],
            fields["total_distance_dm"],
        ]
    else:
        raise AssertionError(f"unsupported protocol {protocol}")
    return [str(CLI), protocol, *[str(value) for value in values]]


class BleProtocolVectorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vectors = json.loads(FIXTURE.read_text(encoding="utf-8"))["vectors"]
        if not PARSER_ONLY:
            subprocess.run(
                ["make", "-C", "test", "ble_codec_cli"],
                cwd=ROOT,
                check=True,
                text=True,
            )

    @unittest.skipIf(PARSER_ONLY, "--parser-only 不构建 host C codec")
    def test_c_encoder_matches_fixed_hex(self):
        for vector in self.vectors:
            with self.subTest(vector=vector["name"]):
                result = subprocess.run(
                    cli_args(vector),
                    cwd=ROOT,
                    check=True,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(result.stdout.strip(), vector["expected_hex"])

    def test_python_parser_matches_fixed_fields(self):
        for vector in self.vectors:
            with self.subTest(vector=vector["name"]):
                payload = bytes.fromhex(vector["expected_hex"])
                decoded = PARSERS[vector["protocol"]](payload)
                for field, expected in vector["expected_fields"].items():
                    actual = decoded[field]
                    if isinstance(expected, float):
                        self.assertAlmostEqual(actual, expected, places=6, msg=field)
                    else:
                        self.assertEqual(actual, expected, field)

    def test_strict_parsers_reject_old_or_inconsistent_payloads(self):
        with self.assertRaises(ValueError):
            parse_cps(bytes.fromhex("20fa000c0000003412"))
        with self.assertRaises(ValueError):
            parse_treadmill(bytes.fromhex("0603e803393000964101"))
        with self.assertRaises(ValueError):
            parse_ftms_indoor_bike(bytes.fromhex("4400b80bb400"))


if __name__ == "__main__":
    unittest.main()
