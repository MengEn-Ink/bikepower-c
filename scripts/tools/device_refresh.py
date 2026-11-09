#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""设备刷新固定动作：ROM 复位 → 等待广播 → 新进程严格回读。

背景（已实测）：Windows 对已绑定设备的 GATT 服务表有缓存残留，
radio 开关 / PnP / bthserv / pnputil / unpair 均无法刷新；
唯一便宜可靠的动作是让设备 ROM 复位后重新广播，中心端重新枚举。
本脚本把这个动作固化成一条命令，避免每次人肉试错。

严格判据（任一不满足即失败，绝不把过期表当通过）：
  - stale_reasons 为空
  - meta.max_len == 48、meta.schema == 14
  - DIS firmware 含期望版本
  - 可选：core14 / bridge_source / 标准串号 sha256 与基线一致

用法：
  python scripts/tools/device_refresh.py --port COM205 --expect-version 4.3.3
  python scripts/tools/device_refresh.py --address 10:20:BA:CC:3A:EE --no-reset
"""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / "tests"
sys.path.insert(0, str(TESTS))

DEFAULT_ADDRESS = "10:20:BA:CC:3A:EE"
DEFAULT_NAME = "BikePower"
SERIAL_CHAR = "00002a25-0000-1000-8000-00805f9b34fb"
FW_CHAR = "00002a26-0000-1000-8000-00805f9b34fb"

EXIT_OK = 0
EXIT_STALE = 1
EXIT_ENV = 2


def idf_python() -> Path:
    sys.path.insert(0, str(Path(__file__).parent))
    from dev_workflow import Context, default_repo_root, detect_idf, detect_idf_python
    ctx = Context(repo=default_repo_root())
    idf, _ = detect_idf(ctx)
    py, _ = detect_idf_python(ctx, idf)
    if not py:
        raise SystemExit("[refresh] 找不到 IDF Python；先运行 dev_workflow.ps1 doctor")
    return py


def ensure_bleak() -> None:
    """BLE 读取依赖 bleak；当前解释器缺依赖时自动用 IDF Python 重执行。"""
    try:
        import bleak  # noqa: F401
        return
    except ImportError:
        pass
    if os.environ.get("BW_REFRESH_IDF_PY") == "1":
        raise SystemExit("[refresh] 已切换到 IDF Python 但仍缺 bleak，请检查环境")
    py = idf_python()
    env = dict(os.environ, BW_REFRESH_IDF_PY="1")
    print(f"[refresh] 当前解释器缺 bleak，切换到 IDF Python: {py}")
    raise SystemExit(subprocess.call([str(py), str(Path(__file__).resolve())] + sys.argv[1:],
                                      env=env))


def rom_reset(port: str) -> None:
    py = idf_python()
    argv = [str(py), "-m", "esptool", "--chip", "esp32c3", "--port", port, "run"]
    proc = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=180)
    if proc.returncode != 0:
        detail = (proc.stdout or "").strip() + (proc.stderr or "").strip()
        raise SystemExit(f"[refresh] ROM 复位失败: {detail[-400:]}")
    print(f"[refresh] ROM 复位完成 port={port}")


async def validate(address: str, name: str, timeout: int) -> dict:
    from ble_config_e2e import DeviceSession
    from ble_winrt_refresh import connect_validated, StaleCacheError

    result = {"address": address, "at": time.strftime("%Y-%m-%dT%H:%M:%S")}
    session = DeviceSession(name, timeout, address=address, reset_method="none")
    try:
        result["stale_reasons"] = await connect_validated(session)
        meta, groups = await session.read_snapshot()
        result["meta"] = meta
        result["groups_hex"] = {k: v.hex() for k, v in groups.items()}
        fw = bytes(await session.client.read_gatt_char(FW_CHAR))
        result["dis_firmware"] = fw.split(b"\x00")[0].decode("ascii", "replace")
        serial = bytes(await session.client.read_gatt_char(SERIAL_CHAR))
        result["serial_length"] = len(serial)
        result["serial_sha256"] = hashlib.sha256(serial).hexdigest()
        result["core14_mask"] = groups["core"][14]
        result["bridge_source"] = groups["bridge"][1]
        result["status"] = "success"
    except StaleCacheError as exc:
        result.update(status="stale", error=str(exc))
    except Exception as exc:  # noqa: BLE001
        result.update(status="error", error=f"{type(exc).__name__}: {exc}")
    finally:
        try:
            await session.disconnect()
        except Exception:  # noqa: BLE001
            pass
        result["released"] = True
    return result


def check(result: dict, expect_version: str, expect_core14: Optional[int],
          expect_bridge: Optional[int], expect_serial: Optional[str]) -> list:
    problems = []
    if result.get("status") != "success":
        problems.append(f"连接/读取失败: {result.get('error') or result.get('status')}")
        return problems
    meta = result.get("meta") or {}
    if meta.get("max_len") != 48:
        problems.append(f"meta.max_len={meta.get('max_len')} != 48")
    if meta.get("schema") != 14:
        problems.append(f"meta.schema={meta.get('schema')} != 14")
    fw = result.get("dis_firmware", "")
    if expect_version and expect_version not in fw:
        problems.append(f"DIS firmware={fw} 不含期望版本 {expect_version}")
    if expect_core14 is not None and result.get("core14_mask") != expect_core14:
        problems.append(f"core14={result.get('core14_mask')} != {expect_core14}")
    if expect_bridge is not None and result.get("bridge_source") != expect_bridge:
        problems.append(f"bridge_source={result.get('bridge_source')} != {expect_bridge}")
    if expect_serial and result.get("serial_sha256") != expect_serial:
        problems.append(f"串号 sha256={result.get('serial_sha256')} 与基线不一致")
    return problems


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="device_refresh",
                                description="ROM 复位 + 新进程严格回读")
    p.add_argument("--port", default="COM205", help="串口，用于 ROM 复位")
    p.add_argument("--address", default=DEFAULT_ADDRESS)
    p.add_argument("--name", default=DEFAULT_NAME)
    p.add_argument("--expect-version", default="", help="DIS 固件名需包含的版本，如 4.3.3")
    p.add_argument("--expect-core14", type=int)
    p.add_argument("--expect-bridge", type=int)
    p.add_argument("--expect-serial-sha256")
    p.add_argument("--wait", type=float, default=8.0, help="复位后等待广播恢复的秒数")
    p.add_argument("--timeout", type=int, default=30)
    p.add_argument("--no-reset", action="store_true", help="跳过 ROM 复位，只做回读")
    p.add_argument("--out", default=str(ROOT / "tests" / "reports" / "device-refresh.json"))
    args = p.parse_args(argv)
    ensure_bleak()

    if not args.no_reset:
        rom_reset(args.port)
        time.sleep(args.wait)

    result = asyncio.run(validate(args.address, args.name, args.timeout))
    problems = check(result, args.expect_version, args.expect_core14,
                     args.expect_bridge, args.expect_serial_sha256)
    result["problems"] = problems

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(result, ensure_ascii=False))
    if problems:
        print(f"[refresh] FAIL 严格回读不通过（{len(problems)} 项）：")
        for item in problems:
            print(f"   - {item}")
        return EXIT_STALE
    print("[refresh] OK 严格回读通过（服务表新鲜、meta/版本/配置一致）")
    return EXIT_OK


if __name__ == "__main__":
    raise SystemExit(main())
