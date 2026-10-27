#!/usr/bin/env python3
"""BikePower Config Service 真机脚本的离线协议回归。"""

import struct

from ble_config_e2e import (
    GROUP_MASK_ALL,
    OP_BEGIN,
    OP_CANCEL,
    OP_COMMIT,
    PROTOCOL_VERSION,
    decode_counted_string,
    make_begin,
    make_cancel,
    make_commit,
    make_gatt_rebuild_groups,
    make_invalid_groups,
    make_modified_groups,
    parse_meta,
    parse_response,
    validate_groups,
)


TAG = "[ble-config-e2e-unit]"


def fixture_groups():
    core = struct.pack("<BHHBHBBBBBBB", 1, 200, 90, 0, 130, 1, 5, 2, 3, 10, 0, 7)
    profile = struct.pack("<BHHHBBB", 1, 70, 250, 170, 0, 15, 25) + bytes([6]) + b"Custom"
    running = struct.pack("<BH BHBBBHH", 1, 1000, 5, 170, 2, 0, 0, 300, 360)
    running += bytes([6]) + b"Runner"
    led = bytes([1, 0, 1, 0, 2, 1, 3, 2, 4, 3, 5, 4])
    identity = bytes([1, 9]) + b"BikePower"
    return {
        "core": core,
        "profile": profile,
        "running": running,
        "led": led,
        "identity": identity,
    }


def main():
    meta = struct.pack("<BBBBIHH", 1, 12, 0x1F, 0x0F, 0x11223344, 48, 0)
    parsed_meta = parse_meta(meta)
    assert parsed_meta["revision"] == 0x11223344
    assert parsed_meta["group_mask"] == GROUP_MASK_ALL

    response = struct.pack("<BBBBII", 0x80, OP_COMMIT, 0, 3, 0x01020304, 9)
    parsed_response = parse_response(response)
    assert parsed_response == {
        "request": OP_COMMIT,
        "result": 0,
        "restart_flags": 3,
        "transaction_id": 0x01020304,
        "revision": 9,
    }

    assert make_begin(0x01020304, 0x11223344) == bytes.fromhex(
        "01010403020144332211"
    )
    assert make_commit(0x01020304) == bytes.fromhex("02040302011f")
    assert make_cancel(0x01020304) == bytes.fromhex("0304030201")
    assert make_begin(1, 2)[0] == OP_BEGIN
    assert make_commit(1)[0] == OP_COMMIT
    assert make_cancel(1)[0] == OP_CANCEL

    original = fixture_groups()
    validate_groups(original)
    assert decode_counted_string(original["identity"], 1, 23) == "BikePower"

    modified = make_modified_groups(original)
    validate_groups(modified)
    assert all(modified[key] != original[key] for key in original)
    assert decode_counted_string(modified["identity"], 1, 23) == "BikePower-E2E"

    invalid = make_invalid_groups(original)
    assert struct.unpack_from("<H", invalid["core"], 1)[0] == 0xFFFF
    assert invalid["profile"] == original["profile"]

    rebuilt = make_gatt_rebuild_groups(modified)
    assert bool(rebuilt["core"][14] & 0x04) != bool(modified["core"][14] & 0x04)
    assert rebuilt["profile"] == modified["profile"]
    assert PROTOCOL_VERSION == 1

    print(f"{TAG} PASS 元数据、控制帧、五组修改、非法值与 GATT 掩码回归")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
