#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BikePower C 发行流水线：一条命令完成构建 → 合并 → 证据 → 文档 → zip → manifest。

边界（保持人工确认点，不越权）：
  - 不打 git tag、不 push、不创建 GitHub Release；结束时只打印建议命令。
  - 不烧录设备、不碰串口/BLE。
  - 不伪造证据：证据缺失、版本不匹配、P0 FAIL 一律失败。

产物目录按版本隔离：build/release/<version>/
  <version>/
    generic-c3-v<version>.bin          合并镜像（bootloader@0x0 + partition@0x8000 + app@0x10000）
    generic-c3-v<version>.bin.sha256
    manifest.txt
    test-report.md
    core-flow-endurance.md
    user-manual.md
    images/
    evidence/device-acceptance-<board>.json
    test-summary.md（可选输入）  release-notes.md（可选输入 / Release 说明）
    bikepower-c-v<version>.zip
    bikepower-c-v<version>.zip.sha256
    release-state.json（阶段状态）

用法：
  python scripts/tools/release_build.py --version 4.3.3 --board generic
  python scripts/tools/release_build.py --version 4.3.3 --dry-run
  python scripts/tools/release_build.py --version 4.3.3 --skip-build --evidence <json>
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time
import zipfile
from datetime import datetime, timezone, timedelta
from pathlib import Path
from typing import List, Optional, Sequence

ROOT = Path(__file__).resolve().parents[2]
CST = timezone(timedelta(hours=8))
GENERATED_BY_DOC = "scripts/tools/release_build.py"

BOARD_DIR_NAME = {"generic": "generic-c3", "m5stamp": "m5stamp-c3"}
BOARD_LABEL = {"generic": "generic-c3 (release)", "m5stamp": "m5stamp-c3 (compile-only, NOT published)"}

# 生产源码输入（这些路径 dirty 时拒绝发版，除非 --allow-dirty-source）
SOURCE_PATHS = ("main", "components", "CMakeLists.txt", "partitions.csv",
                "sdkconfig.defaults", "dependencies.lock")

DEFAULT_TEST_SUMMARY = """**本版发版门禁结论**

| 门禁项 | 结果 | 证据 |
|---|---|---|
| Host 领域单测 | 见 tests/reports/release-full/run-report.md | 同左 |
| 虚拟板完整 BLE 链路 | 见 tests/release-virtual/virtual-e2e-report.md | 同左 |
| 浏览器 E2E | 见 tests/reports/release-full/browser-e2e-syschrome.log | 同左 |
| 真机验收 | 见 evidence/device-acceptance-*.json | 同左 |
"""


def now_iso() -> str:
    return datetime.now(CST).isoformat(timespec="seconds")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def git(*args: str) -> str:
    try:
        return subprocess.check_output(["git", "-C", str(ROOT)] + list(args),
                                       text=True, encoding="utf-8").strip()
    except subprocess.CalledProcessError:
        return ""


def read_firmware_version() -> str:
    src = (ROOT / "main" / "bikepower_config.h").read_text(encoding="utf-8")
    m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', src)
    if not m:
        raise SystemExit("[release] 无法从 main/bikepower_config.h 读取 FIRMWARE_VERSION")
    return m.group(1)


def dirty_source_files() -> List[str]:
    out = git("status", "--porcelain")
    dirty: List[str] = []
    for line in out.splitlines():
        if len(line) < 4:
            continue
        path = line[3:].strip().strip('"')
        for prefix in SOURCE_PATHS:
            if path == prefix or path.startswith(prefix + "/"):
                dirty.append(path)
                break
    return dirty


def idf_python() -> Path:
    sys.path.insert(0, str(Path(__file__).parent))
    from dev_workflow import Context, default_repo_root, detect_idf, detect_idf_python
    ctx = Context(repo=default_repo_root())
    idf, _ = detect_idf(ctx)
    py, _ = detect_idf_python(ctx, idf)
    if not py:
        raise SystemExit("[release] 找不到 IDF Python；先运行 scripts/dev_workflow.ps1 doctor")
    return py


def stage(state_path: Path, name: str, status: str, detail: str = "") -> None:
    record = {}
    if state_path.is_file():
        try:
            record = json.loads(state_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            record = {}
    record.setdefault("stages", {})[name] = {
        "status": status, "detail": detail, "at": now_iso(),
    }
    record["updated_at"] = now_iso()
    state_path.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n",
                          encoding="utf-8", newline="\n")
    print(f"  [{status}] {name}" + (f"：{detail}" if detail else ""))


