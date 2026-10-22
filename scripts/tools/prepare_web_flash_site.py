#!/usr/bin/env python3
"""生成 BikePower BLE Monitor / ESP Web Tools 静态站点。

部署时优先读取 GitHub 最新 Release，必要时回退到 Gitee。脚本只接受当前
Release 中两个受支持板型的 merged bin，并在复制到站点前验证旁文件 SHA256。
没有可用 Release 时仍生成可部署站点，但 latest.json 会明确标记不可刷写。
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


def fetch_gitee_release(repo, token):
    repo = validate_repo(repo)
    query = ""
    if token:
        query = "?" + urllib.parse.urlencode({"access_token": token})
    release = request_json(
        f"https://gitee.com/api/v5/repos/{repo}/releases/latest{query}",
        {"Accept": "application/json", "User-Agent": "bikepower-web-flash-site"},
        "Gitee",
    )
    if release is not None:
        release["_provider"] = "gitee"
    return release


def release_assets(release):
    raw_assets = release.get("assets") or release.get("attach_files") or []
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
        if len(matches) != 1:
            raise ReleaseUnavailable(
                f"{tag} 缺少唯一的 {board} merged bin（找到 {len(matches)} 个）"
            )
        firmware = matches[0]
        sha_name = firmware["name"] + ".sha256"
        sha_matches = by_name.get(sha_name, [])
        if len(sha_matches) != 1:
            raise ReleaseUnavailable(
                f"{tag} 缺少唯一的 {sha_name}（找到 {len(sha_matches)} 个）"
            )
        selected[board] = {"firmware": firmware, "sha256": sha_matches[0]}
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

    if provider == "gitee" and token:
        query = urllib.parse.parse_qsl(parsed.query, keep_blank_values=True)
        if not any(key == "access_token" for key, _ in query):
            query.append(("access_token", token))
            parsed = parsed._replace(query=urllib.parse.urlencode(query))
            url = urllib.parse.urlunparse(parsed)

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


def materialize_release(site_dir, release, token="", allow_file=False):
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

    firmware_dir = site_dir / "firmware"
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
            "download": f"firmware/{firmware_name}",
            "filename": firmware_name,
            "label": BOARD_LABELS[board],
            "manifest": f"firmware/{manifest_name}",
            "sha256": actual,
            "sha256_file": f"firmware/{firmware_name}.sha256",
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
    write_json(firmware_dir / "latest.json", latest)
    return latest


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
    parser.add_argument("--github-repo", default=os.getenv("GITHUB_REPOSITORY", ""))
    parser.add_argument("--gitee-repo", default=os.getenv("GITEE_REPOSITORY", "mengen4jv/bikepower-c"))
    parser.add_argument("--release-json", type=Path, help="离线测试用 Release API 响应")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    site_dir = initialize_site(args.source_dir, args.site_dir)
    if args.release_json:
        release = load_release_fixture(args.release_json)
        latest = materialize_release(site_dir, release, allow_file=True)
        print(f"[web-flash] 离线站点已生成: v{latest['version']}")
        return 0

    github_token = os.getenv("GITHUB_TOKEN", "")
    gitee_token = os.getenv("GITEE_TOKEN", "")
    sources = []
    if args.github_repo:
        sources.append(
            ("GitHub", lambda: fetch_github_release(args.github_repo, github_token), github_token)
        )
    if args.gitee_repo:
        sources.append(
            ("Gitee", lambda: fetch_gitee_release(args.gitee_repo, gitee_token), gitee_token)
        )

    reasons = []
    for label, fetch, token in sources:
        try:
            release = fetch()
            if release is None:
                reasons.append(f"{label} 没有可用 Release")
                continue
            latest = materialize_release(site_dir, release, token=token)
            print(
                f"[web-flash] 已从 {label} 生成 v{latest['version']} 双板型刷写站点"
            )
            return 0
        except ReleaseUnavailable as exc:
            reasons.append(f"{label}: {exc}")

    write_unavailable(site_dir, reasons)
    print("[web-flash] 未找到完整 Release；BLE Monitor 继续部署，网页刷写已禁用")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ReleaseUnavailable, ReleaseValidationError) as exc:
        print(f"[web-flash] ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
