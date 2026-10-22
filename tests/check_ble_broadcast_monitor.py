#!/usr/bin/env python3
"""BLE 广播项开关监控脚本静态门禁。"""
import asyncio
import contextlib
import io
from pathlib import Path
import py_compile
import sys
import types

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tests" / "ble_broadcast_monitor.py"
OP_GATE = ROOT / "tests" / "operation_gate.py"
OP_DOC = ROOT / "docs" / "dev" / "05-operation-test-gate.md"
README = ROOT / "tests" / "README.md"
QUALITY_GATE = ROOT / "scripts" / "tools" / "quality_gate.py"

TAG = "[ble-broadcast-check]"


def check(condition, message, failures):
    if condition:
        print(f"{TAG} PASS {message}")
    else:
        failures.append(message)
        print(f"{TAG} FAIL {message}")


def probe_restore_failure_is_fatal():
    """Use a dependency-free fake device flow to prove restore failure returns non-zero."""
    try:
        import bleak  # noqa: F401
    except ModuleNotFoundError:
        bleak_stub = types.ModuleType("bleak")
        bleak_stub.BleakClient = object
        bleak_stub.BleakScanner = object
        sys.modules["bleak"] = bleak_stub

    import ble_broadcast_monitor as monitor

    class Args:
        base_url = "http://fake"
        device_name = "BikePower"
        scan_timeout = 0
        profile_settle_sec = 0
        settle_sec = 0
        sample_sec = 0
        min_count = 0

    class FakeDevice:
        name = "BikePower"
        address = "fake-address"

    class FakeClient:
        def __init__(self, _device):
            pass

        async def __aenter__(self):
            return self

        async def __aexit__(self, _exc_type, _exc, _tb):
            return False

    async def fake_find_device(_name, _timeout):
        return FakeDevice()

    async def fake_subscribe(_client, _counters):
        return ["cps"]

    async def fake_sleep(_seconds):
        return None

    def fake_request(_base, path, payload=None, timeout=5):
        del payload, timeout
        if path == "/api/state":
            return {"sport_profile": "running", "broadcast_mask": 5}
        return {"ok": True}

    def fake_post(_base, profile, mask):
        if profile == "running" and mask == 5:
            raise RuntimeError("injected restore failure")

    originals = {
        "request_json": monitor.request_json,
        "post_config": monitor.post_config,
        "find_device": monitor.find_device,
        "BleakClient": monitor.BleakClient,
        "subscribe_available": monitor.subscribe_available,
        "evaluate_case": monitor.evaluate_case,
        "sleep": monitor.asyncio.sleep,
    }
    try:
        monitor.request_json = fake_request
        monitor.post_config = fake_post
        monitor.find_device = fake_find_device
        monitor.BleakClient = FakeClient
        monitor.subscribe_available = fake_subscribe
        monitor.evaluate_case = lambda _case, _counters, _min_count: []
        monitor.asyncio.sleep = fake_sleep
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            rc = asyncio.run(monitor.run(Args()))
        return rc == 1 and "FAIL 恢复原始配置失败" in output.getvalue()
    finally:
        monitor.request_json = originals["request_json"]
        monitor.post_config = originals["post_config"]
        monitor.find_device = originals["find_device"]
        monitor.BleakClient = originals["BleakClient"]
        monitor.subscribe_available = originals["subscribe_available"]
        monitor.evaluate_case = originals["evaluate_case"]
        monitor.asyncio.sleep = originals["sleep"]


def main():
    failures = []
    check(SCRIPT.is_file(), "存在 tests/ble_broadcast_monitor.py", failures)
    if not SCRIPT.is_file():
        print(f"{TAG} 结果: {len(failures)} 失败")
        return 1

    try:
        py_compile.compile(str(SCRIPT), doraise=True)
        print(f"{TAG} PASS ble_broadcast_monitor.py 语法正确")
    except py_compile.PyCompileError as exc:
        failures.append("ble_broadcast_monitor.py 语法错误")
        print(f"{TAG} FAIL {exc.msg}")

    src = SCRIPT.read_text(encoding="utf-8")
    op_gate = OP_GATE.read_text(encoding="utf-8")
    op_doc = OP_DOC.read_text(encoding="utf-8")
    readme = README.read_text(encoding="utf-8")
    quality_gate = QUALITY_GATE.read_text(encoding="utf-8")

    for token, message in [
        ("BleakClient", "使用 bleak 连接 BLE 设备"),
        ("BleakScanner", "使用 bleak 扫描 BikePower"),
        ("from ble_protocol import", "复用无 bleak 依赖的协议解析器"),
        ('"/api/config"', "通过 /api/config 切换广播项"),
        ('"/api/state"', "读取 /api/state 并保存原始配置"),
        ("post_config(base_url, original_profile, original_mask)", "测试结束恢复原始 profile 和 broadcast_mask"),
        ('all_failures.append(failure)', "恢复原始配置失败计入最终失败集合"),
        ('FAIL 恢复原始配置失败', "恢复失败日志明确标记 FAIL"),
        ('"all_on"', "包含全开场景"),
        ('"power_off"', "包含关闭功率场景"),
        ('"hr_only"', "包含只开心率场景"),
        ('"power_only"', "包含只开功率场景"),
        ("expect_zero(counts, \"cps\"", "关闭功率时断言 CPS 停止"),
        ("expect_zero(counts, \"ftms_power\"", "关闭功率时断言 FTMS 功率字段停止"),
        ("expect_positive(counts, \"hr\"", "开心率时断言 HRS 有通知"),
        ("expect_zero(counts, \"csc\"", "关闭踏频时断言 CSC 停止"),
        ("parse_ftms_indoor_bike", "解析 FTMS Indoor Bike 变长数据"),
        ('parsed["power_w"]', "FTMS 功率读取标准字段"),
        ('parsed["cadence_rpm"]', "FTMS 踏频读取标准字段"),
    ]:
        check(token in src, message, failures)

    restore_fail_pos = src.find('all_failures.append(failure)')
    final_fail_pos = src.find("if all_failures:", restore_fail_pos)
    final_return_pos = src.find("return 1", final_fail_pos)
    check(0 <= restore_fail_pos < final_fail_pos < final_return_pos,
          "恢复失败会流入最终非零返回", failures)
    check(probe_restore_failure_is_fatal(),
          "fake 恢复异常实测返回非零并输出 FAIL", failures)

    check("tests/ble_broadcast_monitor.py" in op_gate,
          "operation_gate.py 接入 BLE 广播项真机门禁", failures)
    check("tests/check_ble_broadcast_monitor.py" in quality_gate,
          "quality_gate.py 接入 BLE 广播项静态门禁", failures)
    check("tests/ble_broadcast_monitor.py" in op_doc and "OP-BCAST-003" in op_doc,
          "操作矩阵记录 BLE 广播项数据面门禁", failures)
    check("tests/ble_broadcast_monitor.py" in readme,
          "tests/README.md 记录 BLE 广播项真机门禁用法", failures)

    if failures:
        print(f"{TAG} 结果: {len(failures)} 失败")
        return 1
    print(f"{TAG} PASS: BLE 广播项监控脚本门禁通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
