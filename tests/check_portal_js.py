#!/usr/bin/env python3
"""门户 JS 语法静态检查（TC-3a，v3.2.0 重构版）

直接对 main/web/app.js 用 node --check 验证 JS 语法。

背景：v3.1.6 /ota 页 pollStatus 结尾 }}} 多一个 }，导致整个 script 块
报 "missing ) after argument list"，"检查更新"按钮点击完全无反应。
v3.2.0 重构后 JS 不再嵌入 C 字符串，直接从 main/web/app.js 读取。

用法:
  python3 tests/check_portal_js.py                    # 检查默认文件
  python3 tests/check_portal_js.py --file main/web/app.js
  python3 tests/check_portal_js.py --verbose          # 打印文件大小

退出码：
  0 = JS 语法正确
  1 = 有语法错误或 node 不可用
"""
import argparse
import os
import shutil
import subprocess
import sys

TAG = "[TC-3a]"


def main():
    parser = argparse.ArgumentParser(description="门户 JS 语法静态检查 (TC-3a)")
    parser.add_argument("--file", default="main/web/app.js",
                        help="待检查的 JS 文件（默认 main/web/app.js）")
    parser.add_argument("--verbose", action="store_true",
                        help="打印文件大小")
    args = parser.parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target = args.file if os.path.isabs(args.file) else os.path.join(repo_root, args.file)
    if not os.path.isfile(target):
        print(f"{TAG} FAIL: 文件不存在 {target}", file=sys.stderr)
        return 1

    node_path = shutil.which("node")
    if not node_path:
        print(f"{TAG} FAIL: 未找到 node 可执行文件，无法验证 JS 语法", file=sys.stderr)
        print(f"{TAG} 请安装 Node.js (apt install nodejs 或 nvm install --lts)", file=sys.stderr)
        return 1

    with open(target, encoding="utf-8") as f:
        js = f.read()

    size = len(js)
    if args.verbose:
        print(f"{TAG} 文件: {target}")
        print(f"{TAG} 大小: {size} 字节")
        print(f"{TAG} node: {node_path}")
        print()

    if not js.strip():
        print(f"{TAG} FAIL: {target} 为空文件", file=sys.stderr)
        return 1

    result = subprocess.run(
        [node_path, "--check", target],
        capture_output=True, text=True, timeout=15,
    )
    if result.returncode == 0:
        print(f"{TAG} PASS: {os.path.relpath(target, repo_root)} JS 语法正确 (size={size})")
        return 0

    err = result.stderr.strip() or result.stdout.strip()
    print(f"{TAG} FAIL: JS 语法错误，禁止编译/烧录，请修复后再继续", file=sys.stderr)
    for line in err.split("\n")[:12]:
        print(f"{TAG}   {line}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
