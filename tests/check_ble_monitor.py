#!/usr/bin/env python3
"""BLE 浏览器监控客户端静态门禁。

检查本地 Web Bluetooth 监控页面和本地服务入口是否存在，并覆盖自动重连、
骑行/跑步数据解析、异常检测和导出能力。
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
HTML = ROOT / "tools" / "web" / "ble-monitor.html"
SERVER = ROOT / "scripts" / "tools" / "serve_ble_monitor.py"
OP_DOC = ROOT / "docs" / "dev" / "05-operation-test-gate.md"
TESTS_README = ROOT / "tests" / "README.md"
QUALITY_GATE = ROOT / "scripts" / "tools" / "quality_gate.py"
OP_GATE = ROOT / "tests" / "operation_gate.py"

TAG = "[ble-monitor-check]"


def check(condition, message, failures):
    if condition:
        print(f"{TAG} PASS {message}")
    else:
        failures.append(message)
        print(f"{TAG} FAIL {message}")


def main():
    failures = []
    check(HTML.is_file(), "存在 tools/web/ble-monitor.html", failures)
    check(SERVER.is_file(), "存在 scripts/tools/serve_ble_monitor.py", failures)
    if failures:
        print(f"{TAG} 结果: {len(failures)} 失败")
        return 1

    html = HTML.read_text(encoding="utf-8")
    server = SERVER.read_text(encoding="utf-8")
    op_doc = OP_DOC.read_text(encoding="utf-8")
    tests_readme = TESTS_README.read_text(encoding="utf-8")
    quality_gate = QUALITY_GATE.read_text(encoding="utf-8")
    op_gate = OP_GATE.read_text(encoding="utf-8")

    required_html_tokens = [
        ("navigator.bluetooth.requestDevice", "支持首次授权选择 BikePower 设备"),
        ("navigator.bluetooth.getDevices", "支持已授权设备自动重连"),
        ("setInterval(checkAnomalies", "周期检测通知中断等异常"),
        ("ANOMALY_LIMITS", "集中定义异常阈值"),
        ("parseCyclingPower", "解析 CPS 功率通知"),
        ("parseHeartRate", "解析 HRS 心率通知"),
        ("parseCsc", "解析 CSC 踏频通知"),
        ("parseIndoorBike", "解析 FTMS Indoor Bike 通知"),
        ("result.power", "FTMS 解析按当前固件变长数据提取功率"),
        ("result.cadence", "FTMS 解析可同步踏频字段"),
        ("flags & 0x0040", "FTMS 仅在 Instantaneous Power Present 时解析功率"),
        ("parseRsc", "解析 RSC 跑步通知"),
        ("distanceDm / 10", "RSC 累计距离按 0.1m 单位换算"),
        ("recordAnomaly", "异常统一计数和日志"),
        ("downloadReport", "支持导出监控报告"),
        ("00001818-0000-1000-8000-00805f9b34fb", "包含 Cycling Power Service UUID"),
        ("00001814-0000-1000-8000-00805f9b34fb", "包含 Running Speed and Cadence Service UUID"),
        ("00001826-0000-1000-8000-00805f9b34fb", "包含 Fitness Machine Service UUID"),
        ("0000180d-0000-1000-8000-00805f9b34fb", "包含 Heart Rate Service UUID"),
        ("0000180a-0000-1000-8000-00805f9b34fb", "包含 Device Information Service UUID"),
    ]
    for token, message in required_html_tokens:
        check(token in html, message, failures)

    check("ThreadingHTTPServer" in server and "tools/web" in server,
          "本地服务脚本从 tools/web 提供页面", failures)
    check("Cache-Control" in server and "no-store" in server,
          "本地服务禁止缓存旧监控页面", failures)
    check("ble-monitor.html" in server and "127.0.0.1" in server,
          "本地服务默认指向 localhost 页面", failures)

    scripts = re.findall(r"<script>(.*?)</script>", html, flags=re.S)
    check(len(scripts) == 1, "监控页包含单个内联脚本，便于静态语法检查", failures)
    node = shutil.which("node")
    if node and scripts:
        with tempfile.NamedTemporaryFile("w", suffix=".js", encoding="utf-8", delete=False) as tmp:
            tmp.write(scripts[0])
            tmp_path = tmp.name
        try:
            result = subprocess.run([node, "--check", tmp_path], capture_output=True, text=True, timeout=15)
            check(result.returncode == 0, "监控页内联 JS 语法正确", failures)
            if result.returncode != 0:
                print((result.stderr or result.stdout).strip())
        finally:
            Path(tmp_path).unlink(missing_ok=True)
    else:
        check(False, "未找到 node，无法检查监控页内联 JS 语法", failures)

    check("tests/check_ble_monitor.py" in quality_gate,
          "quality_gate.py 聚合 BLE 监控静态门禁", failures)
    check("tests/check_ble_monitor.py" in op_gate,
          "operation_gate.py 聚合 BLE 监控静态门禁", failures)
    check("OP-BLE-003" in op_doc and "ble-monitor.html" in op_doc,
          "操作覆盖矩阵记录 BLE 监控客户端门禁", failures)
    check("serve_ble_monitor.py" in tests_readme and "ble-monitor.html" in tests_readme,
          "tests/README.md 记录 BLE 监控客户端用法", failures)

    if failures:
        print(f"{TAG} 结果: {len(failures)} 失败")
        return 1
    print(f"{TAG} PASS: BLE 监控客户端门禁通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
