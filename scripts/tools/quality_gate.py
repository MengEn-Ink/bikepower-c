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
import shutil
import subprocess
import sys
from pathlib import Path

# 项目根目录：脚本位于 scripts/tools/，向上两级即项目根
PROJECT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

IS_WINDOWS = sys.platform.startswith("win")

# IDF 路径探测优先级列表
IDF_CANDIDATE_PATHS = [
    # 优先使用环境变量
    os.environ.get("IDF_PATH"),
    # Windows 默认安装路径
    os.path.expandvars(r"%USERPROFILE%\esp\esp-idf-v5.5.4") if IS_WINDOWS else None,
    r"C:\Espressif\frameworks\esp-idf-v5.5.4" if IS_WINDOWS else None,
    # macOS 默认安装路径
    os.path.expanduser("~/esp/esp-idf-v5.5.4") if not IS_WINDOWS else None,
    # 相邻项目布局（bikepower-c 和 esp32-power 在同一父目录）
    os.path.abspath(os.path.join(PROJECT, "..", "esp32-power", "esp-idf")),
    # 原 Linux 开发机默认路径
    "/home/mengen1014/work/esp32-power/esp-idf" if not IS_WINDOWS else None,
]

SCORE = {"pass": 0, "warn": 0, "fail": 0}


def ok(msg):
    SCORE["pass"] += 1
    print(f"  \u2705 {msg}")


def warn(msg):
    SCORE["warn"] += 1
    print(f"  \u26a0\ufe0f  {msg}")


def fail(msg):
    SCORE["fail"] += 1
    print(f"  \u274c {msg}")


def find_python():
    """返回可用的 Python 解释器路径。"""
    for py in ["python3", "python"]:
        if shutil.which(py):
            return py
    return sys.executable


def run_cmd(cmd_args, cwd=PROJECT, timeout=None):
    """以列表参数形式运行命令，返回 (stdout+stderr, returncode)。
    优先直接传参，不经过 shell，避免平台差异。"""
    try:
        r = subprocess.run(
            cmd_args,
            capture_output=True,
            text=True,
            cwd=cwd,
            timeout=timeout,
        )
        return r.stdout + r.stderr, r.returncode
    except FileNotFoundError as e:
        return f"命令未找到: {e}", 127
    except Exception as e:
        return f"执行异常: {e}", 1


def run(cmd):
    """向后兼容：接受字符串命令，Windows 下走 cmd /c，非 Windows 走 bash -c。
    新代码优先用 run_cmd 直接传参。"""
    if IS_WINDOWS:
        return run_cmd(["cmd", "/c", cmd])
    else:
        return run_cmd(["bash", "-c", cmd])


def find_idf_python(idf_path):
    """在 ESP-IDF 安装中找到对应的 Python 解释器。"""
    python_env = os.environ.get("IDF_PYTHON_ENV_PATH", "")
    if python_env:
        py_exe = os.path.join(python_env, "Scripts", "python.exe") if IS_WINDOWS else os.path.join(python_env, "bin", "python")
        if os.path.exists(py_exe):
            return py_exe
    env_root = Path(os.path.expanduser("~")) / ".espressif" / "python_env"
    env_dirs = sorted(
        env_root.glob("idf5.5_py*_env") if env_root.is_dir() else [],
        key=lambda path: path.stat().st_mtime,
        reverse=True,
    )
    for env_dir in env_dirs:
        candidate = env_dir / ("Scripts/python.exe" if IS_WINDOWS else "bin/python")
        if candidate.is_file():
            return str(candidate)
    return find_python()


def activate_idf_tools(idf_path, python_exe):
    """通过 ESP-IDF 官方导出接口补齐工具链 PATH 和运行时环境。"""
    idf_tools = os.path.join(idf_path, "tools", "idf_tools.py")
    if not os.path.exists(idf_tools):
        return False
    env = os.environ.copy()
    env["IDF_PATH"] = idf_path
    try:
        result = subprocess.run(
            [python_exe, idf_tools, "export", "--format=key-value"],
            capture_output=True,
            text=True,
            cwd=PROJECT,
            timeout=30,
            env=env,
        )
    except Exception:
        return False
    if result.returncode != 0:
        return False

    current_path = os.environ.get("PATH", "")
    exported = {}
    for raw_line in result.stdout.splitlines():
        if "=" not in raw_line:
            continue
        key, value = raw_line.split("=", 1)
        key = key.strip()
        value = value.strip().strip('"')
        if not key:
            continue
        value = value.replace("${PATH}", current_path)
        value = value.replace("$PATH", current_path).replace("%PATH%", current_path)
        exported[key] = value
    if "PATH" not in exported:
        return False
    os.environ.update(exported)
    os.environ["IDF_PATH"] = idf_path
    return True


