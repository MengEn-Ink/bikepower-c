#!/usr/bin/env python3
"""Create and validate BikePower C device acceptance evidence."""
import argparse
import json
import re
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_EVIDENCE_DIR = ROOT / "tests" / "reports"
SCHEMA_VERSION = 1
VALID_BOARDS = ("generic-c3", "m5stamp-c3")
VALID_STATUSES = ("PASS", "FAIL", "NOT_RUN")

ITEM_DEFINITIONS = {
    "serial_boot": ("P0", "auto", "串口启动日志"),
    "ble_cycling": ("P0", "auto", "骑行 BLE 自测"),
    "ble_running": ("P0", "auto", "跑步 BLE 自测"),
    "ble_reconnect": ("P0", "auto", "BLE 连接与重连"),
    "portal_http": ("P0", "auto", "配网门户 HTTP"),
    "broadcast_mask": ("P0", "auto", "广播项数据面"),
    "soak_5min": ("P0", "auto", "5 分钟设备耐久"),
    "button_short_long": ("P1", "manual", "实体按钮短按与长按"),
    "wifi_confirm": ("P1", "manual", "WiFi 二次确认"),
    "led_patterns": ("P1", "manual", "LED 灯效"),
    "portal_mobile": ("P1", "manual", "手机 Captive Portal"),
    "portal_exit": ("P1", "manual", "配网退出与 BLE 连续性"),
    "zwift_cycling": ("P1", "manual", "Zwift 骑行兼容"),
    "running_apps": ("P1", "manual", "跑步 App 兼容"),
}

P0_ITEM_IDS = tuple(
    item_id
    for item_id, (priority, _kind, _title) in ITEM_DEFINITIONS.items()
    if priority == "P0"
)


class AcceptanceError(RuntimeError):
    """Raised when acceptance evidence is invalid or incomplete."""


def now_iso():
    return datetime.now().astimezone().isoformat(timespec="seconds")


def read_firmware_version():
    config = (ROOT / "main" / "bikepower_config.h").read_text(encoding="utf-8")
    match = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', config)
    if not match:
        raise AcceptanceError("无法从 main/bikepower_config.h 读取 FIRMWARE_VERSION")
    return match.group(1)


def current_git_sha():
    try:
        return subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"],
            text=True,
        ).strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        raise AcceptanceError(f"无法读取当前 Git ID: {exc}") from exc


def new_record(firmware_version, git_sha, board, operator, now=None):
    if board not in VALID_BOARDS:
        raise AcceptanceError(f"不支持的板型: {board}")
    if not firmware_version:
        raise AcceptanceError("固件版本不能为空")
    if not git_sha:
        raise AcceptanceError("Git ID 不能为空")
    timestamp = now or now_iso()
    items = {}
    for item_id, (priority, kind, title) in ITEM_DEFINITIONS.items():
        items[item_id] = {
            "title": title,
            "priority": priority,
            "kind": kind,
            "status": "NOT_RUN",
            "checked_at": "",
            "evidence": "",
        }
    return {
        "schema_version": SCHEMA_VERSION,
        "firmware_version": firmware_version,
        "git_sha": git_sha,
        "board": board,
        "operator": operator or "",
        "created_at": timestamp,
        "updated_at": timestamp,
        "items": items,
    }


def set_item(record, item_id, status, evidence, now=None):
    if item_id not in ITEM_DEFINITIONS:
        raise AcceptanceError(f"未知验收项: {item_id}")
    if status not in VALID_STATUSES:
        raise AcceptanceError(f"非法状态: {status}")
    if status in {"PASS", "FAIL"} and not str(evidence).strip():
        raise AcceptanceError(f"{status} 状态必须提供 evidence")

    timestamp = now or now_iso()
    priority, kind, title = ITEM_DEFINITIONS[item_id]
    record.setdefault("items", {})[item_id] = {
        "title": title,
        "priority": priority,
        "kind": kind,
        "status": status,
        "checked_at": timestamp,
        "evidence": str(evidence).strip(),
    }
    record["updated_at"] = timestamp
    return record


