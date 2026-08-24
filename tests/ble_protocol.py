#!/usr/bin/env python3
"""标准 BLE 运动数据 payload 解析器。"""
import struct


def parse_cps(data):
    if len(data) != 8:
        raise ValueError("CPS Measurement 必须为 8 字节")
    flags, power_w, crank_revs, crank_event_time = struct.unpack("<HhHH", data)
    if flags != 0x0020:
        raise ValueError(f"CPS flags 非法: 0x{flags:04x}")
    return {
        "flags": flags,
        "power_w": power_w,
        "crank_revs": crank_revs,
        "crank_event_time": crank_event_time,
    }


def parse_csc(data):
    if len(data) != 5 or data[0] != 0x02:
        raise ValueError("CSC crank-only payload 非法")
    crank_revs, crank_event_time = struct.unpack_from("<HH", data, 1)
    return {
        "flags": data[0],
        "crank_revs": crank_revs,
        "crank_event_time": crank_event_time,
    }


def parse_hrs(data):
    if len(data) != 2 or data[0] != 0:
        raise ValueError("HRS 8-bit payload 非法")
    return {"flags": data[0], "heart_rate_bpm": data[1]}


def parse_ftms_bike(data):
    if len(data) < 4:
        raise ValueError("FTMS Indoor Bike payload 过短")
    flags = struct.unpack_from("<H", data, 0)[0]
    offset = 2
    speed = None
    cadence = None
    power = None
    if not (flags & 0x0001):
        if len(data) < offset + 2:
            raise ValueError("FTMS Indoor Bike 缺 speed")
        speed = struct.unpack_from("<H", data, offset)[0]
        offset += 2
    if flags & 0x0004:
        if len(data) < offset + 2:
            raise ValueError("FTMS Indoor Bike 缺 cadence")
        cadence = struct.unpack_from("<H", data, offset)[0] // 2
        offset += 2
    if flags & 0x0040:
        if len(data) < offset + 2:
            raise ValueError("FTMS Indoor Bike 缺 power")
        power = struct.unpack_from("<h", data, offset)[0]
        offset += 2
    if offset != len(data):
        raise ValueError("FTMS Indoor Bike 存在未声明字段")
    return {
        "flags": flags,
        "speed_x100_kmh": speed,
        "cadence_rpm": cadence,
        "power_w": power,
    }


def parse_ftms_treadmill(data):
    if len(data) != 10:
        raise ValueError("FTMS Treadmill payload 必须为 10 字节")
    flags, speed = struct.unpack_from("<HH", data, 0)
    if flags != 0x0504:
        raise ValueError(f"FTMS Treadmill flags 非法: 0x{flags:04x}")
    distance = data[4] | (data[5] << 8) | (data[6] << 16)
    heart_rate = data[7]
    elapsed = struct.unpack_from("<H", data, 8)[0]
    return {
        "flags": flags,
        "speed_x100_kmh": speed,
        "distance_m": distance,
        "heart_rate_bpm": heart_rate,
        "elapsed_s": elapsed,
    }


def parse_rsc(data):
    if len(data) != 10:
        raise ValueError("RSC payload 必须为 10 字节")
    flags = data[0]
    speed = struct.unpack_from("<H", data, 1)[0]
    cadence = data[3]
    stride = struct.unpack_from("<H", data, 4)[0]
    distance_x10_m = struct.unpack_from("<I", data, 6)[0]
    return {
        "flags": flags,
        "running": bool(flags & 0x04),
        "speed_1_256_mps": speed,
        "cadence_spm": cadence,
        "stride_cm": stride,
        "total_distance_x10_m": distance_x10_m,
    }


PARSERS = {
    "cps": parse_cps,
    "csc": parse_csc,
    "hrs": parse_hrs,
    "ftms_bike": parse_ftms_bike,
    "ftms_treadmill": parse_ftms_treadmill,
    "rsc": parse_rsc,
}
