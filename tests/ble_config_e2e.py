#!/usr/bin/env python3
"""BikePower Config Service 真机端到端测试。

覆盖五组配置读取、事务错误路径、全量写入、NVS 重启持久化、GATT 重建，
并在 finally 中恢复测试前的完整配置，避免污染开发板。
"""

import argparse
import asyncio
import re
import secrets
import struct
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

CONFIG_SERVICE_UUID = "7a5a0000-4c7a-4f2a-9b3e-6d1f4a8c0b21"
CONFIG_META_UUID = "7a5a0001-4c7a-4f2a-9b3e-6d1f4a8c0b21"
CONFIG_GROUP_UUIDS = {
    "core": "7a5a0002-4c7a-4f2a-9b3e-6d1f4a8c0b21",
    "profile": "7a5a0003-4c7a-4f2a-9b3e-6d1f4a8c0b21",
    "running": "7a5a0004-4c7a-4f2a-9b3e-6d1f4a8c0b21",
    "led": "7a5a0005-4c7a-4f2a-9b3e-6d1f4a8c0b21",
    "identity": "7a5a0006-4c7a-4f2a-9b3e-6d1f4a8c0b21",
}
CONFIG_CONTROL_UUID = "7a5a0007-4c7a-4f2a-9b3e-6d1f4a8c0b21"
DIS_FIRMWARE_UUID = "00002a26-0000-1000-8000-00805f9b34fb"
HR_SERVICE_UUID = "0000180d-0000-1000-8000-00805f9b34fb"

PROTOCOL_VERSION = 1
GROUP_MASK_ALL = 0x1F
OP_BEGIN = 0x01
OP_COMMIT = 0x02
OP_CANCEL = 0x03
OP_RESPONSE = 0x80

RESULT_SUCCESS = 0x00
RESULT_STALE_REVISION = 0x04
RESULT_INVALID_VALUE = 0x07
RESULT_INCOMPLETE = 0x08

RESTART_GATT = 0x01
RESTART_NAME = 0x02


class ConfigE2EError(RuntimeError):
    """配置端到端断言失败。"""


def require(condition, message):
    if not condition:
        raise ConfigE2EError(message)


def parse_meta(data):
    data = bytes(data)
    require(len(data) == 12, f"Meta 长度错误: {len(data)}")
    protocol, schema, group_mask, capabilities, revision, max_len, reserved = struct.unpack(
        "<BBBBIHH", data
    )
    require(protocol == PROTOCOL_VERSION, f"协议版本不支持: {protocol}")
    require((group_mask & GROUP_MASK_ALL) == GROUP_MASK_ALL, "设备未提供全部五组配置")
    require(revision != 0, "revision 不得为 0")
    require(max_len >= 15, f"最大组长度异常: {max_len}")
    require(reserved == 0, f"Meta 保留字段非 0: {reserved}")
    return {
        "protocol": protocol,
        "schema": schema,
        "group_mask": group_mask,
        "capabilities": capabilities,
        "revision": revision,
        "max_len": max_len,
    }


def parse_response(data):
    data = bytes(data)
    require(len(data) == 12, f"Control 响应长度错误: {len(data)}")
    opcode, request, result, restart_flags, transaction_id, revision = struct.unpack(
        "<BBBBII", data
    )
    require(opcode == OP_RESPONSE, f"Control 响应 opcode 错误: 0x{opcode:02x}")
    return {
        "request": request,
        "result": result,
        "restart_flags": restart_flags,
        "transaction_id": transaction_id,
        "revision": revision,
    }


def make_begin(transaction_id, revision):
    return struct.pack("<BBII", OP_BEGIN, PROTOCOL_VERSION, transaction_id, revision)


def make_commit(transaction_id, required_mask=GROUP_MASK_ALL):
    return struct.pack("<BIB", OP_COMMIT, transaction_id, required_mask)


def make_cancel(transaction_id):
    return struct.pack("<BI", OP_CANCEL, transaction_id)


def decode_counted_string(data, offset, maximum):
    require(offset < len(data), "字符串长度字段缺失")
    length = data[offset]
    require(0 < length <= maximum, f"字符串长度无效: {length}")
    require(offset + 1 + length == len(data), "字符串载荷长度不匹配")
    try:
        return data[offset + 1 :].decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ConfigE2EError(f"字符串不是 UTF-8: {exc}") from exc