def _require_string(record, key):
    value = record.get(key)
    if not isinstance(value, str):
        raise AcceptanceError(f"字段 {key} 必须是字符串")
    return value


def validate_record(
    record,
    expected_version=None,
    expected_git_sha=None,
    expected_board=None,
    strict=False,
):
    if not isinstance(record, dict):
        raise AcceptanceError("证据根节点必须是 JSON 对象")
    if record.get("schema_version") != SCHEMA_VERSION:
        raise AcceptanceError(
            f"不支持的 schema_version: {record.get('schema_version')}"
        )

    firmware_version = _require_string(record, "firmware_version")
    git_sha = _require_string(record, "git_sha")
    board = _require_string(record, "board")
    _require_string(record, "operator")
    _require_string(record, "created_at")
    _require_string(record, "updated_at")

    if board not in VALID_BOARDS:
        raise AcceptanceError(f"不支持的板型: {board}")
    if expected_version and firmware_version != expected_version:
        raise AcceptanceError(
            f"固件版本不匹配: evidence={firmware_version} expected={expected_version}"
        )
    if expected_git_sha and git_sha != expected_git_sha:
        raise AcceptanceError(
            f"Git ID 不匹配: evidence={git_sha} expected={expected_git_sha}"
        )
    if expected_board and board != expected_board:
        raise AcceptanceError(
            f"板型不匹配: evidence={board} expected={expected_board}"
        )

    items = record.get("items")
    if not isinstance(items, dict):
        raise AcceptanceError("字段 items 必须是 JSON 对象")
    missing = sorted(set(ITEM_DEFINITIONS) - set(items))
    unknown = sorted(set(items) - set(ITEM_DEFINITIONS))
    if missing:
        raise AcceptanceError("缺少验收项: " + ", ".join(missing))
    if unknown:
        raise AcceptanceError("包含未知验收项: " + ", ".join(unknown))

    for item_id, (priority, kind, title) in ITEM_DEFINITIONS.items():
        item = items[item_id]
        if not isinstance(item, dict):
            raise AcceptanceError(f"验收项 {item_id} 必须是 JSON 对象")
        if item.get("title") != title:
            raise AcceptanceError(f"验收项 {item_id} 标题不匹配")
        if item.get("priority") != priority:
            raise AcceptanceError(f"验收项 {item_id} 优先级不匹配")
        if item.get("kind") != kind:
            raise AcceptanceError(f"验收项 {item_id} 类型不匹配")
        status = item.get("status")
        if status not in VALID_STATUSES:
            raise AcceptanceError(f"验收项 {item_id} 状态非法: {status}")
        checked_at = item.get("checked_at")
        evidence = item.get("evidence")
        if not isinstance(checked_at, str) or not isinstance(evidence, str):
            raise AcceptanceError(f"验收项 {item_id} 时间和证据必须是字符串")
        if status in {"PASS", "FAIL"} and (not checked_at or not evidence.strip()):
            raise AcceptanceError(f"验收项 {item_id} 缺少执行时间或证据")

    if strict:
        blocked = [
            f"{item_id}={items[item_id]['status']}"
            for item_id in P0_ITEM_IDS
            if items[item_id]["status"] != "PASS"
        ]
        if blocked:
            raise AcceptanceError("P0 验收未通过: " + ", ".join(blocked))
    return record


def summarize_record(record):
    validate_record(record)
    summary = {
        "p0": {"pass": 0, "fail": 0, "not_run": 0},
        "p1": {"pass": 0, "fail": 0, "not_run": 0},
    }
    for item in record["items"].values():
        priority = item["priority"].lower()
        status = item["status"].lower()
        summary[priority][status] += 1
    return summary


def load_record(path):
    path = Path(path)
    try:
        record = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise AcceptanceError(f"证据文件不存在: {path}") from exc
    except json.JSONDecodeError as exc:
        raise AcceptanceError(f"证据文件不是有效 JSON: {path}: {exc}") from exc
    validate_record(record)
    return record