def build(board: str, state_path: Path) -> Path:
    from dev_workflow import Context, cmd_build, default_repo_root
    ctx = Context(repo=default_repo_root())
    stage(state_path, "build", "RUNNING", "dev_workflow production 增量构建")
    rc = cmd_build(ctx, "production", board, dry_run=False)
    if rc != 0:
        stage(state_path, "build", "FAIL", f"exit={rc}")
        raise SystemExit(f"[release] 构建失败 exit={rc}")
    cache = ctx.mode_dir("production")
    for rel in ("bootloader/bootloader.bin", "partition_table/partition-table.bin",
                "bikepower-c.bin"):
        if not (cache / rel).is_file():
            stage(state_path, "build", "FAIL", f"缺少构建产物 {rel}")
            raise SystemExit(f"[release] 缺少构建产物: {cache / rel}")
    stage(state_path, "build", "PASS", f"cache={cache}")
    return cache


def merge_image(cache: Path, out_bin: Path, state_path: Path, python_exe: Path) -> None:
    argv = [
        str(python_exe), "-m", "esptool", "--chip", "esp32c3", "merge_bin",
        "-o", str(out_bin),
        "--flash_mode", "dio", "--flash_freq", "80m", "--flash_size", "4MB",
        "0x0", str(cache / "bootloader" / "bootloader.bin"),
        "0x8000", str(cache / "partition_table" / "partition-table.bin"),
        "0x10000", str(cache / "bikepower-c.bin"),
    ]
    proc = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8")
    if proc.returncode != 0:
        stage(state_path, "merge", "FAIL", proc.stderr.strip()[-300:])
        raise SystemExit("[release] merge_bin 失败")
    size = out_bin.stat().st_size
    stage(state_path, "merge", "PASS", f"{size} bytes sha256={sha256_file(out_bin)}")


def resolve_evidence(out_dir: Path, board: str, source: Optional[Path],
                     state_path: Path) -> dict:
    board_dir = BOARD_DIR_NAME[board]
    target = out_dir / "evidence" / f"device-acceptance-{board_dir}.json"
    if source:
        if not source.is_file():
            stage(state_path, "evidence", "FAIL", f"证据源不存在 {source}")
            raise SystemExit(f"[release] 证据源不存在: {source}")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    if not target.is_file():
        stage(state_path, "evidence", "FAIL", f"缺少证据 {target}；用 --evidence 指定")
        raise SystemExit(
            f"[release] 缺少真机证据: {target}\n"
            "  用 --evidence <json> 指定本轮真机验收证据；不得用旧版本证据替代。")
    record = json.loads(target.read_text(encoding="utf-8"))
    stage(state_path, "evidence", "internal", f"读取 {target.relative_to(ROOT)}")
    return record


def validate_evidence(record: dict, version: str, state_path: Path) -> None:
    if str(record.get("firmware_version", "")) != version:
        stage(state_path, "evidence-validate", "FAIL",
              f"证据版本 {record.get('firmware_version')} != {version}")
        raise SystemExit(f"[release] 证据版本不匹配：{record.get('firmware_version')} != {version}")
    items = record.get("items", {})
    p0 = {k: v for k, v in items.items() if v.get("priority") == "P0"}
    failed = [k for k, v in p0.items() if v.get("status") == "FAIL"]
    not_run = [k for k, v in p0.items() if v.get("status") == "NOT_RUN"]
    missing = [k for k, v in p0.items() if v.get("status") not in ("PASS", "FAIL", "NOT_RUN")]
    if failed:
        stage(state_path, "evidence-validate", "FAIL", f"P0 FAIL: {failed}")
        raise SystemExit(f"[release] P0 证据存在 FAIL：{failed}")
    if missing:
        stage(state_path, "evidence-validate", "FAIL", f"P0 状态非法: {missing}")
        raise SystemExit(f"[release] P0 证据状态非法：{missing}")
    detail = f"P0 PASS {len(p0) - len(failed) - len(not_run)}/{len(p0)}"
    if not_run:
        detail += f"，NOT_RUN: {not_run}（须在报告中写明原因）"
    stage(state_path, "evidence-validate", "PASS", detail)


