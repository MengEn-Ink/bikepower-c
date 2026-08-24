#!/usr/bin/env python3
"""BLE 广播项开关监控脚本静态门禁。"""
from pathlib import Path
import py_compile
import sys

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tests" / "ble_broadcast_monitor.py"
OP_GATE = ROOT / "tests" / "operation_gate.py"
OP_DOC = ROOT / "docs" / "dev" / "04-operation-test-gate.md"
README = ROOT / "tests" / "README.md"
QUALITY_GATE = ROOT / "scripts" / "tools" / "quality_gate.py"

TAG = "[ble-broadcast-check]"


def check(condition, message, failures):
    if condition:
        print(f"{TAG} PASS {message}")
    else:
        failures.append(message)
        print(f"{TAG} FAIL {message}")


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
        ('"/api/config"', "通过 /api/config 切换广播项"),
        ('"/api/state"', "读取 /api/state 并保存原始配置"),
        ("post_config(base_url, original_profile, original_mask)", "测试结束恢复原始 profile 和 broadcast_mask"),
        ('"all_on"', "包含全开场景"),
        ('"power_off"', "包含关闭功率场景"),
        ('"hr_only"', "包含只开心率场景"),
        ('"power_only"', "包含只开功率场景"),
        ("expect_zero(counts, \"cps\"", "关闭功率时断言 CPS 停止"),
        ("expect_zero(counts, \"ftms_power\"", "关闭功率时断言 FTMS 功率字段停止"),
        ("expect_positive(counts, \"hr\"", "开心率时断言 HRS 有通知"),
        ("expect_zero(counts, \"csc\"", "关闭踏频时断言 CSC 停止"),
        ("parse_ftms_indoor_bike", "解析 FTMS Indoor Bike 变长数据"),
    ]:
        check(token in src, message, failures)

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
