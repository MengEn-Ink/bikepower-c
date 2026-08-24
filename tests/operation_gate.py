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
TOOLS_DIR = ROOT / "scripts" / "tools"
sys.path.insert(0, str(TOOLS_DIR))

from device_acceptance import (  # noqa: E402
    AcceptanceError,
    current_git_sha,
    load_record,
    new_record,
    read_firmware_version,
    set_item,
    validate_record,
    write_record,
)

IDF_CANDIDATES = [
    os.environ.get("IDF_PATH"),
    str(Path.home() / "esp" / "esp-idf-v5.5.4"),
    str((ROOT / ".." / "esp32-power" / "esp-idf").resolve()),
    "/home/mengen1014/work/esp32-power/esp-idf",
]


class Gate:
    def __init__(self, level, report, evidence_path=None):
        self.level = level
        self.report = report
        self.evidence_path = evidence_path
        self.rows = []
        self.failures = []

    def record(self, name, command, rc, output, required=True, acceptance_item=None):
        status = "PASS" if rc == 0 else "FAIL" if required else "SKIP"
        detail = tail(output)
        self.rows.append((datetime.now().strftime("%H:%M:%S"), status, name, command, detail))
        print(f"[operation-gate] {status} {name}")
        if detail:
            print(f"[operation-gate]   {detail}")
        if self.evidence_path and acceptance_item:
            record_gate_result(
                self.evidence_path,
                acceptance_item,
                rc=rc,
                output=output,
                command=command,
                required=required,
            )
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


def prepare_evidence(path, firmware_version, git_sha, board, operator):
    path = Path(path)
    if path.exists():
        record = load_record(path)
        validate_record(
            record,
            expected_version=firmware_version,
            expected_git_sha=git_sha,
            expected_board=board or None,
        )
        if operator and record["operator"] != operator:
            record["operator"] = operator
            record["updated_at"] = datetime.now().astimezone().isoformat(timespec="seconds")
            write_record(path, record)
        return path
    if not board:
        return None
    record = new_record(firmware_version, git_sha, board, operator)
    write_record(path, record)
    return path


def record_gate_result(path, item_id, rc, output, command, required=True):
    del required
    record = load_record(path)
    status = "PASS" if rc == 0 else "FAIL"
    detail = tail(output) or f"rc={rc}"
    evidence = f"{command}: {detail}"
    set_item(record, item_id, status, evidence)
    write_record(path, record)


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


def run_step(gate, name, command, timeout=120, required=True, acceptance_item=None):
    try:
        result = run_shell(command, timeout)
        gate.record(
            name,
            command,
            result.returncode,
            result.stdout,
            required=required,
            acceptance_item=acceptance_item,
        )
    except subprocess.TimeoutExpired as exc:
        gate.record(
            name,
            command,
            124,
            (exc.stdout or "") + "\nTIMEOUT",
            required=required,
            acceptance_item=acceptance_item,
        )


