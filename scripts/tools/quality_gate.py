#!/usr/bin/env python3
"""BikePower C 项目质量扫描工具

扫描范围：
1. 编译检查 — idf.py build 是否通过
2. Host 单测 — test/test_domain 断言是否全过
3. 常量一致性 — bikepower_config.h vs domain_config.h 重复定义冲突
4. 必备资产 — 关键文档/测试脚本是否存在
5. Git 工作区 — 未提交/未推送统计

环境要求：
- 已 source esp-idf/export.sh；或脚本自动按优先级探测路径激活。
- IDF 探测优先级：$IDF_PATH 环境变量 → $HOME/esp/esp-idf-v5.5.4（macOS 默认）→ ../esp32-power/esp-idf（相邻项目布局）→ /home/mengen1014/work/esp32-power/esp-idf（Linux 默认）。
- 仅支持 ESP-IDF v5.5.x 版本。
"""

import os
import re
import subprocess
import sys

# 项目根目录：脚本位于 scripts/tools/，向上两级即项目根
PROJECT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# IDF 路径探测优先级列表
IDF_CANDIDATE_PATHS = [
    # 优先使用环境变量
    os.environ.get("IDF_PATH"),
    # macOS 默认安装路径
    os.path.expanduser("~/esp/esp-idf-v5.5.4"),
    # 相邻项目布局（bikepower-c 和 esp32-power 在同一父目录）
    os.path.abspath(os.path.join(PROJECT, "..", "esp32-power", "esp-idf")),
    # 原 Linux 开发机默认路径
    "/home/mengen1014/work/esp32-power/esp-idf",
]

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
    r = subprocess.run(["bash", "-c", cmd], capture_output=True, text=True, cwd=PROJECT)
    return r.stdout + r.stderr, r.returncode


def idf_ready():
    """检查 idf.py 是否已在 PATH 中可用。"""
    r = subprocess.run(["bash", "-c", "idf.py --version >/dev/null 2>&1"])
    return r.returncode == 0


def ensure_idf():
    """若 idf.py 不可用，则按优先级探测 IDF 路径并返回激活命令前缀。
    返回 None 表示未找到可用的 ESP-IDF 环境。
    """
    if idf_ready():
        return ""
    # 过滤掉 None/空路径，按顺序探测
    for path in filter(None, IDF_CANDIDATE_PATHS):
        export_sh = os.path.join(path, "export.sh")
        if not os.path.exists(export_sh):
            continue
        # 验证 source 后 idf.py 确实可用
        test_cmd = f". {export_sh} >/dev/null 2>&1 && idf.py --version >/dev/null 2>&1"
        r = subprocess.run(["bash", "-c", test_cmd], cwd=PROJECT)
        if r.returncode == 0:
            return f". {export_sh} >/dev/null 2>&1 && "
    # 所有候选路径均无效
    return None


# 1. 编译检查
print("\n=== 1. 编译检查 ===")
idf_prefix = ensure_idf()
if idf_prefix is None:
    fail("未找到可用的 ESP-IDF v5.5.x 环境，请设置 IDF_PATH 环境变量，或安装到默认路径 ~/esp/esp-idf-v5.5.4")
else:
    out, rc = run(f"{idf_prefix}idf.py build 2>&1 | tail -5")
    if rc == 0 and ("Project build complete" in out or "write_flash" in out):
        ok("idf.py build 通过")
    else:
        fail(f"idf.py build 失败 (rc={rc}): {out[:200]}")

# 2. Host 单测
print("\n=== 2. Host 单测 ===")
out, rc = run("make -C test clean >/dev/null 2>&1 && make -C test 2>&1 >/dev/null && ./test/test_domain 2>&1 | tail -3")
m = re.search(r'(\d+) 通过.*?(\d+) 失败', out)
if m:
    npass, nfail = int(m.group(1)), int(m.group(2))
    if nfail == 0:
        ok(f"单测通过 ({npass} 断言)")
    else:
        fail(f"单测有失败: {npass} 通过, {nfail} 失败")
else:
    fail(f"单测未识别输出: {out[:200]}")

# 3. Git 状态
print("\n=== 3. Git 状态 ===")
out, _ = run("git status --short")
out_stripped = out.strip()
if out_stripped:
    n = len(out_stripped.split('\n'))
    warn(f"有 {n} 个未提交的改动")
else:
    ok("工作区 clean")

out, _ = run("git rev-list --count @{u}..HEAD 2>/dev/null")
out = out.strip()
if out.isdigit() and int(out) > 0:
    warn(f"有 {out} 个未推送的 commit")
else:
    ok("全部已推送（或无上游）")

# 4. 必备资产
print("\n=== 4. 必备资产 ===")
required_assets = [
    "README.md",
    "AGENTS.md",
    "CHANGELOG.md",
    "PROGRESS.md",
    "LICENSE",
    "docs/INDEX.md",
    "docs/user/01-quickstart.md",
    "docs/user/02-user-manual.md",
    "docs/user/03-hardware.md",
    "docs/user/04-ota-user-guide.md",
    "docs/dev/01-technical-reference.md",
    "docs/dev/02-firmware-security.md",
    "docs/dev/03-test-coverage-analysis.md",
    "docs/archive/01-cidf-nimble-rewrite.md",
    "docs/archive/02-micropython-vs-c-comparison.md",
    "main/app_main.c",
    "main/ble_bikepower.c",
    "main/wifi_portal.c",
    "main/radio_ctrl.c",
    "main/bikepower_config.h",
    "components/bikepower_domain/include/domain_config.h",
    "test/test_domain.c",
    "tests/ble_selftest.py",
    "tests/stage3_conn_test.py",
    "tests/stage5_portal_test.py",
    "tests/serial_capture.py",
]
for d in required_assets:
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
            # 仅匹配简单常量宏 #define NAME value（NAME 后非 '('，过滤函数宏）
            m = re.match(r'#define\s+(\w+)(?!\S)\s+(\S+)', line)
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
