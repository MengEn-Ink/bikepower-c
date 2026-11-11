#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BikePower C 发行流水线：一条命令完成构建 → 合并 → 证据 → 文档 → zip → manifest。

边界（保持人工确认点，不越权）：
  - 不打 git tag、不 push、不创建 GitHub Release；结束时只打印建议命令。
  - 不烧录设备、不碰串口/BLE。
  - 不伪造证据：证据缺失、版本不匹配、P0 FAIL 一律失败。

正式产物目录：build/release/<version>/。
候选目录：build/release/candidates/<version>/<board>/。
阶段状态位于 build/release/.state/，不进入发行包。
  <version>/
    generic-c3-v<version>.bin          合并镜像（bootloader@0x0 + partition@0x8000 + app@0x10000）
    generic-c3-v<version>.bin.sha256
    manifest.txt
    test-report.md
    core-flow-endurance.md
    user-manual.md
    images/
    evidence/device-acceptance-<board>.json
    bikepower-c-v<version>.zip
    bikepower-c-v<version>.zip.sha256

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

from device_acceptance import AcceptanceError, new_record, validate_record
from dev_workflow import Context, SOURCE_TOP_FILES

ROOT = Path(__file__).resolve().parents[2]
CST = timezone(timedelta(hours=8))
GENERATED_BY_DOC = "scripts/tools/release_build.py"

BOARD_DIR_NAME = {"generic": "generic-c3", "m5stamp": "m5stamp-c3"}
BOARD_LABEL = {"generic": "generic-c3 (release)", "m5stamp": "m5stamp-c3 (compile-only, NOT published)"}

# 生产源码输入（这些路径 dirty 时拒绝发版，除非 --allow-dirty-source）
SOURCE_PATHS = ("main", "components", "CMakeLists.txt", "partitions.csv",
                *SOURCE_TOP_FILES, "scripts/tools/generate_release_docs.py",
                "scripts/tools/release_build.py", "scripts/tools/dev_workflow.py")

DEFAULT_TEST_SUMMARY = (
    "本轮设备验证以 evidence/device-acceptance-*.json 为准；"
    "主机、浏览器和构建结果由调用方通过 --test-summary 提供。"
    "未执行项保持 NOT_RUN。\n"
)


def now_iso() -> str:
    return datetime.now(CST).isoformat(timespec="seconds")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def git(*args: str, ctx: Optional[Context] = None) -> str:
    try:
        return subprocess.check_output(["git", "-C", str(ctx.repo if ctx else ROOT)] + list(args),
                                       text=True, encoding="utf-8").strip()
    except subprocess.CalledProcessError:
        return ""


def read_firmware_version(ctx: Optional[Context] = None) -> str:
    src = ((ctx.repo if ctx else ROOT) / "main" / "bikepower_config.h").read_text(encoding="utf-8")
    m = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', src)
    if not m:
        raise SystemExit("[release] 无法从 main/bikepower_config.h 读取 FIRMWARE_VERSION")
    return m.group(1)


def dirty_source_files(ctx: Optional[Context] = None) -> List[str]:
    repo = ctx.repo if ctx else ROOT
    try:
        out = subprocess.check_output(
            ["git", "-C", str(repo), "status", "--porcelain=v1", "-z", "--",
             *SOURCE_PATHS], text=True, encoding="utf-8")
    except subprocess.CalledProcessError as exc:
        raise SystemExit("[release] 无法核对生产输入的 Git 状态") from exc
    entries = out.split("\0") if "\0" in out else out.splitlines()
    paths = []
    index = 0
    while index < len(entries):
        line = entries[index]
        index += 1
        if len(line) < 4:
            continue
        paths.append(line[3:])
        if "R" in line[:2] or "C" in line[:2]:
            if index < len(entries):
                paths.append(entries[index])
                index += 1
    return paths


def idf_python(ctx: Optional[Context] = None) -> Path:
    sys.path.insert(0, str(Path(__file__).parent))
    from dev_workflow import Context, default_repo_root, detect_idf, detect_idf_python
    ctx = ctx or Context(repo=default_repo_root())
    idf, _ = detect_idf(ctx)
    py, _ = detect_idf_python(ctx, idf)
    if not py:
        raise SystemExit("[release] 找不到 IDF Python；先运行 scripts/dev_workflow.ps1 doctor")
    return py


def write_text(path: Path, text: str) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(text)


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
    write_text(state_path, json.dumps(record, ensure_ascii=False, indent=2) + "\n")
    print(f"  [{status}] {name}" + (f"：{detail}" if detail else ""))


