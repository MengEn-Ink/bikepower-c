#!/usr/bin/env python3
"""ESP32-C3 串口日志采集守护进程

特性:
  - 后台常驻，持续抓取串口日志，每行加完整挂钟时间戳
  - 非阻塞读取 (in_waiting + reset_input_buffer)，确保实时写入
  - fcntl 文件锁防多实例，PID 自管理
  - 串口被占用时（烧录/其他脚本）自动退让，空闲后自动重连
  - 不触发设备复位，不影响运行中的设备
  - 日志按会话命名: tests/logs/device_YYYYMMDD_HHMMSS.log，保留最近 5 个
  - 关键字实时高亮（配网/射频/错误/心跳等 ESP-IDF 日志级别分类）
  - 支持 SIGTERM/SIGINT 优雅退出
  - --daemon 双 fork 后台化
  - 跨平台：macOS / Linux 自动探测串口 + 烧录检测

用法:
  python3 tests/serial_daemon.py                           # 前台运行（自动探测串口）
  python3 tests/serial_daemon.py --daemon                  # 后台守护模式
  python3 tests/serial_daemon.py -p /dev/cu.usbmodem101    # 指定端口
  python3 tests/serial_daemon.py --no-color                # 禁用颜色
  DAEMON_ARGS='--no-color' bash scripts/log_daemon.sh start  # 通过包装脚本启动
"""

import argparse
import datetime
import fcntl
import os
import re
import signal
import subprocess
import sys
import time
import traceback
from collections import deque