def copy_endurance(out_dir: Path, source: Optional[Path], state_path: Path) -> None:
    default = ROOT / "tests" / "reports" / "release-full" / "core-flow-600s.md"
    src = source or (default if default.is_file() else None)
    if not src or not src.is_file():
        stage(state_path, "endurance", "FAIL", "缺少耐久报告")
        raise SystemExit(
            "[release] 缺少耐久报告：用 --endurance <md> 指定，或先跑 "
            "tests/test_core_flow_endurance.py --duration 600")
    shutil.copy2(src, out_dir / "core-flow-endurance.md")
    stage(state_path, "endurance", "PASS", src.name)


def generate_docs(out_dir: Path, test_summary: Optional[Path], notes: Optional[Path],
                  state_path: Path) -> None:
    summary = test_summary.read_text(encoding="utf-8") if test_summary else DEFAULT_TEST_SUMMARY
    notes_text = notes.read_text(encoding="utf-8") if notes else (
        "本发行包由 release_build.py 统一生成；未执行项以 test-report.md 内的 NOT_RUN 记录为准。")
    argv = [sys.executable, str(ROOT / "scripts" / "tools" / "generate_release_docs.py"),
            "--release-dir", str(out_dir), "--test-summary", summary, "--notes", notes_text]
    proc = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8")
    if proc.returncode != 0:
        stage(state_path, "docs", "FAIL", proc.stderr.strip()[-300:])
        raise SystemExit("[release] 随附文档生成失败")
    for name in ("test-report.md", "user-manual.md", "images"):
        if not (out_dir / name).exists():
            stage(state_path, "docs", "FAIL", f"缺少 {name}")
            raise SystemExit(f"[release] 文档产物缺失: {name}")
    (out_dir / "release-notes.md").write_text(notes_text, encoding="utf-8", newline="\n")
    stage(state_path, "docs", "PASS", "test-report.md / user-manual.md / images / release-notes.md")


def write_manifest(out_dir: Path, version: str, board: str, state_path: Path) -> Path:
    board_dir = BOARD_DIR_NAME[board]
    bins = sorted(out_dir.glob(f"{board_dir}-v{version}.bin"))
    lines = [
        "# BikePower C release manifest",
        f"version: v{version}",
        f"git_commit: {git('rev-parse', '--short', 'HEAD')}",
        f"built: {now_iso()}",
        f"generated_by: {GENERATED_BY_DOC}",
        f"boards: {BOARD_LABEL[board]}",
        "",
    ]
    for b in bins:
        digest = sha256_file(b)
        lines.append(f"{b.name}  {b.stat().st_size}  sha256={digest}")
        b.with_suffix(".bin.sha256").write_text(f"{digest}  {b.name}\n",
                                                encoding="utf-8", newline="\n")
    lines += [
        "",
        "notes:",
        "- 合并镜像布局：bootloader@0x0 + partition@0x8000 + app@0x10000，适用于全片刷写。",
        "- 已有用户升级如需保留 NVS 配置与绑定，请仅刷写 app（0x10000）。",
        "- 未执行验证项以 test-report.md 的 NOT_RUN 记录为准，不计入通过。",
    ]
    manifest = out_dir / "manifest.txt"
    manifest.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    stage(state_path, "manifest", "PASS", manifest.name)
    return manifest


def make_zip(out_dir: Path, version: str, state_path: Path) -> Path:
    zip_path = out_dir / f"bikepower-c-v{version}.zip"
    if zip_path.exists():
        zip_path.unlink()
    members = sorted([p for p in out_dir.iterdir()
                      if p.is_file() and (p.suffix in (".bin", ".md", ".txt") or
                                          p.name.endswith(".bin.sha256"))])
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for p in members:
            zf.write(p, arcname=p.name)
        images = out_dir / "images"
        if images.is_dir():
            for p in sorted(images.rglob("*")):
                if p.is_file():
                    zf.write(p, arcname=str(p.relative_to(out_dir)).replace("\\", "/"))
    digest = sha256_file(zip_path)
    (out_dir / f"bikepower-c-v{version}.zip.sha256").write_text(
        f"{digest}  {zip_path.name}\n", encoding="utf-8", newline="\n")
    stage(state_path, "zip", "PASS", f"{zip_path.name} {zip_path.stat().st_size} bytes")
    return zip_path


