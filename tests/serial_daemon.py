#!/usr/bin/env python3
"""串口日志守护进程（v3.1.6）

特性：
  - 后台常驻，持续抓取串口日志，每行加挂钟时间戳
  - 串口被占用时（烧录/其他脚本）自动退让，空闲后自动重连
  - 不触发设备复位，不影响运行中的设备
  - 日志按日滚动：tests/logs/serial_<YYYYMMDD>.log
  - 关键字实时高亮（WiFi 扫描/连接/OTA/错误等）
  - 支持 SIGTERM/SIGINT 优雅退出
  - 跨平台：端口不传则自动探测（Linux /dev/ttyACM*、macOS /dev/cu.usbmodem*）

用法：
  # 前台运行（自动探测串口）
  python3 tests/serial_daemon.py
  # 指定端口
  python3 tests/serial_daemon.py -p /dev/cu.usbmodem101
  # 后台守护
  nohup python3 tests/serial_daemon.py > /tmp/serial_daemon.out 2>&1 &
  # 停止
  pkill -f serial_daemon.py
"""
import argparse
import os
import re
import signal
import subprocess
import sys
import time
from datetime import datetime

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("ERROR: 需要 pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(1)

# ===== 关键字高亮分类 =====
HIGHLIGHTS = [
    (r"wifi_scan|/wifi/scan|WiFi 扫描|扫描完成|扫描失败|scan_", "\033[36m", "扫描"),
    (r"wifi_conn|/wifi/save|/wifi/connect|测试连接|异步测试", "\033[35m", "连接"),
    (r"radio_ctrl_wifi_scan|scan 切 APSTA|scan 创建 STA", "\033[36m", "射频扫描"),
    (r"GET /ota|POST /ota|异步检查|异步触发|ota_check|ota_upgrade", "\033[34m", "OTA"),
    (r"STA 连接|sta_event|got ip|DHCP server assigned", "\033[34m", "STA"),
    (r"SNTP|时间同步|sntp", "\033[35m", "SNTP"),
    (r"SSL|TLS|certificate|crt_bundle|https", "\033[35m", "TLS"),
    (r"E \(|ERROR|error in accept|error in send|error in recv|panic|abort|Backtrace|Guru|WDT|EMFILE|ENOMEM|fail|FAIL", "\033[31m", "错误"),
    (r"PORTAL:|首页请求|GET /more|ui/module|POST /config|草稿保存", "\033[32m", "配网"),
    (r"RADIO:|SoftAP|配网|共存|WiFi 配网窗口", "\033[33m", "射频"),
    (r"心跳|state=|堆=", "\033[90m", "心跳"),
]

class C:
    RESET = "\033[0m"
    RED = "\033[31m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    BLUE = "\033[34m"
    MAGENTA = "\033[35m"
    CYAN = "\033[36m"
    GRAY = "\033[90m"

running = True

def signal_handler(sig, frame):
    global running
    running = False

def parse_args():
    p = argparse.ArgumentParser(description="串口日志守护进程")
    p.add_argument("-p", "--port", default=None, help="串口端口，不传则自动探测")
    p.add_argument("-b", "--baud", type=int, default=115200, help="波特率")
    p.add_argument("--no-color", action="store_true", help="禁用颜色")
    p.add_argument("--quiet", action="store_true", help="不实时打印，只写文件")
    return p.parse_args()


def detect_port():
    """自动探测 ESP32-C3 串口。优先匹配乐鑫 USB-JTAG VID/PID，再回退按设备名匹配。"""
    ports = list(list_ports.comports())
    for p in ports:
        if p.vid == 0x303A and p.pid == 0x1001:
            return p.device
    for p in ports:
        dev = p.device
        if "ttyACM" in dev or "ttyUSB" in dev or "usbmodem" in dev or "usbserial" in dev:
            return dev
    return None

def is_port_available(port):
    """检测串口是否被其他进程占用（烧录时 esptool 会独占）"""
    try:
        import fcntl
        f = open(port, "rb")
        try:
            fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.flock(f.fileno(), fcntl.LOCK_UN)
            f.close()
            return True
        except (IOError, OSError):
            f.close()
            return False
    except Exception:
        return True  # 无法判定时尝试打开


def is_flash_in_progress(port):
    """检测是否有 esptool/idf.py 烧录进程在运行（针对本串口）。

    烧录时 esptool 需要独占串口，守护进程必须主动让出。
    跨平台实现：用 `ps -axo pid=,command=` 列出全部进程命令行（Linux/macOS 通用），
    匹配 esptool 或 idf.py flash 且命令行包含本端口。
    """
    my_pid = os.getpid()
    base = os.path.basename(port)
    port_names = [port, base]
    try:
        out = subprocess.run(
            ["ps", "-axo", "pid=,command="],
            capture_output=True, text=True, timeout=3,
        ).stdout
    except Exception:
        return False
    for line in out.splitlines():
        line = line.strip()
        if not line:
            continue
        head, _, cmd = line.partition(" ")
        if not head.isdigit() or int(head) == my_pid:
            continue
        # 匹配烧录进程：esptool / idf.py flash，且命令行含本端口
        if ("esptool" in cmd or "idf.py" in cmd) and any(p in cmd for p in port_names):
            return True
        if "idf.py" in cmd and "flash" in cmd and any(p in cmd for p in port_names):
            return True
    return False

def highlight(line, use_color):
    """关键字高亮，返回 (类别, 着色行)"""
    if not use_color:
        return None, line
    for pat, color, cat in HIGHLIGHTS:
        if re.search(pat, line, re.IGNORECASE):
            return cat, f"{color}{line}{C.RESET}"
    return None, line

def open_log(logs_dir):
    """按日打开日志文件"""
    os.makedirs(logs_dir, exist_ok=True)
    today = datetime.now().strftime("%Y%m%d")
    path = os.path.join(logs_dir, f"serial_{today}.log")
    return open(path, "ab")

def main():
    global running
    args = parse_args()
    use_color = not args.no_color and sys.stdout.isatty()
    logs_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # 端口：显式指定则固定；否则启动时探测，未插设备时循环内持续重试探测
    fixed_port = args.port
    port = fixed_port or detect_port()

    print(f"{C.GREEN}=== 串口守护进程启动 ==={C.RESET}" if use_color else "=== 串口守护进程启动 ===")
    print(f"端口: {port or '未探测到，等待设备接入'}{'（自动探测）' if not fixed_port else ''}  波特: {args.baud}")
    print(f"日志目录: {logs_dir}")
    print(f"模式: {'静默写文件' if args.quiet else '实时高亮+写文件'}")
    print(f"串口被占用时自动退让，5 秒后重试")
    print(f"PID={os.getpid()}，停止: pkill -f serial_daemon.py")
    print()

    f_log = None
    log_date = None
    ser = None
    reconnect_delay = 5

    while running:
        # 自动探测模式下设备未接入时持续重试探测
        if port is None:
            port = detect_port()
            if port is None:
                time.sleep(reconnect_delay)
                continue
            print(f"探测到串口: {port}")

        # 按日切换日志文件
        today = datetime.now().strftime("%Y%m%d")
        if log_date != today:
            if f_log:
                f_log.close()
            f_log = open_log(logs_dir)
            log_date = today

        # 尝试打开串口（被占用则退让）
        if ser is None:
            if is_flash_in_progress(port):
                # 烧录进行中，主动等待让出串口
                time.sleep(1)
                continue
            if not is_port_available(port):
                time.sleep(reconnect_delay)
                continue
            try:
                ser = serial.Serial(port, args.baud, timeout=0.5)
                ts = datetime.now().strftime("%H:%M:%S")
                msg = f"[{ts}] [DAEMON] 串口已连接 {port}"
                print(msg)
                f_log.write((msg + "\n").encode("utf-8"))
                f_log.flush()
            except Exception as e:
                ts = datetime.now().strftime("%H:%M:%S")
                msg = f"[{ts}] [DAEMON] 串口打开失败: {e}，{reconnect_delay}s 后重试"
                print(msg, file=sys.stderr)
                f_log.write((msg + "\n").encode("utf-8"))
                f_log.flush()
                if not fixed_port:
                    port = None  # 自动探测模式下重新探测
                time.sleep(reconnect_delay)
                continue

        # 持有串口期间检测烧录需求，主动让出
        if ser is not None and is_flash_in_progress(port):
            ts = datetime.now().strftime("%H:%M:%S")
            msg = f"[{ts}] [DAEMON] 检测到烧录进程，主动让出串口"
            print(msg)
            f_log.write((msg + "\n").encode("utf-8"))
            f_log.flush()
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(2)  # 等烧录完成
            continue

        # 读取数据
        try:
            data = ser.read(2048)
            if not data:
                continue
        except Exception as e:
            # 串口异常（设备拔出/重置），关闭后重连
            ts = datetime.now().strftime("%H:%M:%S")
            msg = f"[{ts}] [DAEMON] 串口异常: {e}，重连中"
            print(msg, file=sys.stderr)
            f_log.write((msg + "\n").encode("utf-8"))
            f_log.flush()
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            if not fixed_port:
                port = None  # 自动探测模式下设备可能换了节点，重新探测
            time.sleep(2)
            continue

        f_log.write(data)
        f_log.flush()

        if args.quiet:
            continue

        # 实时高亮打印
        text = data.decode("utf-8", "replace")
        for line in text.split("\n"):
            line = line.rstrip()
            if not line:
                continue
            ts = datetime.now().strftime("%H:%M:%S")
            cat, colored = highlight(line, use_color)
            if cat:
                print(f"{C.GRAY}[{ts}]{C.RESET} {C.YELLOW}[{cat}]{C.RESET} {colored}" if use_color else f"[{ts}] [{cat}] {line}")
            else:
                print(f"{C.GRAY}[{ts}]{C.RESET} {colored}" if use_color else f"[{ts}] {line}")

    # 退出清理
    ts = datetime.now().strftime("%H:%M:%S")
    msg = f"[{ts}] [DAEMON] 守护进程退出"
    print(msg)
    if f_log:
        f_log.write((msg + "\n").encode("utf-8"))
        f_log.close()
    if ser:
        try:
            ser.close()
        except Exception:
            pass

if __name__ == "__main__":
    main()