def build(board: str, state_path: Path, ctx: Optional[Context] = None) -> Path:
    from dev_workflow import Context, cmd_build, default_repo_root
    ctx = ctx or Context(repo=default_repo_root())
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


def validate_cache(ctx: Context, board: str) -> Path:
    from dev_workflow import (
        DevWorkflowError,
        artifact_hashes, detect_idf, detect_tools_root, git_identity,
        load_idf_toolchain, read_state, snapshot_files, source_fingerprint,
        _config_bucket, _merged_defaults, _hash_file,
    )
    cache = ctx.mode_dir("production")
    state = read_state(ctx, "production") or {}
    idf, _ = detect_idf(ctx)
    selected, _ = load_idf_toolchain(idf, detect_tools_root(ctx),
                                     ctx.config.get("target", "esp32c3"))
    bucket = _config_bucket("production", board, _merged_defaults(ctx, board),
                            selected, ctx.config.get("target", "esp32c3"))
    expected = {
        "status": "PASS", "stage": "done", "board": board,
        "git_head": git_identity(ctx)["head"],
        "source_fingerprint": source_fingerprint(ctx)[0],
        "config_bucket": bucket,
    }
    for key, value in expected.items():
        if state.get(key) != value:
            raise SystemExit(f"[release] 缓存 {key} 不匹配，必须重新构建")
    try:
        if state.get("artifact_hashes") != artifact_hashes(cache):
            raise SystemExit("[release] 缓存实际产物 SHA256 不匹配")
        for rel, digest in snapshot_files(ctx).items():
            dest = cache / "source-snapshot" / rel
            if not dest.is_file() or _hash_file(dest) != digest:
                raise SystemExit(f"[release] 缓存源码快照不匹配: {rel}")
        config = cache / "source-snapshot" / "sdkconfig"
        if not config.is_file() or state.get("sdkconfig_hash") != _hash_file(config):
            raise SystemExit("[release] 缓存 sdkconfig 不匹配")
    except (OSError, RuntimeError, DevWorkflowError) as exc:
        raise SystemExit(f"[release] 缓存校验失败: {exc}") from exc
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
    stage(state_path, "evidence", "internal", f"读取 {target}")
    return record


def validate_evidence(record: dict, version: str, state_path: Optional[Path],
                      git_sha: str = "", board: str = "", strict: bool = True) -> None:
    try:
        validate_record(record, expected_version=version, expected_git_sha=git_sha or None,
                        expected_board=BOARD_DIR_NAME[board] if board else None, strict=strict)
    except AcceptanceError as exc:
        if state_path:
            stage(state_path, "evidence-validate", "FAIL", str(exc))
        raise SystemExit(f"[release] 证据校验失败: {exc}") from exc
    if state_path:
        stage(state_path, "evidence-validate", "PASS", "权威验收结构与身份通过")


def copy_endurance(out_dir: Path, source: Optional[Path], state_path: Path,
                    candidate: bool = False) -> None:
    src = source
    if not src and candidate:
        write_text(out_dir / "core-flow-endurance.md", "# 候选构建\n\n本轮耐久未执行（NOT_RUN）。\n")
        stage(state_path, "endurance", "NOT_RUN", "候选未提供本轮耐久报告")
        return
    if not src or not src.is_file():
        stage(state_path, "endurance", "FAIL", "缺少耐久报告")
        raise SystemExit(
            "[release] 缺少耐久报告：用 --endurance <md> 指定，或先跑 "
            "tests/test_core_flow_endurance.py --duration 600")
    shutil.copy2(src, out_dir / "core-flow-endurance.md")
    stage(state_path, "endurance", "PASS", src.name)


def generate_docs(out_dir: Path, test_summary: Optional[Path], notes: Optional[Path],
                  state_path: Path, ctx: Optional[Context] = None) -> None:
    summary = test_summary.read_text(encoding="utf-8") if test_summary else DEFAULT_TEST_SUMMARY
    notes_text = notes.read_text(encoding="utf-8") if notes else (
        "本发行包由 release_build.py 统一生成；未执行项以 test-report.md 内的 NOT_RUN 记录为准。")
    repo = ctx.repo if ctx else ROOT
    argv = [sys.executable, str(repo / "scripts" / "tools" / "generate_release_docs.py"),
            "--release-dir", str(out_dir), "--test-summary", summary, "--notes", notes_text]
    proc = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8")
    if proc.returncode != 0:
        stage(state_path, "docs", "FAIL", proc.stderr.strip()[-300:])
        raise SystemExit("[release] 随附文档生成失败")
    for name in ("test-report.md", "user-manual.md", "images"):
        if not (out_dir / name).exists():
            stage(state_path, "docs", "FAIL", f"缺少 {name}")
            raise SystemExit(f"[release] 文档产物缺失: {name}")
    stage(state_path, "docs", "PASS", "test-report.md / user-manual.md / images")


