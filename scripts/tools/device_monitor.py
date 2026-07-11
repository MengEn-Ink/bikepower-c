#!/usr/bin/env python3
"""BikePower C 串口日志捕获与设备监控
用法: python scripts/tools/device_monitor.py [-p PORT] [-l LOG_FILE]

跨平台兼容：Windows 上自动探测 COM 口，Unix 上自动探测 /dev/tty* 设备。
"""
import argparse
import datetime
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("错误: 缺少 pyserial，请安装: pip install pyserial")
    sys.exit(1)

BAUD = 115200

# 串口探测评分（越小优先级越高）
def _score_port(port):
    hwid = (getattr(port, "hwid", "") or "").upper()
    desc = (getattr(port, "description", "") or "").lower()
    device = getattr(port, "device", "") or ""
    vid = getattr(port, "vid", None)
    pid = getattr(port, "pid", None)
    # ESP32-C3 USB VID:PID = 303A:1001
    if vid == 0x303A and pid == 0x1001:
        return 0
    if "303A:1001" in hwid:
        return 0
    if "ESP" in hwid or "espressif" in desc or "jtag/serial" in desc or "usb serial" in desc:
        return 1
    if device.upper().startswith("COM"):
        return 2
    if "ttyACM" in device or "ttyUSB" in device or "usbmodem" in device or "usbserial" in device:
        return 3
    return 9


def find_port():
    """自动探测 ESP32-C3 串口设备。"""
    try:
        ports = list(list_ports.comports())
    except Exception:
        return None
    candidates = [p for p in ports if _score_port(p) < 9]
    if not candidates:
        return None
    candidates.sort(key=_score_port)
    return candidates[0].device


def main():
    parser = argparse.ArgumentParser(description="BikePower C 串口监控")
    parser.add_argument("-p", "--port", help="串口设备路径（不传则自动探测）")
    parser.add_argument("-l", "--log", help="日志文件路径（可选）")
    parser.add_argument("--baud", type=int, default=BAUD, help=f"波特率（默认 {BAUD}）")
    args = parser.parse_args()

    port = args.port or find_port()
    if not port:
        print("错误: 找不到 ESP32-C3 串口，请用 -p 指定")
        sys.exit(1)

    print(f"连接 {port} @ {args.baud} baud...")
    print("按 Ctrl+C 退出")

    log_file = None
    if args.log:
        log_file = open(args.log, "a", encoding="utf-8")

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

                    if "restart" in decoded.lower() or "panic" in decoded.lower() or "abort" in decoded.lower():
                        print("\n警告: 检测到设备重启/异常!\n")
                else:
                    time.sleep(0.05)
    except KeyboardInterrupt:
        print("\n退出")
    finally:
        if log_file:
            log_file.close()


if __name__ == "__main__":
    main()