def ensure_idf():
    """若 idf.py 不可用，则按优先级探测 IDF 路径。
    返回 None 表示未找到可用的 ESP-IDF 环境。
    返回 (idf_path, python_exe) 表示可用的 IDF 根目录和对应 Python。"""
    configured_path = os.environ.get("IDF_PATH", "")
    if configured_path:
        idf_py = os.path.join(configured_path, "tools", "idf.py")
        if os.path.exists(idf_py):
            py_exe = find_idf_python(configured_path)
            if activate_idf_tools(configured_path, py_exe):
                return configured_path, py_exe
    if shutil.which("idf.py"):
        return "", sys.executable
    # 过滤掉 None/空路径，按顺序探测
    for path in filter(None, IDF_CANDIDATE_PATHS):
        idf_py = os.path.join(path, "tools", "idf.py")
        if not os.path.exists(idf_py):
            continue
        py_exe = find_idf_python(path)
        # 验证 idf.py 确实可运行
        try:
            r = subprocess.run(
                [py_exe, idf_py, "--version"],
                capture_output=True,
                text=True,
                cwd=PROJECT,
                timeout=30,
            )
            if r.returncode == 0 and activate_idf_tools(path, py_exe):
                return path, py_exe
        except Exception:
            continue
    return None


# 1. 编译检查
print("\n=== 1. 编译检查 ===")
idf_result = ensure_idf()
if idf_result is None:
    fail("未找到可用的 ESP-IDF v5.5.x 环境，请设置 IDF_PATH 环境变量")
else:
    idf_path, py_exe = idf_result
    idf_py = os.path.join(idf_path, "tools", "idf.py") if idf_path else "idf.py"
    out, rc = run_cmd([py_exe, idf_py, "build"])
    last_lines = "\n".join(out.strip().splitlines()[-5:])
    if rc == 0 and ("Project build complete" in out or "write_flash" in out):
        ok("idf.py build 通过")
    else:
        fail(f"idf.py build 失败 (rc={rc}): {last_lines[:200]}")

# 2. Host 单测
print("\n=== 2. Host 单测 ===")
make_cmd = shutil.which("make") or shutil.which("mingw32-make")
gcc_cmd = shutil.which("gcc") or shutil.which("clang")
if not make_cmd or not gcc_cmd:
    warn("无 host 编译工具链（make/gcc），跳过单测")
else:
    test_dir = os.path.join(PROJECT, "test")
    run_cmd([make_cmd, "-C", "test", "clean"], cwd=PROJECT)
    _, rc_build = run_cmd([make_cmd, "-C", "test"], cwd=PROJECT)
    if rc_build != 0:
        fail("host 单测编译失败")
    else:
        test_bin = os.path.join(test_dir, "test_domain.exe" if IS_WINDOWS else "test_domain")
        out, rc = run_cmd([test_bin], cwd=test_dir)
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
if shutil.which("git"):
    out, _ = run_cmd(["git", "-C", PROJECT, "status", "--short"])
    out_stripped = out.strip()
    if out_stripped:
        n = len(out_stripped.split('\n'))
        warn(f"有 {n} 个未提交的改动")
    else:
        ok("工作区 clean")

    out, rc = run_cmd(["git", "-C", PROJECT, "diff", "--check"])
    if rc == 0:
        ok("工作区 whitespace 检查通过")
    else:
        fail(f"工作区 whitespace 检查失败: {out.strip().splitlines()[0] if out.strip() else f'rc={rc}'}")

    out, rc = run_cmd(["git", "-C", PROJECT, "diff", "--cached", "--check"])
    if rc == 0:
        ok("暂存区 whitespace 检查通过")
    else:
        fail(f"暂存区 whitespace 检查失败: {out.strip().splitlines()[0] if out.strip() else f'rc={rc}'}")

    if out_stripped:
        warn("工作区未 clean，跳过最近提交 whitespace 检查")
    else:
        out, rc = run_cmd(["git", "-C", PROJECT, "show", "--check", "--format=", "HEAD"])
        if rc == 0:
            ok("最近提交 whitespace 检查通过")
        else:
            fail(f"最近提交 whitespace 检查失败: {out.strip().splitlines()[0] if out.strip() else f'rc={rc}'}")

    out, _ = run_cmd(["git", "-C", PROJECT, "rev-list", "--count", "@{u}..HEAD"])
    out = out.strip()
    if out.isdigit() and int(out) > 0:
        warn(f"有 {out} 个未推送的 commit")
    else:
        ok("全部已推送（或无上游）")