def write_record(path, record):
    path = Path(path)
    validate_record(record)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=path.parent,
            prefix=f".{path.name}.",
            suffix=".tmp",
            delete=False,
        ) as temp:
            json.dump(record, temp, ensure_ascii=False, indent=2)
            temp.write("\n")
            temp_path = Path(temp.name)
        temp_path.replace(path)
    finally:
        if temp_path and temp_path.exists():
            temp_path.unlink()


def format_summary(record):
    summary = summarize_record(record)
    return (
        f"board={record['board']} firmware={record['firmware_version']} "
        f"git={record['git_sha']} "
        f"P0 PASS={summary['p0']['pass']} FAIL={summary['p0']['fail']} "
        f"NOT_RUN={summary['p0']['not_run']} "
        f"P1 PASS={summary['p1']['pass']} FAIL={summary['p1']['fail']} "
        f"NOT_RUN={summary['p1']['not_run']}"
    )


def add_common_arguments(parser, include_defaults=True):
    parser.add_argument("--file", default="")
    parser.add_argument(
        "--firmware-version",
        default=read_firmware_version() if include_defaults else "",
    )
    parser.add_argument(
        "--git-sha",
        default=current_git_sha() if include_defaults else "",
    )


def build_parser():
    parser = argparse.ArgumentParser(
        description="BikePower C 真机验收证据管理工具"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    init_parser = subparsers.add_parser("init", help="初始化当前版本验收证据")
    add_common_arguments(init_parser)
    init_parser.add_argument("--board", choices=VALID_BOARDS, required=True)
    init_parser.add_argument("--operator", default="")
    init_parser.add_argument("--force", action="store_true")

    set_parser = subparsers.add_parser("set", help="更新单个验收项")
    add_common_arguments(set_parser)
    set_parser.add_argument("--item", choices=tuple(ITEM_DEFINITIONS), required=True)
    set_parser.add_argument("--status", choices=VALID_STATUSES, required=True)
    set_parser.add_argument("--evidence", default="")

    validate_parser = subparsers.add_parser("validate", help="校验证据")
    add_common_arguments(validate_parser)
    validate_parser.add_argument("--strict", action="store_true")
    validate_parser.add_argument("--board", choices=VALID_BOARDS, default="")

    summary_parser = subparsers.add_parser("summary", help="输出证据摘要")
    add_common_arguments(summary_parser)
    return parser


def main():
    args = build_parser().parse_args()
    if args.file:
        path = Path(args.file)
    elif args.command == "init":
        path = DEFAULT_EVIDENCE_DIR / f"device-acceptance-{args.board}.json"
    else:
        print(
            "[device-acceptance] FAIL set/validate/summary 必须用 --file 指定板型证据",
            file=sys.stderr,
        )
        return 1
    try:
        if args.command == "init":
            if path.exists() and not args.force:
                raise AcceptanceError(
                    f"证据文件已存在: {path}；确认重建时使用 --force"
                )
            record = new_record(
                args.firmware_version,
                args.git_sha,
                args.board,
                args.operator,
            )
            write_record(path, record)
            print(f"[device-acceptance] initialized: {path}")
            print(f"[device-acceptance] {format_summary(record)}")
            return 0

        record = load_record(path)
        validate_record(
            record,
            expected_version=args.firmware_version,
            expected_git_sha=args.git_sha,
        )
        if args.command == "set":
            set_item(record, args.item, args.status, args.evidence)
            write_record(path, record)
            print(f"[device-acceptance] updated: {args.item}={args.status}")
        elif args.command == "validate":
            validate_record(
                record,
                expected_version=args.firmware_version,
                expected_git_sha=args.git_sha,
                expected_board=args.board or None,
                strict=args.strict,
            )
            print("[device-acceptance] validation passed")
        print(f"[device-acceptance] {format_summary(record)}")
        return 0
    except AcceptanceError as exc:
        print(f"[device-acceptance] FAIL {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
