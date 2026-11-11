#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""诊断串口长时观察：不复位设备，持续记录连接/断开/订阅/加密诊断事件。

仅用于真机心率根因排查。全量日志落盘，并在结束时按服务名聚合订阅集合。

用法:
  python tests/diag_serial_watch.py -p COM205 -d 90 -o tests/reports/.../watch.log
"""
import argparse
import re
import sys
import time
from collections import defaultdict

try:
    import serial
except ImportError:
    print("[watch] 缺少 pyserial", file=sys.stderr)
    raise

SUB_RE = re.compile(r"DIAG: subscribe handle=(\d+) svc=(\S+) cur_notify=(\d+) cur_indicate=(\d+) reason=(\d+)")
CONNECT_RE = re.compile(r"DIAG: connect handle=(\d+) status=(\d+)")
DISCONNECT_RE = re.compile(r"DIAG: disconnect handle=(\d+) reason=(\d+)")
ENC_RE = re.compile(r"DIAG: enc_change conn=(\d+) status=(\d+) encrypted=(\d+)")
KEY_RE = re.compile(r"DIAG:|connect|disconnect|enc_change|panic|Guru|Backtrace|WDT|heart|HRS")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port", default="COM205")
    ap.add_argument("-d", "--duration", type=float, default=90.0)
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    subs = defaultdict(list)  # svc -> list of (handle, notify, indicate, reason, conn_order)
    connects = []
    disconnects = []
    encs = []
    order = 0

    sp = serial.Serial(args.port, 115200, timeout=1)
    sp.setDTR(False)
    sp.setRTS(False)
    sp.flushInput()
    out = open(args.output, "a", encoding="utf-8")
    started = time.time()
    print(f"[watch] 串口已打开 {args.port}（DTR/RTS 已拉低），观察 {args.duration}s -> {args.output}", file=sys.stderr)
    last_beat = 0.0
    try:
        while time.time() - started < args.duration:
            line = sp.readline().decode("utf-8", "replace").rstrip()
            if not line:
                now = time.time()
                if now - last_beat >= 15:
                    last_beat = now
                    print(f"[watch] 心跳 {now - started:.0f}s 暂无新行", file=sys.stderr)
                continue
            out.write(line + "\n")
            out.flush()
            m = SUB_RE.search(line)
            if m:
                handle, svc, notify, indicate, reason = m.groups()
                subs[svc].append((int(handle), int(notify), int(indicate), int(reason)))
                print(line, file=sys.stderr)
                continue
            m = CONNECT_RE.search(line)
            if m:
                connects.append((int(m.group(1)), int(m.group(2))))
                order += 1
                print(line, file=sys.stderr)
                continue
            m = DISCONNECT_RE.search(line)
            if m:
                disconnects.append((int(m.group(1)), int(m.group(2))))
                print(line, file=sys.stderr)
                continue
            m = ENC_RE.search(line)
            if m:
                encs.append((int(m.group(1)), int(m.group(2)), int(m.group(3))))
                print(line, file=sys.stderr)
                continue
            if KEY_RE.search(line):
                print(line, file=sys.stderr)
    finally:
        out.close()
        sp.close()

    print("\n===== 诊断事件聚合 =====", file=sys.stderr)
    print(f"connect: {len(connects)} 次 {connects}", file=sys.stderr)
    print(f"disconnect: {len(disconnects)} 次 {disconnects}", file=sys.stderr)
    print(f"enc_change: {len(encs)} 次 {encs}", file=sys.stderr)
    print("订阅服务集合:", file=sys.stderr)
    for svc, records in subs.items():
        print(f"  {svc}: {len(records)} 条 -> {records}", file=sys.stderr)
    print(f"\n[watch] HRS 订阅次数: {len(subs.get('HRS', []))}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