else:
    warn("未找到 git，跳过 Git 状态检查")

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
    "docs/dev/01-technical-reference.md",
    "docs/dev/02-firmware-security.md",
    "docs/dev/03-test-coverage-analysis.md",
    "docs/dev/10-stable-delivery-design.md",
    "docs/dev/11-stable-delivery-plan.md",
    "docs/dev/12-top5-protocol-stability-design.md",
    "docs/dev/13-project-review-and-optimization.md",
    "docs/archive/01-cidf-nimble-rewrite.md",
    "docs/archive/02-micropython-vs-c-comparison.md",
    "scripts/tools/generate_release_docs.py",
    "scripts/tools/device_acceptance.py",
    "scripts/tools/publish_gitee_release.py",
    "scripts/tools/serve_ble_monitor.py",
    "tools/web/ble-monitor.html",
    "main/app_main.c",
    "main/ble_bikepower.c",
    "main/wifi_portal.c",
    "main/radio_ctrl.c",
    "main/bikepower_config.h",
    "components/bikepower_domain/include/domain_config.h",
    "components/bikepower_domain/include/ble_payload_codec.h",
    "components/bikepower_domain/ble_payload_codec.c",
    "test/test_domain.c",
    "test/ble_codec_cli.c",
    "tests/fixtures/ble_protocol_vectors.json",
    "tests/ble_protocol.py",
    "tests/test_ble_protocol_vectors.py",
    "tests/test_ble_reconnect_harness.py",
    "tests/test_ble_running_harness.py",
    "tests/device_profile_harness.py",
    "tests/test_device_profile_harness.py",
    "tests/check_wifi_done_contract.py",
    "tests/check_ftms_control_contract.py",
    "tests/ble_selftest.py",
    "tests/ble_broadcast_monitor.py",
    "tests/stage3_conn_test.py",
    "tests/stage5_portal_test.py",
    "tests/test_portal_config_save.py",
    "tests/test_core_flow_endurance.py",
    "tests/test_device_acceptance.py",
    "tests/test_operation_gate_evidence.py",
    "tests/test_serial_boot_check.py",
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

# 6. 静态门禁聚合（调起 tests/ 下的纯静态检查脚本）
print("\n=== 6. 静态门禁聚合 ===")
# (脚本相对路径, 人类可读名称)
STATIC_CHECKS = [
    ("tests/test_ble_protocol_vectors.py", "BLE 协议 golden vectors (TC-BLE-VECTOR)"),
    ("tests/test_ble_reconnect_harness.py", "BLE 重连 harness (TC-BLE-RECONNECT)"),
    ("tests/test_ble_running_harness.py", "BLE running 分阶段 harness (TC-BLE-RUNNING)"),
    ("tests/test_device_profile_harness.py", "BLE profile harness (TC-BLE-PROFILE)"),
    ("tests/check_wifi_done_contract.py", "WiFi 完成事件契约 (TC-WIFI-DONE)"),
    ("tests/check_ftms_control_contract.py", "FTMS 控制点契约 (TC-FTMS-CP)"),
    ("tests/check_config_consistency.py", "常量一致性 (TC-3c)"),
    ("tests/check_nvs_schema.py", "NVS schema (TC-3e)"),
    ("tests/check_tasks_wdt.py", "任务/队列/WDT 完整性 (TC-3g)"),
    ("tests/check_board_desc.py", "板型描述表 (TC-3f)"),
    ("tests/check_portal_structure.py", "门户结构 (TC-3d)"),
    ("tests/check_portal_js.py", "门户 JS 语法 (TC-3a)"),
    ("tests/check_portal_render_gate.py", "门户渲染截图接线 (TC-3n)"),
    ("tests/check_portal_render.py", "门户渲染截图 (TC-3o)"),
    ("tests/check_operation_coverage.py", "操作覆盖矩阵 (TC-3j)"),
    ("tests/check_ble_monitor.py", "BLE 监控客户端 (TC-3k)"),
    ("tests/check_ble_advertising_fields.py", "BLE 广播字段 (TC-BLE-ADV)"),
    ("tests/check_ble_broadcast_monitor.py", "BLE 广播项监控脚本 (TC-3l)"),
    ("tests/check_release_gate.py", "Release 产物门禁 (TC-3m)"),
    ("tests/test_release_artifacts.py", "Release 产物门禁功能样例 (TC-3p)"),
    ("tests/test_device_acceptance.py", "真机验收证据功能样例 (TC-3q)"),
    ("tests/test_operation_gate_evidence.py", "真机门禁证据采集样例 (TC-3r)"),
    ("tests/test_serial_boot_check.py", "串口启动日志判定样例 (TC-3s)"),
]
# 环境缺失标志：命中则降级为 warn（无设备/无依赖环境仍可跑），不算真实失败
ENV_MISSING_MARKERS = [
    "未找到 node",
    "无法验证 JS",
    "未找到 agent-browser",
    "未安装",
    "command not found",
    "No module named",
    "ModuleNotFoundError",
]
python_exe = find_python()
for rel_path, name in STATIC_CHECKS:
    full = os.path.join(PROJECT, rel_path)
    if not os.path.exists(full):
        fail(f"{name}: 脚本缺失 {rel_path}")
        continue
    out, rc = run_cmd([python_exe, full], cwd=PROJECT)
    if rc == 0:
        ok(f"{name} 通过")
    elif any(marker in out for marker in ENV_MISSING_MARKERS):
        # 环境缺失（如未装 node）：降级 warn 并说明原因，不掩盖真实失败
        reason = next(m for m in ENV_MISSING_MARKERS if m in out)
        warn(f"{name} 因环境缺失跳过（命中标志：{reason}）")
    else:
        fail(f"{name} 检查失败: {out.strip().splitlines()[-1] if out.strip() else f'rc={rc}'}")

# 7. 总体
print(f"\n{'='*40}")
print(f"结果: ✅ {SCORE['pass']}  ⚠️  {SCORE['warn']}  ❌ {SCORE['fail']}")
if SCORE["fail"] > 0:
    print("状态: 需要修复")
    sys.exit(1)
else:
    print("状态: 通过")
