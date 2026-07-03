#!/usr/bin/env python3
"""生成固件随附测试报告与用户文档副本。"""
import argparse
import hashlib
import shutil
import subprocess
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def read_text(path):
    return path.read_text(encoding="utf-8")


def extract_define(src, name):
    for line in src.splitlines():
        parts = line.strip().split(maxsplit=2)
        if len(parts) >= 3 and parts[0] == "#define" and parts[1] == name:
            value = parts[2].split("//", 1)[0].strip()
            if value.startswith('"') and '"' in value[1:]:
                return value.split('"', 2)[1]
            return value.split()[0] if value else ""
    return ""


def git_output(args, default=""):
    try:
        return subprocess.check_output(["git", "-C", str(ROOT)] + args, text=True).strip()
    except Exception:
        return default


def sha256_file(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def collect_artifacts(release_dir, git_sha):
    items = []
    for path in sorted(release_dir.glob("*")):
        if not path.is_file() or path.name in {"manifest.txt", "test-report.md", "user-manual.md"}:
            continue
        if path.name.startswith("bikepower-c-") and f"-g{git_sha}.bin" not in path.name:
            continue
        if path.name.startswith("bikepower-c-") and path.name.endswith(".bin.sha256") and f"-g{git_sha}.bin.sha256" not in path.name:
            continue
        if path.name.startswith("bikepower-c-"):
            items.append(path)
    return items


def write_test_report(release_dir, version, stable, fix, git_sha, test_summary, notes):
    report = release_dir / "test-report.md"
    artifacts = collect_artifacts(release_dir, git_sha)
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    status_lines = git_output(["status", "--short"]).splitlines()
    if status_lines:
        status_text = "<br>".join(f"`{line}`" for line in status_lines)
    else:
        status_text = "clean"

    lines = [
        "# BikePower C 固件测试报告",
        "",
        f"- 生成时间：{now}",
        f"- 固件版本：v{version}",
        f"- 稳定版：{stable}",
        f"- 修复版：{fix}",
        f"- Git ID：{git_sha}",
        f"- 工作区状态：{status_text}",
        "",
        "## 一、随附产物",
        "",
        "| 文件 | 大小 | SHA256 |",
        "|------|------|--------|",
    ]
    for path in artifacts:
        size = path.stat().st_size
        digest = sha256_file(path)
        lines.append(f"| `{path.name}` | {size} bytes | `{digest}` |")
    lines.extend([
        "",
        "## 二、测试结论",
        "",
        test_summary.strip() or "未提供额外测试摘要；请以本报告生成前实际执行的门禁记录为准。",
        "",
        "## 三、最低验证要求",
        "",
        "- 编译：`idf.py build` 或 `scripts/build_all.sh` 必须 0 error。",
        "- Host 单测：`make -C test && ./test/test_domain` 必须全绿。",
        "- 静态门禁：`check_config_consistency.py`、`check_portal_js.py`、`check_portal_structure.py`、`check_nvs_schema.py`、`check_tasks_wdt.py`、`check_board_desc.py` 必须全绿。",
        "- Mock 配网页保存：`python3 tests/test_portal_config_save.py` 必须通过。",
        "- 真机验证：烧录后至少确认启动日志无 panic/WDT/Backtrace，BLE 自测通过；涉及配网时补跑 `stage5_portal_test.py`。",
        "- 人工项：物理按钮和 LED 灯效必须由人工确认，未执行时在报告中明确标记。",
        "",
        "## 四、补充说明",
        "",
        notes.strip() or "无。",
        "",
    ])
    report.write_text("\n".join(lines), encoding="utf-8")
    return report


def write_user_manual(release_dir):
    out = release_dir / "user-manual.md"
    images_src = ROOT / "docs" / "images"
    images_dst = release_dir / "images"
    if images_src.exists():
        if images_dst.exists():
            shutil.rmtree(images_dst)
        shutil.copytree(images_src, images_dst)

    def manual_part(path):
        return read_text(path).replace("../images/", "images/")

    parts = [
        "# BikePower C 用户随附文档",
        "",
        "> 本文件随固件产物一起生成，便于离线分发；内容来源于 README 和 docs/user/。",
        "",
        manual_part(ROOT / "docs" / "user" / "01-quickstart.md"),
        "",
        "---",
        "",
        manual_part(ROOT / "docs" / "user" / "02-user-manual.md"),
        "",
        "---",
        "",
        manual_part(ROOT / "docs" / "user" / "03-hardware.md"),
        "",
    ]
    out.write_text("\n".join(parts), encoding="utf-8")
    return out


def main():
    parser = argparse.ArgumentParser(description="生成 release 随附测试报告和用户文档")
    parser.add_argument("--release-dir", default=str(ROOT / "build" / "release"))
    parser.add_argument("--test-summary", default="")
    parser.add_argument("--notes", default="")
    args = parser.parse_args()

    release_dir = Path(args.release_dir).resolve()
    release_dir.mkdir(parents=True, exist_ok=True)

    config = read_text(ROOT / "main" / "bikepower_config.h")
    version = extract_define(config, "FIRMWARE_VERSION")
    stable = extract_define(config, "FIRMWARE_STABLE_VERSION")
    fix = extract_define(config, "FIRMWARE_FIX_VERSION")
    git_sha = git_output(["rev-parse", "--short", "HEAD"], "nogit")

    test_report = write_test_report(release_dir, version, stable, fix, git_sha, args.test_summary, args.notes)
    user_manual = write_user_manual(release_dir)

    print(f"生成测试报告: {test_report}")
    print(f"生成用户文档: {user_manual}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
