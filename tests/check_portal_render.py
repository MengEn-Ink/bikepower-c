#!/usr/bin/env python3
"""真实 Chromium 渲染核心门户，验证渐进交互并保存截图。"""
import argparse
import subprocess
import sys
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--screenshot-dir", default=str(ROOT / "tests/reports/screenshots"))
    parser.add_argument("--color-scheme", choices=("light", "dark"), default="light")
    args = parser.parse_args()
    python = sys.executable
    for candidate in dict.fromkeys([sys.executable, shutil.which("python3"),
                                    "/usr/bin/python3", "/Library/Developer/CommandLineTools/usr/bin/python3"]):
        if candidate and Path(candidate).is_file():
            probe = subprocess.run([candidate, "-c", "import playwright"], capture_output=True)
            if probe.returncode == 0:
                python = candidate
                break
    result = subprocess.run([python, str(ROOT / "tests/portal_bridge_browser.py"),
                             "--screenshot-dir", args.screenshot_dir,
                             "--color-scheme", args.color_scheme], cwd=ROOT)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