def write_manifest(out_dir: Path, version: str, board: str, state_path: Path) -> Path:
    board_dir = BOARD_DIR_NAME[board]
    bins = sorted(out_dir.glob(f"{board_dir}-v{version}.bin"))
    for b in bins:
        digest = sha256_file(b)
        write_text(b.with_suffix(".bin.sha256"), f"{digest}  {b.name}\n")
    lines = [f"# BikePower C v{version}，由 {GENERATED_BY_DOC} 生成"]
    for path in sorted(out_dir.rglob("*")):
        if not path.is_file() or path.name == "manifest.txt" or ".zip" in path.name:
            continue
        lines.append(f"{path.relative_to(out_dir).as_posix()}  {path.stat().st_size}  {sha256_file(path)}")
    manifest = out_dir / "manifest.txt"
    write_text(manifest, "\n".join(lines) + "\n")
    stage(state_path, "manifest", "PASS", manifest.name)
    return manifest


def make_zip(out_dir: Path, version: str, state_path: Path) -> Path:
    zip_path = out_dir / f"bikepower-c-v{version}.zip"
    if zip_path.exists():
        zip_path.unlink()
    members = sorted(p for p in out_dir.rglob("*")
                     if p.is_file() and ".zip" not in p.name)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for p in members:
            zf.write(p, arcname=p.relative_to(out_dir).as_posix())
    digest = sha256_file(zip_path)
    sidecar = out_dir / f"bikepower-c-v{version}.zip.sha256"
    write_text(sidecar, f"{digest}  {zip_path.name}\n")
    with (out_dir / "manifest.txt").open("a", encoding="utf-8", newline="\n") as manifest:
        for path in (zip_path, sidecar):
            manifest.write(f"{path.name}  {path.stat().st_size}  {sha256_file(path)}\n")
    stage(state_path, "zip", "PASS", f"{zip_path.name} {zip_path.stat().st_size} bytes")
    return zip_path


def print_next_steps(out_dir: Path, version: str, board: str) -> None:
    board_dir = BOARD_DIR_NAME[board]
    rel = out_dir.as_posix()
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


