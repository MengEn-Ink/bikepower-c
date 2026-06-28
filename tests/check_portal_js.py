#!/usr/bin/env python3
"""门户 JS 语法静态检查（TC-3a）

从 main/wifi_portal.c 提取所有 C 字符串字面量拼接的 HTML，
定位所有 <script>...</script> 块，用 node --check 验证 JS 语法。

背景：v3.1.6 /ota 页 pollStatus 结尾 }}} 多一个 }，导致整个 script 块
报 "missing ) after argument list"，"检查更新"按钮点击完全无反应。
此类 bug 烧录后才发现成本极高，本脚本作为前置门在编译前拦截。

用法:
  python3 tests/check_portal_js.py                    # 检查默认文件
  python3 tests/check_portal_js.py --file main/wifi_portal.c
  python3 tests/check_portal_js.py --verbose          # 打印每个 script 块预览

退出码：
  0 = 全部 script 块 JS 语法正确
  1 = 有语法错误或 node 不可用
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

TAG = "[TC-3a]"

C_STRING_PATTERN = re.compile(r'"((?:\\.|[^"\\])*)"')
SCRIPT_OPEN = "<script>"
SCRIPT_CLOSE = "</script>"

# C printf 格式占位符（%d %s %ld %lu %f %u 等），用于 snprintf 模板
# JS 取模运算形如 `a%2`，占位符后跟 `,` `)` ` ` 等非字母数字，可区分
C_PRINTF_PATTERN = re.compile(r'%(?:l|ll)?[diuf]|%(?:l|ll)?[uxc]|%[0-9]*\.[0-9]+f|%s')


def substitute_c_printf_placeholders(js: str) -> str:
    """把 C printf 占位符替换为合法 JS 值，便于静态语法检查。

    %d/%ld/%lu/%u → 0
    %s → （空，因为 %s 通常在 JS 字符串字面量内：var="%s" → var=""）
    %f → 0.0
    其余原样保留。
    """
    def repl(m):
        token = m.group(0)
        if token.endswith("s"):
            return ""
        if token.endswith("f"):
            return "0.0"
        return "0"
    return C_PRINTF_PATTERN.sub(repl, js)


def extract_c_strings(src: str) -> str:
    """提取 C 源码中所有双引号字符串字面量内容，反转义后拼接成连续文本。"""
    parts = []
    for m in C_STRING_PATTERN.finditer(src):
        s = m.group(1)
        s = (s.replace("\\n", "\n")
              .replace("\\t", "\t")
              .replace('\\"', '"')
              .replace("\\'", "'")
              .replace("\\\\", "\\"))
        parts.append(s)
    return "".join(parts)


def find_script_blocks(text: str):
    """返回所有 (start, end, js_content) 三元组列表。"""
    blocks = []
    cursor = 0
    while True:
        s = text.find(SCRIPT_OPEN, cursor)
        if s < 0:
            break
        e = text.find(SCRIPT_CLOSE, s + len(SCRIPT_OPEN))
        if e < 0:
            blocks.append((s, -1, None))
            break
        js = text[s + len(SCRIPT_OPEN):e]
        blocks.append((s, e, js))
        cursor = e + len(SCRIPT_CLOSE)
    return blocks


def check_js_syntax(js: str, node_path: str) -> tuple:
    """用 node --check 验证 JS 语法。返回 (ok: bool, message: str)。

    自动替换 C printf 占位符（%d/%s 等）为合法 JS 值后再检查，
    避免 snprintf 模板被误判为语法错误。
    """
    if not js.strip():
        return True, "空 script 块"
    checked_js = substitute_c_printf_placeholders(js)
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".js", delete=False, encoding="utf-8"
    ) as f:
        f.write(checked_js)
        tmp_path = f.name
    try:
        result = subprocess.run(
            [node_path, "--check", tmp_path],
            capture_output=True, text=True, timeout=10,
        )
        if result.returncode == 0:
            return True, "OK"
        err = result.stderr.strip() or result.stdout.strip()
        return False, err
    finally:
        os.unlink(tmp_path)


def main():
    parser = argparse.ArgumentParser(description="门户 JS 语法静态检查 (TC-3a)")
    parser.add_argument("--file", default="main/wifi_portal.c",
                        help="待检查的 C 源文件（默认 main/wifi_portal.c）")
    parser.add_argument("--verbose", action="store_true",
                        help="打印每个 script 块前 80 字符预览")
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
        src = f.read()

    text = extract_c_strings(src)
    blocks = find_script_blocks(text)

    if not blocks:
        print(f"{TAG} WARN: 未在 {args.file} 中找到任何 <script> 块", file=sys.stderr)
        return 0

    print(f"{TAG} 检查 {args.file}: 共 {len(blocks)} 个 <script> 块")
    print(f"{TAG} node: {node_path}")
    print()

    all_ok = True
    for i, (s, e, js) in enumerate(blocks, 1):
        if js is None:
            print(f"{TAG} [{i}] FAIL: <script> 未闭合 (start={s})")
            all_ok = False
            continue
        ok, msg = check_js_syntax(js, node_path)
        size = len(js)
        status = "OK  " if ok else "FAIL"
        print(f"{TAG} [{i}] {status}  size={size}  range=[{s},{e})")
        if args.verbose and js.strip():
            preview = js[:80].replace("\n", "\\n")
            print(f"{TAG}       preview: {preview!r}")
        if not ok:
            all_ok = False
            for line in msg.split("\n")[:6]:
                print(f"{TAG}       {line}")
            print()

    print()
    if all_ok:
        print(f"{TAG} PASS: 全部 {len(blocks)} 个 script 块 JS 语法正确")
        return 0
    else:
        print(f"{TAG} FAIL: 存在 JS 语法错误，禁止编译/烧录，请修复后再继续", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