def quick_steps(gate):
    run_step(gate, "Host 领域层单测", "make -C test run", 120)
    run_step(gate, "BLE 协议固定向量", "python3 tests/test_ble_protocol_vectors.py", 60)
    run_step(gate, "Mock 保存回显", "python3 tests/test_portal_config_save.py", 60)
    run_step(gate, "操作覆盖矩阵", "python3 tests/check_operation_coverage.py", 30)
    run_step(gate, "BLE 监控客户端静态门禁", "python3 tests/check_ble_monitor.py", 30)
    run_step(gate, "BLE 广播项监控静态门禁", "python3 tests/check_ble_broadcast_monitor.py", 30)
    run_step(gate, "BLE 重连脚本静态门禁", "python3 tests/check_ble_reconnect.py", 30)
    run_step(gate, "WiFi 完成事件可靠性", "python3 tests/check_wifi_done_reliability.py", 30)
    run_step(gate, "FTMS 控制事件顺序", "python3 tests/check_control_event_reliability.py", 30)
    run_step(gate, "构建目录隔离", "python3 tests/check_build_isolation.py", 30)
    run_step(gate, "项目结构清理", "python3 tests/check_project_hygiene.py", 30)
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
        run_step(
            gate,
            "串口启动日志",
            f"python3 tests/serial_capture.py -p {port} -d 12 --check-boot",
            30,
            acceptance_item="serial_boot",
        )
    else:
        gate.note_skip("串口启动日志", "未传 --port")
    run_step(
        gate,
        "BLE 自测",
        "python3 tests/ble_selftest.py",
        60,
        acceptance_item="ble_cycling",
    )
    run_step(
        gate,
        "BLE 跑步 profile 自测",
        "python3 tests/ble_selftest.py --profile running --control 12",
        90,
        acceptance_item="ble_running",
    )
    if run_portal:
        run_step(
            gate,
            "BLE 广播项数据面监控",
            "python3 tests/ble_broadcast_monitor.py",
            120,
            acceptance_item="broadcast_mask",
        )
    else:
        gate.note_skip("BLE 广播项数据面监控", "未传 --run-portal；需要设备处于配网态以便 /api/config 切换 broadcast_mask")
    run_step(
        gate,
        "BLE 连接事件",
        "python3 tests/stage3_conn_test.py",
        60,
        acceptance_item="ble_reconnect",
    )
    gate.note_skip("BLE 浏览器监控", "运行 python3 scripts/tools/serve_ble_monitor.py 后用 Chrome/Edge 打开 ble-monitor.html；Web Bluetooth 首次授权必须人工点击")
    if run_portal:
        run_step(
            gate,
            "配网门户 HTTP 自测",
            "python3 tests/stage5_portal_test.py",
            180,
            acceptance_item="portal_http",
        )
    else:
        gate.note_skip("配网门户 HTTP 自测", "未传 --run-portal；需要设备处于配网态")
    gate.note_skip("人工按钮/LED/Zwift/MyWhoosh", "必须按 docs/dev/04-operation-test-gate.md 人工记录，不能冒充自动通过")


def main():
    parser = argparse.ArgumentParser(description="BikePower C 操作门禁聚合入口")
    parser.add_argument("--level", choices=("quick", "full", "device"), default="quick")
    parser.add_argument("--port", default="", help="真机串口，例如 /dev/cu.usbmodem101")
    parser.add_argument("--run-portal", action="store_true", help="device 模式下运行配网门户 HTTP 自测")
    parser.add_argument("--build", action="store_true", help="full 模式追加 idf.py build")
    parser.add_argument("--report", default="", help="报告输出路径")
    parser.add_argument("--board", choices=("generic-c3", "m5stamp-c3"), default="",
                        help="device 模式下新建证据时使用的板型")
    parser.add_argument("--operator", default="", help="device 模式下记录验收执行人")
    parser.add_argument("--evidence", default="",
                        help="device 模式下验收证据 JSON 路径；默认按板型写入 tests/reports/")
    args = parser.parse_args()

    report = Path(args.report) if args.report else REPORT_DIR / f"operation_gate_{args.level}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.md"
    evidence_path = None
    if args.level == "device":
        evidence_arg = args.evidence
        if not evidence_arg and args.board:
            evidence_arg = str(
                REPORT_DIR / f"device-acceptance-{args.board}.json"
            )
        elif not evidence_arg:
            evidence_arg = ""
        try:
            if evidence_arg:
                evidence_path = prepare_evidence(
                    evidence_arg,
                    firmware_version=read_firmware_version(),
                    git_sha=current_git_sha(),
                    board=args.board,
                    operator=args.operator,
                )
        except AcceptanceError as exc:
            print(f"[operation-gate] FAIL 无法准备验收证据: {exc}")
            return 1
        if evidence_path:
            print(f"[operation-gate] evidence: {evidence_path}")
        else:
            print("[operation-gate] evidence disabled: 文件不存在且未传 --board")
    gate = Gate(args.level, report, evidence_path=evidence_path)

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
