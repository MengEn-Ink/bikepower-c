#!/usr/bin/env python3
"""Create a Gitee Release and upload BikePower C release artifacts."""
import argparse
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_OWNER = "mengen4jv"
DEFAULT_REPO = "bikepower-c"
API_BASE = "https://gitee.com/api/v5"


def load_env(path):
    values = {}
    if not path.exists():
        return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def git_output(args):
    return subprocess.check_output(["git", "-C", str(ROOT)] + args, text=True).strip()


def api_request(method, url, token, data=None, files=None):
    if files:
        boundary = "----BikePowerRelease" + uuid.uuid4().hex
        body = bytearray()
        fields = {"access_token": token}
        for key, value in fields.items():
            body.extend(f"--{boundary}\r\n".encode())
            body.extend(f'Content-Disposition: form-data; name="{key}"\r\n\r\n'.encode())
            body.extend(str(value).encode())
            body.extend(b"\r\n")
        for field, path in files.items():
            payload = path.read_bytes()
            body.extend(f"--{boundary}\r\n".encode())
            body.extend(
                f'Content-Disposition: form-data; name="{field}"; filename="{path.name}"\r\n'.encode()
            )
            body.extend(b"Content-Type: application/octet-stream\r\n\r\n")
            body.extend(payload)
            body.extend(b"\r\n")
        body.extend(f"--{boundary}--\r\n".encode())
        headers = {"Content-Type": f"multipart/form-data; boundary={boundary}"}
        req = urllib.request.Request(url, data=bytes(body), headers=headers, method=method)
    else:
        payload = data or {}
        payload["access_token"] = token
        encoded = urllib.parse.urlencode(payload).encode()
        req = urllib.request.Request(url, data=encoded, method=method)
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            text = resp.read().decode("utf-8")
            return json.loads(text) if text else {}
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"Gitee API {method} {url} failed: HTTP {exc.code}: {detail}") from exc


def find_artifacts(release_dir, version, git_sha):
    required = [
        f"bikepower-c-generic-c3-v{version}-stable-fix-g{git_sha}.bin",
        f"bikepower-c-generic-c3-v{version}-stable-fix-g{git_sha}.bin.sha256",
        f"bikepower-c-m5stamp-c3-v{version}-stable-fix-g{git_sha}.bin",
        f"bikepower-c-m5stamp-c3-v{version}-stable-fix-g{git_sha}.bin.sha256",
        f"bikepower-c-release-v{version}-stable-fix-g{git_sha}.zip",
        f"bikepower-c-release-v{version}-stable-fix-g{git_sha}.zip.sha256",
        "manifest.txt",
        "test-report.md",
        "core-flow-endurance.md",
        "user-manual.md",
    ]
    missing = [name for name in required if not (release_dir / name).is_file()]
    if missing:
        raise FileNotFoundError("release 产物缺失: " + ", ".join(missing))
    return [release_dir / name for name in required]


def release_body(version, git_sha, artifacts):
    artifact_lines = "\n".join(f"- `{path.name}`" for path in artifacts)
    return "\n".join([
        f"BikePower C v{version}",
        "",
        "## 产物",
        "",
        artifact_lines,
        "",
        "## 烧录说明",
        "",
        "普通用户请使用网页端在线烧录：",
        "",
        "https://easyhey.com/?firmware=MOD220",
        "",
        "使用 Chrome 或 Edge 打开链接，把开发板接入电脑，在浏览器弹出的串口列表里选择 `USB JTAG/serial debug unit` 或对应 `COMx` 端口，然后按网页提示完成烧录。",
        "",
        "串口选择截图和完整步骤见随附 `user-manual.md`，图片位于总包 `images/web-flash-serial-port.jpg`。",
        "",
        "开发者如需手动烧录，也可以下载对应板型 merged bin 后使用单文件烧录：",
        "",
        "```bash",
        "esptool.py --chip esp32c3 -b 460800 write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x0 <merged-bin>",
        "```",
        "",
        "## 测试说明",
        "",
        "- `test-report.md`：发版测试报告。",
        "- `core-flow-endurance.md`：3 分钟核心流程耐久测试明细。",
        "- `user-manual.md`：随固件分发的用户手册。",
        "- 未执行的真机或人工项以测试报告记录为准。",
        "",
        f"Git ID: `{git_sha}`",
    ])


def main():
    parser = argparse.ArgumentParser(description="发布 BikePower C Gitee Release")
    parser.add_argument("--owner", default=DEFAULT_OWNER)
    parser.add_argument("--repo", default=DEFAULT_REPO)
    parser.add_argument("--version", default="")
    parser.add_argument("--tag", default="")
    parser.add_argument("--target", default="")
    parser.add_argument("--release-dir", default=str(ROOT / "build" / "release"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    env = load_env(ROOT / ".env")
    token = env.get("GITEE_TOKEN") or os.getenv("GITEE_TOKEN")
    if not token:
        raise SystemExit("缺少 GITEE_TOKEN，请在 .env 或环境变量中配置")

    version = args.version or re.search(
        r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"',
        (ROOT / "main" / "bikepower_config.h").read_text(encoding="utf-8"),
    ).group(1)
    tag = args.tag or f"v{version}"
    target = args.target or git_output(["rev-parse", "--short", "HEAD"])
    git_sha = target
    release_dir = Path(args.release_dir).resolve()
    artifacts = find_artifacts(release_dir, version, git_sha)

    print(f"Release: {args.owner}/{args.repo} {tag} target={target}")
    print("待上传附件:")
    for path in artifacts:
        print(f"  - {path.name} ({path.stat().st_size} bytes)")
    if args.dry_run:
        print("dry-run: 不创建 Release，不上传附件")
        return 0

    create_url = f"{API_BASE}/repos/{args.owner}/{args.repo}/releases"
    release = api_request("POST", create_url, token, {
        "tag_name": tag,
        "target_commitish": target,
        "name": tag,
        "body": release_body(version, git_sha, artifacts),
        "prerelease": "false",
    })
    release_id = release.get("id")
    if not release_id:
        raise RuntimeError(f"创建 Release 后未返回 id: {release}")
    print(f"已创建 Release id={release_id}")

    upload_url = f"{API_BASE}/repos/{args.owner}/{args.repo}/releases/{release_id}/attach_files"
    for path in artifacts:
        api_request("POST", upload_url, token, files={"file": path})
        print(f"已上传: {path.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
