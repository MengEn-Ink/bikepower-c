#!/usr/bin/env python3
"""BikePower C 串口日志捕获与设备监控
用法: python3 scripts/tools/device_monitor.py [-p PORT] [-l LOG_FILE]
"""

import argparse
import datetime
import serial
import sys
import time

SERIAL_PORTS = ["/dev/cu.usbmodem101", "/dev/cu.usbmodem1101",
               "/dev/cu.usbserial-0001", "/dev/ttyUSB0", "/dev/ttyACM0"]
BAUD = 115200

def find_port():
    for p in SERIAL_PORTS:
        try:
            s = serial.Serial(p, BAUD, timeout=1)
            s.close()
            return p
        except (serial.SerialException, OSError):
            continue
    return None

def main():
    parser = argparse.ArgumentParser(description="BikePower C 串口监控")
    parser.add_argument("-p", "--port", help="串口设备路径")
    parser.add_argument("-l", "--log", help="日志文件路径（可选）")
    parser.add_argument("--baud", type=int, default=BAUD, help=f"波特率（默认 {BAUD}）")
    args = parser.parse_args()

    port = args.port or find_port()
    if not port:
        print("错误: 找不到 ESP32-C3 串口")
        sys.exit(1)

    print(f"连接 {port} @ {args.baud} baud...")
    print("按 Ctrl+C 退出")

    log_file = None
    if args.log:
        log_file = open(args.log, "a")

    try:
        with serial.Serial(port, args.baud, timeout=0.1) as ser:
            while True:
                line = ser.readline()
                if line:
                    decoded = line.decode("utf-8", errors="replace").strip()
                    ts = datetime.datetime.now().strftime("%H:%M:%S")
                    print(f"[{ts}] {decoded}")
                    if log_file:
                        log_file.write(f"[{ts}] {decoded}\n")
                        log_file.flush()

                    # 检测关键事件
                    if "restart" in decoded.lower() or "panic" in decoded.lower() or "abort" in decoded.lower():
                        print(f"\n⚠️  检测到设备重启/异常!\n")
                else:
                    time.sleep(0.05)
    except KeyboardInterrupt:
        print("\n退出")
    finally:
        if log_file:
            log_file.close()

if __name__ == "__main__":
    main()
