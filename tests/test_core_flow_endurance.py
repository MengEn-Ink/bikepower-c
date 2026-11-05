#!/usr/bin/env python3
"""核心门户事务与桥接耐久；真实算法由 host gate 验证，mock 不代表硬件。"""
import argparse
import socket
import subprocess
import sys
import time
import traceback
from datetime import datetime
from pathlib import Path
from test_portal_config_save import exercise, wait_ready

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--minutes", type=float, default=3)
    parser.add_argument("--interval", type=float, default=10)
    parser.add_argument("--cycles", type=int, default=0)
    parser.add_argument("--skip-host", action="store_true")
    parser.add_argument("--host-every-minutes", type=float, default=15)
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--report", default="")
    args = parser.parse_args()
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = args.port or sock.getsockname()[1]
    proc = subprocess.Popen([sys.executable, str(ROOT / "tests/mock_server.py"), "--port", str(port)],
                            stdout=subprocess.DEVNULL)
    started = time.monotonic()
    rows, cycles, errors = [], 0, []
    try:
        wait_ready(f"http://127.0.0.1:{port}", proc)
        if not args.skip_host:
            result = subprocess.run(["make", "-C", "test", "run"], cwd=ROOT, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(result.stdout[-2000:] + result.stderr[-2000:])
            rows.append("host 领域 gate 通过")
        while (cycles < args.cycles if args.cycles else time.monotonic() - started < args.minutes * 60):
            exercise(f"http://127.0.0.1:{port}")
            cycles += 1
            rows.append(f"循环 {cycles}：保存、版本冲突、失败、临时测试、校正、零功率、过期、切回模拟、续期通过")
            print(f"[core-flow] PASS cycle={cycles}", flush=True)
            if args.cycles and cycles >= args.cycles:
                break
            time.sleep(min(args.interval, 30, max(0, args.minutes * 60 - (time.monotonic() - started))))
    except Exception as error:
        errors.append(
            f"{type(error).__name__}: {error!r}\n\n```text\n"
            f"{traceback.format_exc().rstrip()}\n```"
        )
    finally:
        proc.terminate()
        proc.wait(timeout=5)
    path = Path(args.report) if args.report else ROOT / "tests/reports" / f"core_flow_endurance_{datetime.now():%Y%m%d_%H%M%S}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("# 核心门户桥接耐久报告\n\n"
                    f"- 结果：{'失败' if errors else '通过'}\n- 循环：{cycles}\n"
                    f"- 实际时长：{time.monotonic() - started:.1f} 秒\n"
                    "- 范围：mock HTTP 与 host 领域逻辑；未覆盖真实无线或物理按键。\n\n"
                    + "\n".join("- " + line for line in rows + errors) + "\n")
    print(f"[core-flow] report: {path}")
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