def validate_groups(groups):
    require(set(groups) == set(CONFIG_GROUP_UUIDS), "配置组集合不完整")
    core = groups["core"]
    require(len(core) == 15 and core[0] == PROTOCOL_VERSION, "Core 组格式错误")
    require(core[14] & 0x07, "broadcast_mask 不得为空")

    profile = groups["profile"]
    require(len(profile) >= 12 and profile[0] == PROTOCOL_VERSION, "Profile 组格式错误")
    decode_counted_string(profile, 10, 31)

    running = groups["running"]
    require(len(running) >= 15 and running[0] == PROTOCOL_VERSION, "Running 组格式错误")
    decode_counted_string(running, 13, 31)

    led = groups["led"]
    require(len(led) == 12 and led[0] == PROTOCOL_VERSION, "LED 组格式错误")

    identity = groups["identity"]
    require(len(identity) >= 3 and identity[0] == PROTOCOL_VERSION, "Identity 组格式错误")
    decode_counted_string(identity, 1, 23)


def encode_identity(name):
    encoded = name.encode("utf-8")
    require(0 < len(encoded) <= 23, "E2E 设备名 UTF-8 长度越界")
    return bytes([PROTOCOL_VERSION, len(encoded)]) + encoded


def _toggle_u16(data, offset, minimum, maximum):
    value = struct.unpack_from("<H", data, offset)[0]
    next_value = value + 1 if value < maximum else value - 1
    require(minimum <= next_value <= maximum, f"无法在范围内修改值: {value}")
    struct.pack_into("<H", data, offset, next_value)


def make_modified_groups(original):
    """每一组至少修改一个合法字段，验证五组写入而不切换 profile。"""
    modified = {key: bytearray(value) for key, value in original.items()}
    _toggle_u16(modified["core"], 1, 0, 2000)
    _toggle_u16(modified["profile"], 1, 40, 150)

    pace_float = modified["running"][3]
    modified["running"][3] = pace_float + 1 if pace_float < 60 else pace_float - 1
    modified["led"][1] = 0 if modified["led"][1] else 1

    current_name = decode_counted_string(bytes(modified["identity"]), 1, 23)
    e2e_name = "BikePower-E2E" if current_name != "BikePower-E2E" else "BikePower-E2F"
    modified["identity"] = bytearray(encode_identity(e2e_name))
    result = {key: bytes(value) for key, value in modified.items()}
    validate_groups(result)
    return result


def make_invalid_groups(original):
    invalid = dict(original)
    core = bytearray(original["core"])
    struct.pack_into("<H", core, 1, 0xFFFF)
    invalid["core"] = bytes(core)
    return invalid


def make_gatt_rebuild_groups(current):
    changed = dict(current)
    core = bytearray(current["core"])
    mask = core[14] & 0x07
    if mask & 0x04:
        core[14] = (mask & ~0x04) or 0x01
    else:
        core[14] = mask | 0x04
    changed["core"] = bytes(core)
    validate_groups(changed)
    return changed


def expected_firmware_name(board):
    if not board:
        return ""
    source = (ROOT / "main" / "bikepower_config.h").read_text(encoding="utf-8")

    def define(name):
        match = re.search(rf'#define\s+{name}\s+"([^"]+)"', source)
        require(match is not None, f"无法读取 {name}")
        return match.group(1)

    git_sha = subprocess.check_output(
        ["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"], text=True
    ).strip()
    return (
        f"{board}-v{define('FIRMWARE_VERSION')}-stable{define('FIRMWARE_STABLE_VERSION')}"
        f"-fix{define('FIRMWARE_FIX_VERSION')}-g{git_sha}"
    )


def reset_device(port):
    import serial

    serial_port = serial.Serial(port, 115200, timeout=1)
    try:
        serial_port.setDTR(False)
        serial_port.setRTS(True)
        time.sleep(0.1)
        serial_port.setRTS(False)
        time.sleep(0.1)
    finally:
        serial_port.close()


