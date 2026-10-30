#!/usr/bin/env python3
"""从 GitHub Release 生成 BikePower BLE Monitor / ESP Web Tools 静态站点。

脚本接受最新 GitHub Release 中受支持板型的 merged bin，并在复制到站点前
验证旁文件 SHA256。没有可用 Release 时默认保留 BLE Monitor 并明确标记不可
刷写；正式 Pages 部署使用 ``--require-firmware`` 将该情况升级为失败。

历史版本（回滚）：额外拉取最近 ``--history`` 个正式 Release，同样逐个校验
SHA256 后镜像到 ``firmware/<tag>/``，并生成 ``firmware/releases.json`` 版本
索引。GitHub Release 附件下载地址不返回 CORS 头，ESP Web Tools 无法跨域读取，
所以历史固件必须在部署时镜像为同源静态文件。单个历史版本校验失败只会被
跳过并记录原因，不会阻断最新版本部署。
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent.parent
BOARD_LABELS = {
    "generic-c3": "通用 ESP32-C3",
    "m5stamp-c3": "M5Stamp C3",
}
FIRMWARE_RE = re.compile(
    r"^bikepower-c-(?P<board>generic-c3|m5stamp-c3)-"
    r"v(?P<version>[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?)-"
    r"stable-fix-g(?P<git>[0-9a-fA-F]{7,40})\.bin$"
)
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
REPO_RE = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")


DEFAULT_HISTORY = 6
MAX_HISTORY = 12
NOTES_EXCERPT_MAX = 900


class ReleaseUnavailable(RuntimeError):
    """远端没有可用于网页刷写的完整 Release。"""


class ReleaseValidationError(RuntimeError):
    """Release 存在，但产物完整性或路径校验失败。"""


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def validate_repo(repo):
    if not repo or not REPO_RE.fullmatch(repo):
        raise ReleaseUnavailable("仓库名必须使用 owner/repo 格式")
    return repo


def request_json(url, headers, provider):
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            return None
        raise ReleaseUnavailable(f"{provider} Release API 返回 HTTP {exc.code}") from exc
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as exc:
        raise ReleaseUnavailable(f"{provider} Release API 暂不可用") from exc


def fetch_github_release(repo, token):
    repo = validate_repo(repo)
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "bikepower-web-flash-site",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    release = request_json(
        f"https://api.github.com/repos/{repo}/releases/latest",
        headers,
        "GitHub",
    )
    if release is not None:
        release["_provider"] = "github"
    return release


def fetch_github_release_list(repo, token, limit):
    """最近的正式 Release（不含 draft / prerelease），按发布时间倒序。"""
    repo = validate_repo(repo)
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "bikepower-web-flash-site",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    per_page = max(1, min(int(limit) + 4, 30))
    releases = request_json(
        f"https://api.github.com/repos/{repo}/releases?per_page={per_page}",
        headers,
        "GitHub",
    )
    if not isinstance(releases, list):
        return []
    result = []
    for release in releases:
        if not isinstance(release, dict) or release.get("draft") or release.get("prerelease"):
            continue
        release["_provider"] = "github"
        result.append(release)
    return result


def release_assets(release):
    raw_assets = release.get("assets") or []
    if not isinstance(raw_assets, list):
        raise ReleaseValidationError("Release 附件列表格式无效")
    assets = []
    for raw in raw_assets:
        if not isinstance(raw, dict):
            continue
        name = raw.get("name") or raw.get("filename")
        url = (
            raw.get("browser_download_url")
            or raw.get("download_url")
            or raw.get("url")
        )
        if isinstance(name, str) and isinstance(url, str):
            assets.append({"name": name, "url": url})
    return assets


def release_version(release):
    tag = str(release.get("tag_name") or "").strip()
    match = re.fullmatch(r"v?([0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?)", tag)
    if not match:
        raise ReleaseUnavailable("最新 Release tag 不是受支持的语义版本")
    return tag, match.group(1)


def select_firmware_assets(release):
    tag, version = release_version(release)
    assets = release_assets(release)
    by_name = {}
    for asset in assets:
        by_name.setdefault(asset["name"], []).append(asset)

    selected = {}
    for board in BOARD_LABELS:
        matches = []
        for asset in assets:
            match = FIRMWARE_RE.fullmatch(asset["name"])
            if match and match.group("board") == board and match.group("version") == version:
                matches.append(asset)
        if not matches:
            continue
        if len(matches) != 1:
            raise ReleaseUnavailable(
                f"{tag} 存在多个 {board} merged bin（找到 {len(matches)} 个）"
            )
        firmware = matches[0]
        sha_name = firmware["name"] + ".sha256"
        sha_matches = by_name.get(sha_name, [])
        if len(sha_matches) != 1:
            raise ReleaseUnavailable(
                f"{tag} 缺少唯一的 {sha_name}（找到 {len(sha_matches)} 个）"
            )
        selected[board] = {"firmware": firmware, "sha256": sha_matches[0]}
    if not selected:
        raise ReleaseUnavailable(f"{tag} 没有受支持的板型 merged bin")
    return tag, version, selected


def safe_filename(name):
    if not name or name in {".", ".."} or Path(name).name != name or "\\" in name:
        raise ReleaseValidationError("Release 附件文件名包含非法路径")
    return name


def download_bytes(url, token="", provider="", allow_file=False):
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme == "file" and allow_file:
        path = Path(urllib.request.url2pathname(parsed.path))
        try:
            return path.read_bytes()
        except OSError as exc:
            raise ReleaseValidationError("本地测试附件读取失败") from exc
    if parsed.scheme != "https":
        raise ReleaseValidationError("Release 附件必须使用 HTTPS 地址")

    headers = {
        "Accept": "application/octet-stream",
        "User-Agent": "bikepower-web-flash-site",
    }
    if provider == "github" and token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            return response.read()
    except urllib.error.HTTPError as exc:
        raise ReleaseUnavailable(f"Release 附件下载返回 HTTP {exc.code}") from exc
    except (urllib.error.URLError, TimeoutError) as exc:
        raise ReleaseUnavailable("Release 附件下载失败") from exc


def parse_sha256(payload, firmware_name):
    try:
        lines = payload.decode("utf-8").splitlines()
    except UnicodeDecodeError as exc:
        raise ReleaseValidationError(f"{firmware_name}.sha256 不是 UTF-8 文本") from exc
    for line in lines:
        parts = line.strip().split()
        if not parts or not SHA256_RE.fullmatch(parts[0]):
            continue
        if len(parts) > 1 and parts[1].lstrip("*") != firmware_name:
            continue
        return parts[0].lower()
    raise ReleaseValidationError(f"{firmware_name}.sha256 未包含对应固件摘要")


def manifest(board, version, firmware_name):
    return {
        "builds": [
            {
                "chipFamily": "ESP32-C3",
                "parts": [{"offset": 0, "path": firmware_name}],
            }
        ],
        "name": f"BikePower C {BOARD_LABELS[board]}",
        "new_install_prompt_erase": True,
        "version": version,
    }


def notes_excerpt(release):
    body = str(release.get("body") or "").replace("\r\n", "\n").strip()
    if len(body) > NOTES_EXCERPT_MAX:
        body = body[:NOTES_EXCERPT_MAX].rstrip() + "…"
    return body


def materialize_release(site_dir, release, token="", allow_file=False, dest_rel="firmware"):
    """校验并镜像一个 Release；dest_rel 为站点内相对目录（最新版用 firmware）。"""
    if dest_rel != "firmware" and not re.fullmatch(r"firmware/v?[0-9A-Za-z.+-]+", dest_rel):
        raise ReleaseValidationError("固件输出目录非法")
    if ".." in dest_rel:
        raise ReleaseValidationError("固件输出目录非法")
    provider = str(release.get("_provider") or "fixture")
    tag, version, selected = select_firmware_assets(release)
    verified = {}

    for board, pair in selected.items():
        firmware_asset = pair["firmware"]
        sha_asset = pair["sha256"]
        firmware_name = safe_filename(firmware_asset["name"])
        safe_filename(sha_asset["name"])
        firmware_data = download_bytes(
            firmware_asset["url"], token=token, provider=provider, allow_file=allow_file
        )
        sha_data = download_bytes(
            sha_asset["url"], token=token, provider=provider, allow_file=allow_file
        )
        expected = parse_sha256(sha_data, firmware_name)
        actual = hashlib.sha256(firmware_data).hexdigest()
        if actual != expected:
            raise ReleaseValidationError(f"{firmware_name} SHA256 校验失败")
        verified[board] = {
            "data": firmware_data,
            "digest": actual,
            "name": firmware_name,
        }

    firmware_dir = site_dir / dest_rel
    firmware_dir.mkdir(parents=True, exist_ok=True)
    boards = {}
    for board, artifact in verified.items():
        firmware_data = artifact["data"]
        actual = artifact["digest"]
        firmware_name = artifact["name"]
        firmware_path = firmware_dir / firmware_name
        firmware_path.write_bytes(firmware_data)
        sha_path = firmware_dir / (firmware_name + ".sha256")
        sha_path.write_text(f"{actual}  {firmware_name}\n", encoding="utf-8")
        manifest_name = f"manifest-{board}.json"
        write_json(firmware_dir / manifest_name, manifest(board, version, firmware_name))
        boards[board] = {
            "download": f"{dest_rel}/{firmware_name}",
            "filename": firmware_name,
            "label": BOARD_LABELS[board],
            "manifest": f"{dest_rel}/{manifest_name}",
            "sha256": actual,
            "sha256_file": f"{dest_rel}/{firmware_name}.sha256",
            "size": len(firmware_data),
        }

    latest = {
        "available": True,
        "boards": boards,
        "generated_at": utc_now(),
        "provider": provider,
        "published_at": release.get("published_at") or release.get("created_at") or "",
        "release_url": release.get("html_url") or release.get("url") or "",
        "schema_version": 1,
        "tag": tag,
        "version": version,
    }
    if dest_rel == "firmware":
        write_json(firmware_dir / "latest.json", latest)
    return latest


def release_entry(info, is_latest, release):
    return {
        "boards": info["boards"],
        "latest": bool(is_latest),
        "notes": notes_excerpt(release),
        "published_at": info.get("published_at", ""),
        "release_url": info.get("release_url", ""),
        "tag": info["tag"],
        "version": info["version"],
    }


def materialize_history(site_dir, latest_info, latest_release, history, token="", allow_file=False, limit=DEFAULT_HISTORY):
    """镜像历史 Release 并写 firmware/releases.json；单个失败只跳过。"""
    entries = [release_entry(latest_info, True, latest_release)]
    skipped = []
    seen = {latest_info["tag"]}
    limit = max(0, min(int(limit), MAX_HISTORY))
    for release in history or []:
        if len(entries) > limit:
            break
        tag = str(release.get("tag_name") or "")
        if not tag or tag in seen:
            continue
        seen.add(tag)
        try:
            _, version = release_version(release)
            info = materialize_release(
                site_dir, release, token=token, allow_file=allow_file,
                dest_rel=f"firmware/{tag}",
            )
            entries.append(release_entry(info, False, release))
        except (ReleaseUnavailable, ReleaseValidationError) as exc:
            skipped.append({"reason": str(exc), "tag": tag})
    index = {
        "generated_at": utc_now(),
        "latest": latest_info["tag"],
        "releases": entries,
        "schema_version": 1,
        "skipped": skipped,
    }
    write_json(site_dir / "firmware" / "releases.json", index)
    return index


def initialize_site(source_dir, site_dir):
    source_dir = source_dir.resolve()
    site_dir = site_dir.resolve()
    if not source_dir.is_dir():
        raise RuntimeError(f"站点源目录不存在: {source_dir}")
    if source_dir == site_dir or source_dir in site_dir.parents or site_dir in source_dir.parents:
        raise RuntimeError("站点源目录与输出目录不能相同或互相嵌套")
    if site_dir == Path(site_dir.anchor):
        raise RuntimeError("拒绝使用文件系统根目录作为站点输出")
    if site_dir.exists():
        raise RuntimeError("站点输出目录已存在，拒绝递归覆盖")
    shutil.copytree(source_dir, site_dir)
    monitor = site_dir / "ble-monitor.html"
    if not monitor.is_file():
        raise RuntimeError("站点源目录缺少 ble-monitor.html")
    shutil.copy2(monitor, site_dir / "index.html")
    return site_dir


def write_unavailable(site_dir, reasons):
    firmware_dir = site_dir / "firmware"
    firmware_dir.mkdir(parents=True, exist_ok=True)
    latest = {
        "available": False,
        "boards": {},
        "generated_at": utc_now(),
        "reason": "；".join(reasons) if reasons else "没有可用的最新 Release 固件",
        "schema_version": 1,
    }
    write_json(firmware_dir / "latest.json", latest)
    write_json(firmware_dir / "releases.json", {
        "generated_at": latest["generated_at"],
        "latest": "",
        "releases": [],
        "schema_version": 1,
        "skipped": [],
    })
    return latest


def load_release_fixture(path):
    try:
        release = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ReleaseValidationError("离线 Release JSON 无法读取") from exc
    if not isinstance(release, dict):
        raise ReleaseValidationError("离线 Release JSON 顶层必须是对象")
    release.setdefault("_provider", "fixture")
    return release


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="生成 BLE Monitor 与网页刷写站点")
    parser.add_argument("--source-dir", type=Path, default=ROOT / "tools" / "web")
    parser.add_argument("--site-dir", type=Path, required=True)
    parser.add_argument(
        "--github-repo",
        default=os.getenv("GITHUB_REPOSITORY", "MengEn-Ink/bikepower-c"),
    )
    parser.add_argument(
        "--require-firmware",
        action="store_true",
        help="没有可用板型固件时失败；正式 Pages 部署必须启用",
    )
    parser.add_argument("--release-json", type=Path, help="离线测试用 Release API 响应")
    parser.add_argument(
        "--history",
        type=int,
        default=DEFAULT_HISTORY,
        help=f"额外镜像的历史版本数量（回滚用，0 关闭，最多 {MAX_HISTORY}）",
    )
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    site_dir = initialize_site(args.source_dir, args.site_dir)
    if args.release_json:
        release = load_release_fixture(args.release_json)
        latest = materialize_release(site_dir, release, allow_file=True)
        materialize_history(site_dir, latest, release, [], allow_file=True, limit=0)
        print(f"[web-flash] 离线站点已生成: v{latest['version']}")
        return 0

    github_token = os.getenv("GITHUB_TOKEN", "")
    reasons = []
    try:
        release = fetch_github_release(args.github_repo, github_token)
        if release is None:
            reasons.append("GitHub 没有可用 Release")
        else:
            latest = materialize_release(site_dir, release, token=github_token)
            history = []
            if args.history > 0:
                try:
                    history = fetch_github_release_list(args.github_repo, github_token, args.history)
                except ReleaseUnavailable as exc:
                    print(f"[web-flash] 历史版本列表获取失败，仅部署最新版: {exc}")
            index = materialize_history(
                site_dir, latest, release, history, token=github_token, limit=args.history
            )
            print(
                f"[web-flash] 已从 GitHub 生成 v{latest['version']} 板型刷写站点；"
                f"可回滚版本 {len(index['releases']) - 1} 个，跳过 {len(index['skipped'])} 个"
            )
            return 0
    except ReleaseUnavailable as exc:
        reasons.append(f"GitHub: {exc}")

    if args.require_firmware:
        raise ReleaseUnavailable("；".join(reasons))

    write_unavailable(site_dir, reasons)
    print("[web-flash] 未找到完整 Release；BLE Monitor 继续部署，网页刷写已禁用")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ReleaseUnavailable, ReleaseValidationError) as exc:
        print(f"[web-flash] ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
