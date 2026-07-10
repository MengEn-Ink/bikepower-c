#!/usr/bin/env python3
"""Portal render gate wiring checks."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tests" / "check_portal_render.py"
QUALITY_GATE = ROOT / "scripts" / "tools" / "quality_gate.py"
TESTS_README = ROOT / "tests" / "README.md"
TAG = "[portal-render-gate-check]"


def check(condition, message, failures):
    if condition:
        print(f"{TAG} PASS {message}")
    else:
        failures.append(message)
        print(f"{TAG} FAIL {message}")


def main():
    failures = []
    check(SCRIPT.is_file(), "存在 tests/check_portal_render.py", failures)
    if not SCRIPT.is_file():
        print(f"{TAG} 结果: {len(failures)} 失败")
        return 1

    src = SCRIPT.read_text(encoding="utf-8")
    quality_gate = QUALITY_GATE.read_text(encoding="utf-8")
    tests_readme = TESTS_README.read_text(encoding="utf-8")

    required_tokens = [
        ("tests/mock_server.py", "启动本地门户 mock server"),
        ("agent-browser", "使用 agent-browser 驱动真实浏览器"),
        ("#/settings/about", "直接进入关于页 hash 路由"),
        ("手机端不支持", "等待并校验电脑端升级说明文案"),
        ("https://easyhey.com/?firmware=MOD220", "校验 MOD220 升级地址"),
        ("screenshot", "保存渲染截图"),
        ("document.body.scrollHeight", "检查页面高度"),
        ("--screenshot-dir", "支持指定截图输出目录"),
    ]
    for token, message in required_tokens:
        check(token in src, message, failures)

    check("tests/check_portal_render.py" in quality_gate,
          "quality_gate.py 聚合门户渲染截图检查", failures)
    check("check_portal_render.py" in tests_readme,
          "tests/README.md 记录门户渲染截图检查", failures)

    result = subprocess.run(
        ["python3", str(SCRIPT), "--browser-cmd", "missing-agent-browser-for-test",
         "--screenshot-dir", str(Path(tempfile.gettempdir()))],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=20,
    )
    check(result.returncode != 0 and "未找到 agent-browser" in (result.stdout + result.stderr),
          "缺少浏览器工具时失败且给出明确环境原因", failures)

    if failures:
        print(f"{TAG} 结果: {len(failures)} 失败")
        return 1
    print(f"{TAG} PASS: 门户渲染截图门禁接线检查通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