class DeviceSession:
    def __init__(self, device_name, scan_timeout):
        self.device_name = device_name
        self.scan_timeout = scan_timeout
        self.address = ""
        self.client = None
        self.responses = asyncio.Queue()
        self.notify_started = False

    async def _find_device(self):
        from bleak import BleakScanner

        if self.address:
            device = await BleakScanner.find_device_by_filter(
                lambda candidate, _adv: candidate.address == self.address,
                timeout=self.scan_timeout,
            )
            if device:
                return device
        device = await BleakScanner.find_device_by_filter(
            lambda candidate, adv: (adv.local_name or candidate.name or "") == self.device_name,
            timeout=self.scan_timeout,
        )
        require(device is not None, f"未发现 BLE 设备 {self.device_name}")
        return device

    async def connect(self):
        from bleak import BleakClient

        if self.client and self.client.is_connected:
            return
        device = await self._find_device()
        self.address = device.address
        self.client = BleakClient(device)
        await self.client.connect()
        self.responses = asyncio.Queue()
        self.notify_started = False
        service_uuids = {service.uuid.lower() for service in self.client.services}
        require(CONFIG_SERVICE_UUID in service_uuids, "未发现 BikePower Config Service")

    async def disconnect(self):
        if self.client and self.client.is_connected:
            await self.client.disconnect()
        self.client = None
        self.notify_started = False

    async def ensure_notifications(self):
        if self.notify_started:
            return

        def on_control(_sender, data):
            self.responses.put_nowait(bytes(data))

        await self.client.start_notify(CONFIG_CONTROL_UUID, on_control)
        self.notify_started = True

    async def wait_response(self, request, transaction_id, timeout=12):
        loop = asyncio.get_running_loop()
        deadline = loop.time() + timeout
        while loop.time() < deadline:
            data = await asyncio.wait_for(self.responses.get(), deadline - loop.time())
            response = parse_response(data)
            if (
                response["request"] == request
                and response["transaction_id"] == transaction_id
            ):
                return response
        raise ConfigE2EError(f"等待 Control 响应超时: request=0x{request:02x}")

    async def control(self, payload, request, transaction_id):
        await self.ensure_notifications()
        await self.client.write_gatt_char(CONFIG_CONTROL_UUID, payload, response=True)
        return await self.wait_response(request, transaction_id)

    async def read_snapshot(self):
        meta = parse_meta(await self.client.read_gatt_char(CONFIG_META_UUID))
        groups = {
            key: bytes(await self.client.read_gatt_char(uuid))
            for key, uuid in CONFIG_GROUP_UUIDS.items()
        }
        validate_groups(groups)
        return meta, groups

    async def write_groups(self, groups):
        for key, uuid in CONFIG_GROUP_UUIDS.items():
            await self.client.write_gatt_char(uuid, groups[key], response=True)

    async def commit_all(self, groups, revision):
        transaction_id = secrets.randbits(32) or 1
        response = await self.control(
            make_begin(transaction_id, revision), OP_BEGIN, transaction_id
        )
        require(response["result"] == RESULT_SUCCESS, f"BEGIN 失败: {response}")
        await self.write_groups(groups)
        response = await self.control(
            make_commit(transaction_id), OP_COMMIT, transaction_id
        )
        return response

    def has_service(self, uuid):
        return uuid.lower() in {service.uuid.lower() for service in self.client.services}


async def reconnect(session, settle_seconds):
    await session.disconnect()
    await asyncio.sleep(settle_seconds)
    await session.connect()


async def verify_firmware(session, expected):
    if not expected:
        return
    actual = bytes(await session.client.read_gatt_char(DIS_FIRMWARE_UUID)).decode("utf-8")
    require(actual == expected, f"固件标识不匹配: actual={actual} expected={expected}")
    print(f"[ble-config-e2e] PASS 固件标识 {actual}")


async def verify_snapshot(session, expected, label):
    meta, actual = await session.read_snapshot()
    require(actual == expected, f"{label} 五组配置与期望不一致")
    print(f"[ble-config-e2e] PASS {label}（revision {meta['revision']}）")
    return meta


async def restore_original(session, original, port, settle_seconds):
    """无论主测试是否成功，都清理活动事务并恢复完整原配置。"""
    await reconnect(session, settle_seconds)
    meta, current = await session.read_snapshot()
    if current != original:
        response = await session.commit_all(original, meta["revision"])
        require(response["result"] == RESULT_SUCCESS, f"恢复原配置失败: {response}")
        if response["restart_flags"] & RESTART_GATT:
            await reconnect(session, settle_seconds)
    await verify_snapshot(session, original, "恢复后读取")
    await session.disconnect()
    await asyncio.to_thread(reset_device, port)
    await asyncio.sleep(settle_seconds)
    await session.connect()
    await verify_snapshot(session, original, "恢复后重启持久化")
    print("[ble-config-e2e] PASS 已恢复测试前全部配置并验证 NVS")