def main(argv: Optional[Sequence[str]] = None, ctx: Optional[Context] = None) -> int:
    from dev_workflow import ModeLock, git_identity, load_config
    from check_release_artifacts import validate_release_artifacts
    if ctx is None:
        config_path, config = load_config(None, ROOT)
        ctx = Context(repo=ROOT, config_path=config_path, config=config)
    p = argparse.ArgumentParser(prog="release_build",
                                description="生成 BikePower C 发行产物（不 tag / 不发布）")
    p.add_argument("--version", required=True, help="发行版本，如 4.3.3")
    p.add_argument("--board", default="generic", choices=sorted(BOARD_DIR_NAME))
    p.add_argument("--release-root", default=str(ctx.repo / "build" / "release"))
    p.add_argument("--evidence", help="真机验收证据 JSON 源路径")
    p.add_argument("--endurance", help="耐久报告 md 源路径")
    p.add_argument("--test-summary", help="测试摘要 md（默认内置模板）")
    p.add_argument("--notes", help="Release 说明 md（默认内置模板）")
    p.add_argument("--skip-build", action="store_true")
    p.add_argument("--allow-dirty-source", action="store_true")
    p.add_argument("--candidate", action="store_true",
                   help="仅在 candidates 目录生成候选，实物未执行项保持 NOT_RUN")
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args(argv)

    version = args.version.lstrip("v")
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise SystemExit("[release] 版本必须是 x.y.z")
    root = Path(args.release_root).resolve()
    candidate = args.candidate or args.allow_dirty_source
    out_dir = root / "candidates" / version / BOARD_DIR_NAME[args.board] if candidate else root / version
    state_path = root / ".state" / f"{version}-{args.board}-{'candidate' if candidate else 'release'}.json"
    started = time.time()

    print(f"[release] BikePower C v{version} board={args.board}")
    print(f"[release] 产物目录: {out_dir}")

    fw_version = read_firmware_version(ctx)
    if fw_version != version:
        raise SystemExit(f"[release] 源码 FIRMWARE_VERSION={fw_version} 与 --version {version} 不一致")

    dirty = dirty_source_files(ctx)
    if dirty and not candidate:
        print("[release] 生产源码存在未提交改动，拒绝发版：")
        for d in dirty:
            print(f"    {d}")
        raise SystemExit("[release] 正式入口要求生产输入已提交；--candidate 可生成隔离候选")

    gi = git_identity(ctx)
    if not re.fullmatch(r"[0-9a-f]{40}", gi["head"]):
        raise SystemExit("[release] 必须有可核对的源 Git 身份")
    evidence_src = Path(args.evidence).resolve() if args.evidence else None
    if evidence_src:
        record = json.loads(evidence_src.read_text(encoding="utf-8"))
    elif candidate:
        record = new_record(version, gi["head"][:7], BOARD_DIR_NAME[args.board], "候选未验收")
    else:
        raise SystemExit("[release] 正式入口必须提供本轮 --evidence")
    validate_evidence(record, version, None, git_sha=gi["head"][:7],
                      board=args.board, strict=not candidate)
    endurance = Path(args.endurance).resolve() if args.endurance else None
    if not candidate and (not endurance or not endurance.is_file()):
        raise SystemExit("[release] 正式入口必须提供本轮 --endurance")
    if out_dir.exists() and any(out_dir.iterdir()):
        raise SystemExit(f"[release] 产物目录非空，拒绝混用旧包: {out_dir}")

    if args.dry_run:
        print("[release][dry-run] 计划：")
        print("  1) freeze 检查 → 2) production 增量构建 → 3) merge_bin")
        print("  4) 权威证据校验（版本/Git/板型/完整项）→ 5) 随附文档")
        print("  6) manifest + zip + sha256 → 7) 打印 tag/release 命令")
        print(f"[release][dry-run] 证据需存在: {out_dir}/evidence/device-acceptance-{BOARD_DIR_NAME[args.board]}.json")
        return 0

    py = idf_python(ctx)
    if not args.skip_build:
        state_path.parent.mkdir(parents=True, exist_ok=True)
        build(args.board, state_path, ctx)
    lock = ModeLock(ctx.mode_dir("production") / "build.lock")
    lock.acquire(ctx.timeout("lock_wait"))
    try:
        cache = validate_cache(ctx, args.board)
        state_path.parent.mkdir(parents=True, exist_ok=True)
        out_dir.mkdir(parents=True, exist_ok=True)
        write_text(state_path, json.dumps({
            "version": version, "board": args.board, "started_at": now_iso(),
            "git_head": gi["head"], "candidate": candidate,
        }, ensure_ascii=False, indent=2) + "\n")
        stage(state_path, "freeze", "PASS", f"生产输入 dirty={bool(dirty)} candidate={candidate}")
        stage(state_path, "build", "PASS", "缓存身份、配置、快照与三个产物 SHA256 通过")
        out_bin = out_dir / f"{BOARD_DIR_NAME[args.board]}-v{version}.bin"
        merge_image(cache, out_bin, state_path, py)
        target = out_dir / "evidence" / f"device-acceptance-{BOARD_DIR_NAME[args.board]}.json"
        target.parent.mkdir(parents=True, exist_ok=True)
        write_text(target, json.dumps(record, ensure_ascii=False, indent=2) + "\n")
        validate_evidence(record, version, state_path, gi["head"][:7], args.board, not candidate)
        copy_endurance(out_dir, endurance, state_path, candidate=candidate)
        generate_docs(out_dir,
                      Path(args.test_summary).resolve() if args.test_summary else None,
                      Path(args.notes).resolve() if args.notes else None,
                      state_path, ctx)
        write_manifest(out_dir, version, args.board, state_path)
        make_zip(out_dir, version, state_path)
        validate_cache(ctx, args.board)
        validate_release_artifacts(
            out_dir, version, gi["head"][:7], skip_git_check=True,
            require_device_evidence=not candidate, boards=(BOARD_DIR_NAME[args.board],))
        stage(state_path, "done", "PASS", f"权威产物门禁通过，{round(time.time() - started, 1)}s")
        if candidate:
            print(f"[release] 候选包已生成，实物未执行项保持 NOT_RUN: {out_dir}")
        else:
            print_next_steps(out_dir, version, args.board)
        return 0
    finally:
        lock.release()


if __name__ == "__main__":
    raise SystemExit(main())
