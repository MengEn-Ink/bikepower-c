"""串口抓取脚本：复位设备并捕获启动日志。

跨平台兼容：不传 -p 时自动探测串口（Windows COM 口和 Unix /dev/tty* 均支持）。
"""
import argparse
import sys
import time

import serial
from serial.tools import list_ports

BOOT_REQUIRED_MARKERS = (
    "ESP-ROM:esp32c3",
    "rst:",
)
BOOT_FATAL_MARKERS = (
    "Guru Meditation",
    "Backtrace:",
    "Task watchdog",
    "task_wdt:",
    "coredump",
    "Core 0 panic'ed",
    "Core 1 panic'ed",
)


def find_port():
    """自动探测 ESP32-C3 串口设备。"""
    try:
        ports = list(list_ports.comports())
    except Exception:
        return None
    # 优先匹配 ESP32-C3 USB VID:PID = 303A:1001
    for p in ports:
        if p.vid == 0x303A and p.pid == 0x1001:
            return p.device
    # 次优匹配：描述含 espressif 或常见 USB 串口设备名
    for p in ports:
        device = p.device or ""
        desc = (p.description or "").lower()
        hwid = (p.hwid or "").upper()
        if "ESP" in hwid or "espressif" in desc or "jtag/serial" in desc:
            return p.device
        if "ttyACM" in device or "ttyUSB" in device or "usbmodem" in device:
            return p.device
    return None


def parse_args():
    parser = argparse.ArgumentParser(description="复位设备并捕获串口日志")
    parser.add_argument("port_pos", nargs="?", help="串口端口（兼容旧版位置参数）")
    parser.add_argument("duration_pos", nargs="?", type=float, help="抓取时长秒数（兼容旧版位置参数）")
    parser.add_argument("-p", "--port", help="串口端口（不传则自动探测）")
    parser.add_argument("-d", "--duration", type=float, help="抓取时长秒数")
    parser.add_argument("-o", "--output", help="日志输出文件；不指定则输出到 stdout")
    parser.add_argument(
        "--check-boot",
        action="store_true",
        help="抓取后检查 ROM 复位启动标志，并拒绝 panic/WDT/Backtrace/coredump",
    )
    args = parser.parse_args()
    port = args.port or args.port_pos or find_port()
    if not port:
        parser.error("找不到串口设备，请用 -p 指定")
    duration = args.duration if args.duration is not None else (args.duration_pos if args.duration_pos is not None else 13.0)
    if duration < 0:
        parser.error("duration 必须大于等于 0")
    return port, duration, args.output, args.check_boot


def analyze_boot_log(text):
    if not text.strip():
        return False, "启动日志为空"
    for marker in BOOT_FATAL_MARKERS:
        if marker.lower() in text.lower():
            return False, f"启动日志包含致命标志: {marker}"
    missing = [marker for marker in BOOT_REQUIRED_MARKERS if marker not in text]
    if missing:
        return False, "启动日志缺少标志: " + ", ".join(missing)
    return True, "ROM 复位启动标志完整，未发现致命异常"


def capture(port, duration, output):
    serial_port = serial.Serial(port, 115200, timeout=1)
    serial_port.setDTR(False)
    serial_port.setRTS(True)
    time.sleep(0.1)
    serial_port.setRTS(False)
    time.sleep(0.1)

    lines = []
    started_at = time.time()
    out = open(output, "a", encoding="utf-8") if output else sys.stdout
    try:
        while time.time() - started_at < duration:
            line = serial_port.readline().decode("utf-8", "replace").rstrip()
            if line:
                lines.append(line)
                print(line, file=out, flush=True)
    finally:
        if output:
            out.close()
        serial_port.close()
    return "\n".join(lines)


def main():
    port, duration, output, check_boot = parse_args()
    text = capture(port, duration, output)
    if not check_boot:
        return 0
    ok, detail = analyze_boot_log(text)
    print(f"[serial-boot] {'PASS' if ok else 'FAIL'} {detail}", file=sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