async def run_e2e(args):
    session = DeviceSession(args.device_name, args.scan_timeout)
    original = None
    primary_error = None
    cleanup_error = None
    try:
        await session.connect()
        await verify_firmware(session, expected_firmware_name(args.board))
        meta, original = await session.read_snapshot()
        print(
            "[ble-config-e2e] PASS 五组配置全量读取 "
            f"schema={meta['schema']} revision={meta['revision']}"
        )
        base_revision = meta["revision"]

        stale_id = secrets.randbits(32) or 1
        stale_revision = base_revision + 1 if base_revision < 0xFFFFFFFF else base_revision - 1
        response = await session.control(
            make_begin(stale_id, stale_revision), OP_BEGIN, stale_id
        )
        require(response["result"] == RESULT_STALE_REVISION, f"旧 revision 未被拒绝: {response}")
        print("[ble-config-e2e] PASS stale revision 拒绝")

        cancel_id = secrets.randbits(32) or 1
        response = await session.control(
            make_begin(cancel_id, base_revision), OP_BEGIN, cancel_id
        )
        require(response["result"] == RESULT_SUCCESS, f"取消场景 BEGIN 失败: {response}")
        response = await session.control(make_cancel(cancel_id), OP_CANCEL, cancel_id)
        require(response["result"] == RESULT_SUCCESS, f"CANCEL 失败: {response}")
        print("[ble-config-e2e] PASS CANCEL 不落盘")

        incomplete_id = secrets.randbits(32) or 1
        response = await session.control(
            make_begin(incomplete_id, base_revision), OP_BEGIN, incomplete_id
        )
        require(response["result"] == RESULT_SUCCESS, f"缺组场景 BEGIN 失败: {response}")
        await session.client.write_gatt_char(
            CONFIG_GROUP_UUIDS["core"], original["core"], response=True
        )
        response = await session.control(
            make_commit(incomplete_id), OP_COMMIT, incomplete_id
        )
        require(response["result"] == RESULT_INCOMPLETE, f"缺组提交未被拒绝: {response}")
        response = await session.control(make_cancel(incomplete_id), OP_CANCEL, incomplete_id)
        require(response["result"] == RESULT_SUCCESS, f"缺组事务取消失败: {response}")
        print("[ble-config-e2e] PASS 不完整事务拒绝")

        invalid = make_invalid_groups(original)
        response = await session.commit_all(invalid, base_revision)
        require(response["result"] == RESULT_INVALID_VALUE, f"非法值未被拒绝: {response}")
        meta_after_invalid, groups_after_invalid = await session.read_snapshot()
        require(meta_after_invalid["revision"] == base_revision, "非法提交改变了 revision")
        require(groups_after_invalid == original, "非法提交污染了正式配置")
        print("[ble-config-e2e] PASS 非法值失败不污染配置")

        modified = make_modified_groups(original)
        response = await session.commit_all(modified, base_revision)
        require(response["result"] == RESULT_SUCCESS, f"五组修改提交失败: {response}")
        require(response["revision"] != base_revision, "成功提交未推进 revision")
        require(response["restart_flags"] & RESTART_NAME, "设备名修改未返回刷新提示")
        require(not response["restart_flags"] & RESTART_GATT, "普通五组修改意外触发 GATT 重建")
        await verify_snapshot(session, modified, "五组修改后读取")

        await reconnect(session, args.settle_seconds)
        await verify_snapshot(session, modified, "重新连接后读取")
        await session.disconnect()
        await asyncio.to_thread(reset_device, args.port)
        await asyncio.sleep(args.settle_seconds)
        await session.connect()
        meta_persisted = await verify_snapshot(session, modified, "设备重启后 NVS 读取")

        rebuilt = make_gatt_rebuild_groups(modified)
        expected_hr = bool(rebuilt["core"][14] & 0x04)
        response = await session.commit_all(rebuilt, meta_persisted["revision"])
        require(response["result"] == RESULT_SUCCESS, f"广播掩码提交失败: {response}")
        require(response["restart_flags"] & RESTART_GATT, "广播掩码修改未提示 GATT 重建")
        await reconnect(session, args.settle_seconds)
        await verify_snapshot(session, rebuilt, "GATT 重建后配置读取")
        require(
            session.has_service(HR_SERVICE_UUID) == expected_hr,
            "GATT 重建后的 HRS 注册状态与 broadcast_mask 不一致",
        )
        print("[ble-config-e2e] PASS 广播掩码触发断连、重广播与服务表重建")
    except Exception as exc:
        primary_error = exc
    finally:
        if original is not None:
            try:
                await restore_original(session, original, args.port, args.settle_seconds)
            except Exception as exc:
                cleanup_error = exc
        await session.disconnect()

    if primary_error is not None:
        raise ConfigE2EError(
            f"主测试失败: {type(primary_error).__name__}: {primary_error}"
            + (
                f"；且恢复失败: {type(cleanup_error).__name__}: {cleanup_error}"
                if cleanup_error
                else ""
            )
        )
    if cleanup_error is not None:
        raise ConfigE2EError(
            f"主测试通过但恢复失败: {type(cleanup_error).__name__}: {cleanup_error}"
        )
    print("[ble-config-e2e] PASS 配置读写、错误路径、NVS 持久化与 GATT 重建全部通过")


def parse_args():
    parser = argparse.ArgumentParser(description="BikePower Config Service 真机端到端测试")
    parser.add_argument("--port", required=True, help="用于重启并验证 NVS 的开发板串口")
    parser.add_argument("--board", choices=("generic-c3", "m5stamp-c3"), default="")
    parser.add_argument("--device-name", default="BikePower")
    parser.add_argument("--scan-timeout", type=float, default=20.0)
    parser.add_argument("--settle-seconds", type=float, default=4.0)
    return parser.parse_args()


def main():
    args = parse_args()
    try:
        asyncio.run(run_e2e(args))
    except Exception as exc:
        print(f"[ble-config-e2e] FAIL {type(exc).__name__}: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
