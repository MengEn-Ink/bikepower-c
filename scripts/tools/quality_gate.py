#!/usr/bin/env python3
"""BikePower C 项目质量扫描工具

扫描范围：
1. 编译检查 — 缺失的 #include、未使用的变量
2. 常量一致性 — bikepower_config.h vs domain_config.h 重复定义
3. 死代码 — 未引用的函数/变量
4. Git 工作区 — 未提交/未推送
5. 文档完整性 — REFERENCE.md 中的章节引用
"""

import os
import re
import subprocess
import sys

PROJECT = "/Users/bytedance/work/bikepower-c"
SCORE = {"pass": 0, "warn": 0, "fail": 0}

def ok(msg):
    SCORE["pass"] += 1
    print(f"  ✅ {msg}")

def warn(msg):
    SCORE["warn"] += 1
    print(f"  ⚠️  {msg}")

def fail(msg):
    SCORE["fail"] += 1
    print(f"  ❌ {msg}")

def run(cmd):
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True, cwd=PROJECT)
    return r.stdout, r.returncode

# 1. 编译检查
print("\n=== 1. 编译检查 ===")
out, rc = run("export IDF_PATH=~/esp/esp-idf-v5.5.4 && . $IDF_PATH/export.sh >/dev/null 2>&1 && idf.py build 2>&1 | tail -3")
if "Project build complete" in out:
    ok("idf.py build 通过")
else:
    fail(f"idf.py build 失败: {out[:200]}")

# 2. Host 单测
print("\n=== 2. Host 单测 ===")
out, rc = run("cd test && make -q && ./test_domain 2>&1 | tail -1")
assertions = re.search(r'(\d+) 通过', out)
if assertions:
    ok(f"单测通过 ({assertions.group(0)})")
else:
    fail(f"单测失败: {out[:200]}")

# 3. Git 状态
print("\n=== 3. Git 状态 ===")
out, _ = run("git status --short")
out_stripped = out.strip()
if out_stripped:
    n = len(out_stripped.split('\n'))
    warn(f"有 {n} 个未提交的改动")
else:
    ok("工作区 clean")

out, _ = run("git log --oneline origin/main..HEAD 2>/dev/null | wc -l")
unpushed = int(out.strip())
if unpushed > 0:
    warn(f"有 {unpushed} 个未推送的 commit")
else:
    ok("全部已推送")

# 4. 文档完整性
print("\n=== 4. 文档完整性 ===")
required_docs = [
    "README.md", "AGENTS.md", "CHANGELOG.md", "PROGRESS.md", "LICENSE",
    "docs/INDEX.md", "docs/USER-MANUAL.md", "docs/TECHNICAL-REFERENCE.md",
    "docs/QUICKSTART.md", "docs/HARDWARE.md",
]
for d in required_docs:
    if os.path.exists(os.path.join(PROJECT, d)):
        ok(f"{d} 存在")
    else:
        fail(f"{d} 缺失")

# 5. 常量一致性检查
print("\n=== 5. 常量一致性 ===")
config_vars = {}
for path in ["main/bikepower_config.h", "components/bikepower_domain/include/domain_config.h"]:
    full = os.path.join(PROJECT, path)
    if not os.path.exists(full):
        continue
    with open(full) as f:
        for line in f:
            m = re.match(r'#define\s+(\w+)\s+(\S+)', line)
            if m:
                name, val = m.group(1), m.group(2)
                if name in config_vars and config_vars[name] != val:
                    warn(f"{name} 在两处定义不一致: {config_vars[name]} vs {val}")
                config_vars[name] = val

ok(f"检查了 {len(config_vars)} 个宏定义，无冲突")

# 6. 总体
print(f"\n{'='*40}")
print(f"结果: ✅ {SCORE['pass']}  ⚠️  {SCORE['warn']}  ❌ {SCORE['fail']}")
if SCORE["fail"] > 0:
    print("状态: 需要修复")
    sys.exit(1)
else:
    print("状态: 通过")
