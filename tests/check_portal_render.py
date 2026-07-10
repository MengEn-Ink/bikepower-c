#!/usr/bin/env python3
"""Render the local portal About page and capture a screenshot.

This is a lightweight browser gate for the web portal. It starts the existing
mock server, opens the current main/web build through a real browser, verifies
the compact upgrade guidance, and stores a screenshot for review.
"""
import argparse
import json
import os
import shlex
import shutil
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Keep this literal path visible for the static wiring gate.
MOCK_SERVER_REL = "tests/mock_server.py"
MOCK_SERVER = ROOT / "tests" / "mock_server.py"
DEFAULT_SCREENSHOT_DIR = ROOT / "tests" / "reports" / "screenshots"
TAG = "[portal-render]"


def find_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_http(url, timeout=8):
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1) as resp:
                if resp.status == 200:
                    return
        except Exception as exc:
            last_error = exc
            time.sleep(0.15)
    raise RuntimeError(f"mock server 未就绪: {last_error}")


def resolve_browser_cmd(browser_cmd):
    parts = shlex.split(browser_cmd)
    if not parts:
        raise RuntimeError("未找到 agent-browser: browser command 为空")
    if shutil.which(parts[0]) is None:
        raise RuntimeError(f"未找到 agent-browser: {parts[0]}")
    return parts


def run_browser(browser, session, args, timeout=30):
    cmd = browser + ["--session", session] + args
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
    if result.returncode != 0:
        output = (result.stdout + result.stderr).strip()
        raise RuntimeError(f"agent-browser 命令失败: {' '.join(args)}\n{output}")
    return result.stdout.strip()


def parse_eval_json(raw):
    value = json.loads(raw)
    if isinstance(value, str):
        value = json.loads(value)
    if not isinstance(value, dict):
        raise RuntimeError(f"浏览器 eval 返回类型错误: {type(value).__name__}")
    return value


def main():
    parser = argparse.ArgumentParser(description="渲染门户关于页并生成截图")
    parser.add_argument("--browser-cmd", default=os.environ.get("AGENT_BROWSER_CMD", "npx agent-browser"),
                        help="agent-browser 命令，默认使用 'npx agent-browser'")
    parser.add_argument("--screenshot-dir", default=str(DEFAULT_SCREENSHOT_DIR),
                        help="截图输出目录，默认 tests/reports/screenshots")
    parser.add_argument("--viewport-width", type=int, default=390)
    parser.add_argument("--viewport-height", type=int, default=873)
    parser.add_argument("--max-scroll-height", type=int, default=980,
                        help="移动端关于页允许的最大页面高度，避免内容明显超过一屏")
    args = parser.parse_args()

    try:
        browser = resolve_browser_cmd(args.browser_cmd)
    except RuntimeError as exc:
        print(f"{TAG} FAIL {exc}")
        return 1

    screenshot_dir = Path(args.screenshot_dir)
    screenshot_dir.mkdir(parents=True, exist_ok=True)

    port = find_free_port()
    server = subprocess.Popen(
        [sys.executable, str(MOCK_SERVER), "--bind", "127.0.0.1", "--port", str(port)],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
        text=True,
    )
    session = f"bikepower-portal-render-{int(time.time())}-{port}"
    screenshot_path = screenshot_dir / f"about-render-{int(time.time())}.png"
    url = f"http://127.0.0.1:{port}/?t={int(time.time())}#/settings/about"

    try:
        wait_http(f"http://127.0.0.1:{port}/")
        run_browser(browser, session, ["set", "viewport", str(args.viewport_width), str(args.viewport_height)])
        run_browser(browser, session, ["open", url])
        run_browser(browser, session, ["wait", "--load", "networkidle"], timeout=20)
        run_browser(browser, session, ["wait", "--text", "手机端不支持"], timeout=20)
        run_browser(browser, session, ["screenshot", "--screenshot-dir", str(screenshot_dir), str(screenshot_path)])
        metrics_raw = run_browser(
            browser,
            session,
            ["eval", (
                "JSON.stringify({"
                "href:location.href,"
                "hasUpgrade:document.body.innerText.includes('https://easyhey.com/?firmware=MOD220'),"
                "hasDesktopOnly:document.body.innerText.includes('仅支持电脑端烧录'),"
                "hasMobileUnsupported:document.body.innerText.includes('手机端不支持'),"
                "hasCopyButton:!!document.querySelector('[onclick=\"onCopyUpgradeUrl()\"]'),"
                "oldHero:!!document.querySelector('.about-hero'),"
                "scrollHeight:document.body.scrollHeight,"
                "viewportHeight:window.innerHeight"
                "})"
            )],
        )
        metrics = parse_eval_json(metrics_raw)

        failures = []
        if "#/settings/about" not in metrics.get("href", ""):
            failures.append("未停留在关于页 hash 路由")
        if not metrics.get("hasUpgrade"):
            failures.append("缺少 MOD220 在线升级地址")
        if not metrics.get("hasDesktopOnly"):
            failures.append("缺少仅支持电脑端烧录文案")
        if not metrics.get("hasMobileUnsupported"):
            failures.append("缺少手机端不支持文案")
        if not metrics.get("hasCopyButton"):
            failures.append("缺少升级地址一键复制按钮")
        if metrics.get("oldHero"):
            failures.append("旧 about-hero 介绍内容仍在页面中")
        if int(metrics.get("scrollHeight", 0)) > args.max_scroll_height:
            failures.append(
                f"关于页高度 {metrics.get('scrollHeight')} 超过限制 {args.max_scroll_height}"
            )
        if not screenshot_path.is_file() or screenshot_path.stat().st_size <= 0:
            failures.append(f"截图未生成或为空: {screenshot_path}")

        if failures:
            for failure in failures:
                print(f"{TAG} FAIL {failure}")
            return 1

        print(f"{TAG} PASS 关于页渲染检查通过")
        print(f"{TAG} PASS 截图: {screenshot_path}")
        print(f"{TAG} PASS 页面高度: {metrics.get('scrollHeight')} / viewport {metrics.get('viewportHeight')}")
        return 0
    except Exception as exc:
        print(f"{TAG} FAIL {exc}")
        return 1
    finally:
        try:
            run_browser(browser, session, ["close"], timeout=10)
        except Exception:
            pass
        server.terminate()
        try:
            server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == "__main__":
    raise SystemExit(main())
