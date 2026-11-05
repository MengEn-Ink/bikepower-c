"""开发板真实遥测的严格只读解码，单位与 Pages 一致。"""
import struct

def uuid(index):
    return f"7a5a{0x1000 + index:04x}-4c7a-4f2a-9b3e-6d1f4a8c0b21"

def validate(data):
    if len(data) != 20 or data[0] != 1:
        raise ValueError("遥测需要版本 1 的 20 字节记录")

def decode_meta(data):
    validate(data)
    period, boot = struct.unpack_from("<HI", data, 2)
    if data[1] != 15 or not 100 <= period <= 60000:
        raise ValueError("未知遥测能力或采样周期")
    return {"period_ms": period, "boot": boot,
            "version": ".".join(map(str, data[8:11])),
            "board": "m5stamp-c3" if data[11] == 1 else "generic-c3",
            "reset_reason": data[12], "git": data[13:20].decode("ascii").rstrip("\0")}

def decode(group, data):
    validate(data)
    result = {"sequence": struct.unpack_from("<H", data, 2)[0]}
    if group == 0:
        free, minimum, largest, total = struct.unpack_from("<IIII", data, 4)
        if not data[1] & 1 or not total or not minimum <= free <= total or largest > free:
            raise ValueError("无效堆快照")
        result.update(free=free, minimum=minimum, largest=largest, total=total,
                      free_percent=100 * free / total)
    elif group == 1:
        uptime, busy, mhz, main, stack, state, profile = struct.unpack_from("<IHHHIBB", data, 4)
        valid = bool(data[1] & 1)
        if valid and not main <= busy <= 10000:
            raise ValueError("无效 CPU 窗口")
        result.update(uptime_s=uptime, cpu_percent=busy / 100 if valid else None,
                      main_percent=main / 100 if valid else None, mhz=mhz,
                      main_stack=stack, state=state, profile=profile)
    elif group == 2:
        rssi, connected, mtu, interval, latency, timeout = struct.unpack_from("<bBHHHH", data, 4)
        result.update(rssi=rssi if data[1] & 2 else None, connected=bool(connected),
                      mtu=mtu, interval_ms=interval * 1.25, latency=latency, timeout_ms=timeout * 10)
    elif group == 3:
        values = struct.unpack_from("<IIII", data, 4)
        result.update(zip(("submitted", "failed", "allocation_failed", "lock_skipped"), values))
    else:
        raise ValueError("未知遥测组")
    return result
