#!/usr/bin/env python3
"""BikePower C 操作门禁聚合入口。

把用户操作覆盖矩阵中的自动化脚本串成可重复执行的门禁，并输出 Markdown 报告。
quick：开发期快速检查；full：提交前无设备完整门禁；device：真机门禁。
"""
import argparse
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPORT_DIR = ROOT / "tests" / "reports"

IDF_CANDIDATES = [
    os.environ.get("IDF_PATH"),
    str(Path.home() / "esp" / "esp-idf-v5.5.4"),
    str((ROOT / ".." / "esp32-power" / "esp-idf").resolve()),
    "/home/mengen1014/work/esp32-power/esp-idf",
]


class Gate:
    def __init__(self, level, report):
        self.level = level
        self.report = report
        self.rows = []
        self.failures = []

    def record(self, name, command, rc, output, required=True):
        status = "PASS" if rc == 0 else "FAIL" if required else "SKIP"
        detail = tail(output)
        self.rows.append((datetime.now().strftime("%H:%M:%S"), status, name, command, detail))
        print(f"[operation-gate] {status} {name}")
        if detail:
            print(f"[operation-gate]   {detail}")
        if rc != 0 and required:
            self.failures.append(f"{name}: rc={rc}")

    def note_skip(self, name, reason):
        self.rows.append((datetime.now().strftime("%H:%M:%S"), "SKIP", name, "", reason))
        print(f"[operation-gate] SKIP {name}: {reason}")

    def write_report(self):
        self.report.parent.mkdir(parents=True, exist_ok=True)
        lines = [
            "# BikePower C 操作门禁报告",
            "",
            f"- 等级：{self.level}",
            f"- 时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
            f"- 结果：{'失败' if self.failures else '通过'}",
            "",
            "## 明细",
            "",
            "| 时间 | 状态 | 项目 | 命令 | 摘要 |",
            "|------|------|------|------|------|",
        ]
        for ts, status, name, command, detail in self.rows:
            lines.append(
                f"| {ts} | {status} | {escape(name)} | {escape(command)} | {escape(detail)} |"
            )
        if self.failures:
            lines.extend(["", "## 失败项", ""])
            for failure in self.failures:
                lines.append(f"- {failure}")
        self.report.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"[operation-gate] report: {self.report}")


def escape(text):
    return str(text).replace("|", "\\|").replace("\n", " ")


def tail(text, lines=6):
    text = (text or "").strip()
    if not text:
        return ""
    return " / ".join(text.splitlines()[-lines:])


def run_shell(command, timeout):
    return subprocess.run(
        ["bash", "-lc", command],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
    )


def idf_prefix():
    for path in [p for p in IDF_CANDIDATES if p]:
        export_sh = Path(path) / "export.sh"
        if export_sh.exists():
            return f". {export_sh} >/dev/null 2>&1 && "
    return ""


def run_step(gate, name, command, timeout=120, required=True):
    try:
        result = run_shell(command, timeout)
        gate.record(name, command, result.returncode, result.stdout, required=required)
    except subprocess.TimeoutExpired as exc:
        gate.record(name, command, 124, (exc.stdout or "") + "\nTIMEOUT", required=required)


def quick_steps(gate):
    run_step(gate, "Host 领域层单测", "make -C test run", 120)
    run_step(gate, "Mock 保存回显", "python3 tests/test_portal_config_save.py", 60)
    run_step(gate, "操作覆盖矩阵", "python3 tests/check_operation_coverage.py", 30)
    run_step(gate, "BLE 监控客户端静态门禁", "python3 tests/check_ble_monitor.py", 30)
    run_step(gate, "BLE 广播项监控静态门禁", "python3 tests/check_ble_broadcast_monitor.py", 30)
    run_step(gate, "门户结构", "python3 tests/check_portal_structure.py", 60)
    run_step(gate, "门户 JS", "python3 tests/check_portal_js.py", 60)


def full_steps(gate, build):
    quick_steps(gate)
    run_step(gate, "配置一致性", "python3 tests/check_config_consistency.py", 60)
    run_step(gate, "NVS schema", "python3 tests/check_nvs_schema.py", 60)
    run_step(gate, "板型描述表", "python3 tests/check_board_desc.py", 60)
    run_step(gate, "任务/WDT", "python3 tests/check_tasks_wdt.py", 60)
    run_step(gate, "Release 产物门禁接线", "python3 tests/check_release_gate.py", 30)
    run_step(gate, "Release 产物门禁功能样例", "python3 tests/test_release_artifacts.py", 30)
    run_step(gate, "核心流程耐久", "python3 tests/test_core_flow_endurance.py", 240)
    if build:
        run_step(gate, "ESP-IDF 编译", f"{idf_prefix()}idf.py build", 240)


def device_steps(gate, port, run_portal):
    if port:
        run_step(gate, "串口启动日志", f"python3 tests/serial_capture.py -p {port} -d 12", 30)
    else:
        gate.note_skip("串口启动日志", "未传 --port")
    run_step(gate, "BLE 自测", "python3 tests/ble_selftest.py", 60)
    run_step(gate, "BLE 跑步 profile 自测", "python3 tests/ble_selftest.py --profile running 8", 60, required=False)
    if run_portal:
        run_step(gate, "BLE 广播项数据面监控", "python3 tests/ble_broadcast_monitor.py", 120)
    else:
        gate.note_skip("BLE 广播项数据面监控", "未传 --run-portal；需要设备处于配网态以便 /api/config 切换 broadcast_mask")
    run_step(gate, "BLE 连接事件", "python3 tests/stage3_conn_test.py", 60, required=False)
    gate.note_skip("BLE 浏览器监控", "运行 python3 scripts/tools/serve_ble_monitor.py 后用 Chrome/Edge 打开 ble-monitor.html；Web Bluetooth 首次授权必须人工点击")
    if run_portal:
        run_step(gate, "配网门户 HTTP 自测", "python3 tests/stage5_portal_test.py", 180)
    else:
        gate.note_skip("配网门户 HTTP 自测", "未传 --run-portal；需要设备处于配网态")
    gate.note_skip("人工按钮/LED/Zwift/MyWhoosh", "必须按 docs/dev/05-operation-test-gate.md 人工记录，不能冒充自动通过")


def main():
    parser = argparse.ArgumentParser(description="BikePower C 操作门禁聚合入口")
    parser.add_argument("--level", choices=("quick", "full", "device"), default="quick")
    parser.add_argument("--port", default="", help="真机串口，例如 /dev/cu.usbmodem101")
    parser.add_argument("--run-portal", action="store_true", help="device 模式下运行配网门户 HTTP 自测")
    parser.add_argument("--build", action="store_true", help="full 模式追加 idf.py build")
    parser.add_argument("--report", default="", help="报告输出路径")
    args = parser.parse_args()

    report = Path(args.report) if args.report else REPORT_DIR / f"operation_gate_{args.level}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.md"
    gate = Gate(args.level, report)

    if args.level == "quick":
        quick_steps(gate)
    elif args.level == "full":
        full_steps(gate, args.build)
    else:
        device_steps(gate, args.port, args.run_portal)

    gate.write_report()
    if gate.failures:
        print(f"[operation-gate] FAIL: {len(gate.failures)} failures")
        return 1
    print("[operation-gate] PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