def print_next_steps(out_dir: Path, version: str, board: str) -> None:
    board_dir = BOARD_DIR_NAME[board]
    rel = out_dir.relative_to(ROOT).as_posix()
    print("\n[release] 产物就绪（未 tag、未 push、未发布）：")
    print(f"  目录: {rel}/")
    for p in sorted(out_dir.iterdir()):
        if p.is_file():
            print(f"    - {p.name}  {p.stat().st_size} bytes")
    print("\n[release] 人工确认后执行：")
    print(f"  git tag -a v{version} -m 'BikePower C v{version}'")
    print(f"  git push origin v{version}")
    print("  gh release create v{0} --title 'BikePower C v{0}' --notes-file '{1}/release-notes.md' \\"
          .format(version, rel))
    print(f"    '{rel}/{board_dir}-v{version}.bin' "
          f"'{rel}/{board_dir}-v{version}.bin.sha256' \\")
    print(f"    '{rel}/bikepower-c-v{version}.zip' "
          f"'{rel}/bikepower-c-v{version}.zip.sha256'")


def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(prog="release_build",
                                description="生成 BikePower C 发行产物（不 tag / 不发布）")
    p.add_argument("--version", required=True, help="发行版本，如 4.3.3")
    p.add_argument("--board", default="generic", choices=sorted(BOARD_DIR_NAME))
    p.add_argument("--release-root", default=str(ROOT / "build" / "release"))
    p.add_argument("--evidence", help="真机验收证据 JSON 源路径")
    p.add_argument("--endurance", help="耐久报告 md 源路径")
    p.add_argument("--test-summary", help="测试摘要 md（默认内置模板）")
    p.add_argument("--notes", help="Release 说明 md（默认内置模板）")
    p.add_argument("--skip-build", action="store_true")
    p.add_argument("--allow-dirty-source", action="store_true")
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args(argv)

    version = args.version.lstrip("v")
    out_dir = Path(args.release_root).resolve() / version
    state_path = out_dir / "release-state.json"
    started = time.time()

    print(f"[release] BikePower C v{version} board={args.board}")
    print(f"[release] 产物目录: {out_dir}")

    fw_version = read_firmware_version()
    if fw_version != version:
        raise SystemExit(f"[release] 源码 FIRMWARE_VERSION={fw_version} 与 --version {version} 不一致")

    dirty = dirty_source_files()
    if dirty and not args.allow_dirty_source:
        print("[release] 生产源码存在未提交改动，拒绝发版：")
        for d in dirty:
            print(f"    {d}")
        raise SystemExit("[release] 先提交生产源码，或用 --allow-dirty-source 显式承担风险")

    if args.dry_run:
        print("[release][dry-run] 计划：")
        print("  1) freeze 检查 → 2) production 增量构建 → 3) merge_bin")
        print("  4) 证据校验（版本一致 + P0 无 FAIL）→ 5) 随附文档")
        print("  6) manifest + zip + sha256 → 7) 打印 tag/release 命令")
        print(f"[release][dry-run] 证据需存在: {out_dir}/evidence/device-acceptance-{BOARD_DIR_NAME[args.board]}.json")
        return 0

    out_dir.mkdir(parents=True, exist_ok=True)
    state_path.write_text(json.dumps({
        "version": version, "board": args.board, "started_at": now_iso(),
        "git_head": git("rev-parse", "--short", "HEAD"),
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    stage(state_path, "freeze", "PASS", f"FIRMWARE_VERSION={fw_version}，生产源码无未提交改动")

    py = idf_python()
    cache = (ROOT / "build" / "devcache" / "production")
    if not args.skip_build:
        cache = build(args.board, state_path)
    else:
        stage(state_path, "build", "SKIP", "复用现有 production 缓存")

    board_dir = BOARD_DIR_NAME[args.board]
    out_bin = out_dir / f"{board_dir}-v{version}.bin"
    merge_image(cache, out_bin, state_path, py)

    evidence_src = Path(args.evidence).resolve() if args.evidence else None
    record = resolve_evidence(out_dir, args.board, evidence_src, state_path)
    validate_evidence(record, version, state_path)

    copy_endurance(out_dir, Path(args.endurance).resolve() if args.endurance else None, state_path)
    generate_docs(out_dir,
                  Path(args.test_summary).resolve() if args.test_summary else None,
                  Path(args.notes).resolve() if args.notes else None,
                  state_path)
    write_manifest(out_dir, version, args.board, state_path)
    zip_path = make_zip(out_dir, version, state_path)

    stage(state_path, "done", "PASS", f"{round(time.time() - started, 1)}s")
    print_next_steps(out_dir, version, args.board)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
