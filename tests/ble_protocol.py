"""BikePower BLE measurement payload parsers.

The module has no bleak dependency so the same strict parsing rules can be used
by host golden-vector tests and live device harnesses.
"""

import struct


def _bytes(data):
    try:
        return bytes(data)
    except (TypeError, ValueError) as exc:
        raise ValueError("payload must be bytes-like") from exc


def _require_length(payload, expected, name):
    if len(payload) != expected:
        raise ValueError(f"{name} length must be {expected}, got {len(payload)}")


def parse_cps(data):
    payload = _bytes(data)
    _require_length(payload, 8, "CPS Measurement")
    flags = struct.unpack_from("<H", payload, 0)[0]
    if flags != 0x0020:
        raise ValueError(f"unsupported CPS flags 0x{flags:04x}")
    return {
        "flags": flags,
        "power_w": struct.unpack_from("<h", payload, 2)[0],
        "crank_revolutions": struct.unpack_from("<H", payload, 4)[0],
        "crank_event_time_1024": struct.unpack_from("<H", payload, 6)[0],
    }


def parse_csc(data):
    payload = _bytes(data)
    _require_length(payload, 5, "CSC Measurement")
    flags = payload[0]
    if flags != 0x02:
        raise ValueError(f"unsupported CSC flags 0x{flags:02x}")
    return {
        "flags": flags,
        "crank_revolutions": struct.unpack_from("<H", payload, 1)[0],
        "crank_event_time_1024": struct.unpack_from("<H", payload, 3)[0],
    }


def parse_hr(data):
    payload = _bytes(data)
    _require_length(payload, 2, "Heart Rate Measurement")
    flags = payload[0]
    if flags != 0x00:
        raise ValueError(f"unsupported HRS flags 0x{flags:02x}")
    return {"flags": flags, "heart_rate_bpm": payload[1]}


def parse_ftms_indoor_bike(data):
    payload = _bytes(data)
    if len(payload) < 2:
        raise ValueError(f"FTMS Indoor Bike length must be at least 2, got {len(payload)}")
    flags = struct.unpack_from("<H", payload, 0)[0]
    if flags & ~(0x0001 | 0x0004 | 0x0040):
        raise ValueError(f"unsupported FTMS Indoor Bike flags 0x{flags:04x}")

    offset = 2
    speed = None
    cadence_half = None
    power = None
    if not (flags & 0x0001):
        if len(payload) < offset + 2:
            raise ValueError("FTMS Indoor Bike speed field truncated")
        speed = struct.unpack_from("<H", payload, offset)[0]
        offset += 2
    if flags & 0x0004:
        if len(payload) < offset + 2:
            raise ValueError("FTMS Indoor Bike cadence field truncated")
        cadence_half = struct.unpack_from("<H", payload, offset)[0]
        offset += 2
    if flags & 0x0040:
        if len(payload) < offset + 2:
            raise ValueError("FTMS Indoor Bike power field truncated")
        power = struct.unpack_from("<h", payload, offset)[0]
        offset += 2
    if offset != len(payload):
        raise ValueError(f"FTMS Indoor Bike has {len(payload) - offset} trailing bytes")
    return {
        "flags": flags,
        "speed_x100_kmh": speed,
        "cadence_half_rpm": cadence_half,
        "cadence_rpm": cadence_half / 2.0 if cadence_half is not None else None,
        "power_w": power,
    }


def parse_treadmill(data):
    payload = _bytes(data)
    _require_length(payload, 10, "FTMS Treadmill Data")
    flags = struct.unpack_from("<H", payload, 0)[0]
    if flags != 0x0504:
        raise ValueError(f"unsupported FTMS Treadmill flags 0x{flags:04x}")
    distance = payload[4] | (payload[5] << 8) | (payload[6] << 16)
    return {
        "flags": flags,
        "speed_x100_kmh": struct.unpack_from("<H", payload, 2)[0],
        "total_distance_m": distance,
        "heart_rate_bpm": payload[7],
        "elapsed_time_s": struct.unpack_from("<H", payload, 8)[0],
    }


def parse_rsc(data):
    payload = _bytes(data)
    _require_length(payload, 10, "RSC Measurement")
    flags = payload[0]
    if flags not in (0x03, 0x07):
        raise ValueError(f"unsupported RSC flags 0x{flags:02x}")
    speed = struct.unpack_from("<H", payload, 1)[0]
    distance_dm = struct.unpack_from("<I", payload, 6)[0]
    return {
        "flags": flags,
        "speed_1_256_mps": speed,
        "speed_kmh": speed * 3.6 / 256.0,
        "cadence_spm": payload[3],
        "stride_cm": struct.unpack_from("<H", payload, 4)[0],
        "total_distance_dm": distance_dm,
        "total_distance_m": distance_dm / 10.0,
        "running": bool(flags & 0x04),
    }