PID_FILE = "/tmp/bikepower_log_daemon.pid"
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")
DAEMON_LOG_FILE = os.path.join(LOG_DIR, "daemon.log")
MAX_LOG_FILES = 5
CRASH_CONTEXT_LINES = 40
SCAN_INTERVAL = 5
SERIAL_BAUD = 115200
SERIAL_TIMEOUT = 2
BUFFER_SIZE = 8192
REBOOT_PATTERNS = [
    r"ESP-ROM:",
    r"\brst:0x[0-9a-fA-F]+",
    r"BikePower C 启动",
]
CRASH_PATTERNS = [
    r"panic",
    r"abort",
    r"Backtrace",
    r"Guru Meditation",
    r"task_wdt",
    r"watchdog",
    r"WDT",
]

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("ERROR: 需要 pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(1)

# ===== 关键字高亮分类 =====
HIGHLIGHTS = [
    # ESP-IDF 日志级别
    (r"^\s*E \(\d+\)", "\033[31m", "ERROR"),
    (r"^\s*W \(\d+\)", "\033[33m", "WARN"),
    (r"^\s*I \(\d+\)", "\033[90m", "INFO"),
    (r"^\s*D \(\d+\)", "\033[36m", "DEBUG"),
    (r"^\s*V \(\d+\)", "\033[90m", "VERBOSE"),
    # 业务关键字
    (r"panic|abort|Backtrace|Guru Meditation|WDT|reboot|assert", "\033[31m", "PANIC"),
    (r"PORTAL:|首页请求|GET /|POST /|HTTP|配网|门户", "\033[32m", "配网"),
    (r"RADIO:|SoftAP|STA 连接|got ip|共存|coex|WiFi", "\033[33m", "射频"),
    (r"BLE|NimBLE|广播|连接|GATT|通知|FTMS|CPS|HRS|CSC", "\033[34m", "BLE"),
    (r"心跳|state=|堆=|heap|free", "\033[90m", "心跳"),
    (r"NVS|config|保存|加载|持久化|migrate", "\033[35m", "配置"),
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

_running = True


def signal_handler(signum, frame):
    global _running
    name = signal.Signals(signum).name if hasattr(signal, "Signals") else str(signum)
    daemon_log("收到信号 %s，正在优雅退出..." % name)
    _running = False


def timestamp():
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def ensure_log_dir():
    os.makedirs(LOG_DIR, exist_ok=True)


def get_log_filepath():
    """会话级日志文件：device_YYYYMMDD_HHMMSS.log"""
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    return os.path.join(LOG_DIR, "device_%s.log" % ts)


def get_crash_snapshot_filepath():
    """故障快照文件：crash_YYYYMMDD_HHMMSS.log"""
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    return os.path.join(LOG_DIR, "crash_%s.log" % ts)


def daemon_log(msg):
    """守护进程自身日志：输出到 stdout + daemon.log"""
    line = "[%s] [DAEMON] %s" % (timestamp(), msg)
    print(line, flush=True)
    try:
        with open(DAEMON_LOG_FILE, "a") as f:
            f.write(line + "\n")
    except OSError:
        pass


def rotate_logs():
    """保留最近 MAX_LOG_FILES 个设备日志文件，删除多余旧文件"""
    log_files = []
    try:
        for fname in os.listdir(LOG_DIR):
            full = os.path.join(LOG_DIR, fname)
            if fname.startswith("device_") and fname.endswith(".log"):
                log_files.append((os.path.getmtime(full), full))
    except OSError:
        return
    log_files.sort(key=lambda x: x[0], reverse=True)
    for _, full in log_files[MAX_LOG_FILES:]:
        try:
            os.remove(full)
        except OSError:
            pass


def is_reboot_line(line):
    return any(re.search(pat, line) for pat in REBOOT_PATTERNS)


def is_crash_line(line):
    return any(re.search(pat, line, re.IGNORECASE) for pat in CRASH_PATTERNS)


def append_plain_line(filepath, line):
    try:
        with open(filepath, "a") as f:
            f.write(line + "\n")
    except OSError:
        pass


def append_reboot_separator(filepath):
    line = "[%s] ----- DEVICE REBOOT / BOOT BANNER -----" % timestamp()
    append_plain_line(filepath, line)


def write_crash_snapshot(trigger_line, recent_lines):
    ensure_log_dir()
    snapshot = get_crash_snapshot_filepath()
    try:
        with open(snapshot, "w") as f:
            f.write("CRASH SNAPSHOT %s\n" % timestamp())
            f.write("TRIGGER: %s\n" % trigger_line)
            f.write("--- recent context ---\n")
            for item in recent_lines:
                f.write(item + "\n")
            f.write(trigger_line + "\n")
        daemon_log("故障快照已保存: %s" % snapshot)
    except OSError as e:
        daemon_log("故障快照写入失败: %s" % e)


def process_serial_line(log_path, line, use_color=True, quiet=False,
                        recent_lines=None, crash_keys=None, split_on_reboot=False,
                        crash_snapshot=True):
    """处理单行串口日志，必要时插入重启分隔线、切文件并生成故障快照。"""
    if recent_lines is None:
        recent_lines = deque(maxlen=CRASH_CONTEXT_LINES)
    if crash_keys is None:
        crash_keys = set()

    if is_reboot_line(line):
        append_reboot_separator(log_path)
        if split_on_reboot:
            log_path = get_log_filepath()
            ensure_log_dir()
            append_reboot_separator(log_path)
            rotate_logs()
            daemon_log("检测到设备重启，切换日志文件: %s" % log_path)

    log_line(log_path, line, use_color, quiet)

    if crash_snapshot and is_crash_line(line) and line not in crash_keys:
        crash_keys.add(line)
        write_crash_snapshot(line, list(recent_lines))

    recent_lines.append(line)
    return log_path


def write_pid():
    ensure_log_dir()
    with open(PID_FILE, "w") as f:
        f.write(str(os.getpid()))


def remove_pid():
    try:
        os.remove(PID_FILE)
    except OSError:
        pass


def acquire_lock():
    """fcntl 文件锁，防止多个守护进程同时运行"""
    try:
        lock_fd = os.open(PID_FILE, os.O_RDWR | os.O_CREAT, 0o644)
        fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        os.write(lock_fd, str(os.getpid()).encode())
        os.ftruncate(lock_fd, 0)
        os.lseek(lock_fd, 0, os.SEEK_SET)
        os.write(lock_fd, str(os.getpid()).encode())
        return lock_fd
    except (IOError, OSError):
        try:
            with open(PID_FILE, "r") as f:
                existing_pid = f.read().strip()
            print("[%s] [DAEMON] 已有守护进程运行 (PID=%s)" % (timestamp(), existing_pid), flush=True)
        except (IOError, OSError):
            print("[%s] [DAEMON] 无法获取锁，守护进程可能已在运行" % timestamp(), flush=True)
        sys.exit(0)


def daemonize():
    """双 fork 后台守护进程化"""
    pid = os.fork()
    if pid > 0:
        ensure_log_dir()
        daemon_log("后台守护进程已启动 (PID=%d)" % pid)
        daemon_log("管理命令: bash scripts/log_daemon.sh {status,stop,logs}")
        sys.exit(0)

    os.setsid()
    pid = os.fork()
    if pid > 0:
        sys.exit(0)

    os.umask(0o022)
    null = os.open(os.devnull, os.O_RDWR)
    os.dup2(null, sys.stdin.fileno())
    os.dup2(null, sys.stdout.fileno())
    os.dup2(null, sys.stderr.fileno())
    os.close(null)


def detect_port():
    """自动探测 ESP32-C3 串口。优先匹配乐鑫 USB-JTAG VID/PID，再回退按设备名匹配。"""
    try:
        ports = list(list_ports.comports())
    except Exception:
        return None
    for p in ports:
        if p.vid == 0x303A and p.pid == 0x1001:
            return p.device
    for p in ports:
        dev = p.device
        if "ttyACM" in dev or "ttyUSB" in dev or "usbmodem" in dev or "usbserial" in dev:
            return dev
    return None


def is_flash_in_progress(port):
    """检测是否有 esptool/idf.py 烧录进程在运行（针对本串口）。

    烧录时 esptool 需要独占串口，守护进程必须主动让出。
    跨平台：macOS/Linux 通用 ps 检测。
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
        if ("esptool" in cmd or "idf.py" in cmd) and any(p in cmd for p in port_names):
            return True
    return False


def highlight(line, use_color):
    """关键字高亮，返回 (类别, 着色行)"""
    if not use_color:
        return None, line
    for pat, color, cat in HIGHLIGHTS:
        if re.search(pat, line, re.IGNORECASE):
            return cat, "%s%s%s" % (color, line, C.RESET)
    return None, line


def log_line(filepath, line, use_color=True, quiet=False):
    """写入一行日志到文件 + 终端高亮输出。

    文件格式: [2026-07-01 19:23:59] <原始行>
    终端格式: [2026-07-01 19:23:59] [CATEGORY] <高亮行>
    """
    line = line.rstrip("\r\n")
    if not line:
        return
    ts = timestamp()
    # 文件输出：纯文本，无 ANSI
    entry = "[%s] %s" % (ts, line)
    try:
        with open(filepath, "a") as f:
            f.write(entry + "\n")
    except OSError:
        pass

    if quiet:
        return

    # 终端输出：带颜色高亮
    cat, colored = highlight(line, use_color)
    if cat:
        if use_color:
            print("%s[%s]%s %s[%s]%s %s" % (C.GRAY, ts, C.RESET, C.YELLOW, cat, C.RESET, colored), flush=True)
        else:
            print("[%s] [%s] %s" % (ts, cat, line), flush=True)
    else:
        if use_color:
            print("%s[%s]%s %s" % (C.GRAY, ts, C.RESET, colored), flush=True)
        else:
            print("[%s] %s" % (ts, line), flush=True)


def read_serial(ser, log_path, use_color=True, quiet=False,
                recent_lines=None, crash_keys=None, split_on_reboot=False,
                crash_snapshot=True):
    """非阻塞读取串口缓冲区并写入日志（参考 esp32-power 稳定模式）"""
    try:
        waiting = ser.in_waiting or 0
        if waiting > 0:
            data = ser.read(min(waiting, BUFFER_SIZE))
            text = data.decode("utf-8", errors="replace")
            for line in text.split("\n"):
                line = line.strip()
                if line:
                    log_path = process_serial_line(
                        log_path, line, use_color, quiet, recent_lines, crash_keys,
                        split_on_reboot, crash_snapshot,
                    )
            try:
                ser.reset_input_buffer()
            except Exception:
                pass
        return log_path
    except (OSError, Exception) as e:
        msg = str(e)
        if "EIO" in msg or "device disconnected" in msg.lower():
            raise
        daemon_log("读取错误: %s" % e)
        return log_path


def run_loop(port_prefer=None, use_color=True, quiet=False,
             split_on_reboot=False, crash_snapshot=True):
    """主循环：扫描串口 → 连接 → 采集日志 → 断开后重试"""
    global _running
    current_port = port_prefer
    ser = None
    reconnect_delay = SCAN_INTERVAL
    log_path = get_log_filepath()
    recent_lines = deque(maxlen=CRASH_CONTEXT_LINES)
    crash_keys = set()
    last_port_check = 0
    flash_backoff_until = 0
    last_flash_logged = False

    ensure_log_dir()
    rotate_logs()
    daemon_log("日志守护进程已启动")
    daemon_log("日志目录: %s" % LOG_DIR)
    daemon_log("PID: %d" % os.getpid())

    while _running:
        now = time.time()

        # 自动探测模式下设备未接入时持续重试
        if current_port is None:
            if time.time() < flash_backoff_until:
                time.sleep(SCAN_INTERVAL)
                continue

            flash_active = is_flash_in_progress(current_port or "")
            if flash_active:
                if ser:
                    try:
                        ser.close()
                    except Exception:
                        pass
                    ser = None
                if not last_flash_logged:
                    daemon_log("检测到烧录进行中，暂停串口采集")
                    last_flash_logged = True
                flash_backoff_until = time.time() + 10
                time.sleep(SCAN_INTERVAL)
                continue
            else:
                last_flash_logged = False
                flash_backoff_until = 0

            if now - last_port_check >= SCAN_INTERVAL:
                last_port_check = now
                found = detect_port()
                if found:
                    current_port = found
                    daemon_log("探测到串口: %s" % current_port)

        if current_port is None:
            time.sleep(SCAN_INTERVAL)
            continue

        # 尝试连接串口
        if ser is None:
            if is_flash_in_progress(current_port):
                time.sleep(1)
                continue
            try:
                log_path = get_log_filepath()
                ensure_log_dir()
                with open(log_path, "a") as f:
                    f.write("")
                ser = serial.Serial(current_port, SERIAL_BAUD, timeout=SERIAL_TIMEOUT)
                rotate_logs()
                daemon_log("串口已连接: %s → %s" % (current_port, log_path))
            except Exception as e:
                daemon_log("串口连接失败 (%s): %s" % (current_port, e))
                if not port_prefer:
                    current_port = None
                time.sleep(reconnect_delay)
                continue

        # 持有串口期间检测烧录，主动让出
        if ser is not None and is_flash_in_progress(current_port):
            if not last_flash_logged:
                daemon_log("检测到烧录进程，主动让出串口")
                last_flash_logged = True
            flash_backoff_until = time.time() + 30
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(2)
            continue
        else:
            last_flash_logged = False

        # 读取串口数据
        if ser is not None:
            try:
                log_path = read_serial(
                    ser, log_path, use_color, quiet, recent_lines, crash_keys,
                    split_on_reboot, crash_snapshot,
                )
            except (OSError, Exception) as e:
                msg = str(e)
                if "EIO" in msg or "disconnected" in msg.lower():
                    daemon_log("串口已断开: %s" % current_port)
                else:
                    daemon_log("串口错误 (%s): %s" % (current_port, msg))
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None

            # 定期检查端口是否仍然存在
            if now - last_port_check >= SCAN_INTERVAL:
                last_port_check = now
                if not os.path.exists(current_port):
                    daemon_log("串口设备已移除: %s" % current_port)
                    try:
                        ser.close()
                    except Exception:
                        pass
                    ser = None
                    if not port_prefer:
                        current_port = None
                    time.sleep(SCAN_INTERVAL)
                    continue

        time.sleep(0.1)  # 主循环节拍 100ms，确保实时性

    # 退出清理
    if ser:
        try:
            ser.close()
        except Exception:
            pass
    remove_pid()
    daemon_log("日志守护进程已退出")


def parse_args():
    p = argparse.ArgumentParser(description="ESP32-C3 串口日志采集守护进程")
    p.add_argument("--daemon", action="store_true", help="后台守护模式（双 fork）")
    p.add_argument("-p", "--port", default=None, help="串口端口，不传则自动探测")
    p.add_argument("-b", "--baud", type=int, default=SERIAL_BAUD, help="波特率（默认 %d）" % SERIAL_BAUD)
    p.add_argument("--no-color", action="store_true", help="禁用 ANSI 颜色")
    p.add_argument("--quiet", action="store_true", help="不实时打印，只写文件")
    p.add_argument("--split-on-reboot", action="store_true", help="检测到设备启动 banner 时切换到新的 device_*.log")
    p.add_argument("--no-crash-snapshot", action="store_true", help="禁用 panic/WDT/Backtrace 故障上下文快照")
    return p.parse_args()


def main():
    global _running
    args = parse_args()

    if args.daemon:
        daemonize()

    lock_fd = acquire_lock()

    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGINT, signal_handler)

    use_color = not args.no_color and sys.stdout.isatty()

    try:
        run_loop(
            port_prefer=args.port,
            use_color=use_color,
            quiet=args.quiet,
            split_on_reboot=args.split_on_reboot,
            crash_snapshot=not args.no_crash_snapshot,
        )
    except KeyboardInterrupt:
        print("[%s] 收到 KeyboardInterrupt，正在退出..." % timestamp(), flush=True)
    except Exception as e:
        traceback.print_exc()
    finally:
        try:
            os.close(lock_fd)
        except Exception:
            pass
        remove_pid()


if __name__ == "__main__":
    main()
