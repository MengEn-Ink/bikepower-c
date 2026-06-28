#!/usr/bin/env python3
"""OTA 测试串口日志抓取脚本（v3.1.6）

功能：
  - 持续抓取串口日志到 tests/logs/serial_<时间戳>.log
  - 实时过滤关键字高亮显示（OTA/SNTP/HTTPS/STA/错误等）
  - 自动生成分析摘要 tests/logs/analysis_<时间戳>.md
  - 支持 Ctrl+C 优雅退出

用法：
  python3 tests/ota_serial_monitor.py -p /dev/ttyACM0 -d 600
  python3 tests/ota_serial_monitor.py -p /dev/ttyACM0  # 默认 1800s
"""
import argparse
import os
import re
import sys
import time
from datetime import datetime

try:
    import serial
except ImportError:
    print("ERROR: 需要安装 pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(1)

# ===== 关键字分类 =====
KEYWORDS = {
    "OTA流程": [
        r"GET /ota", r"POST /ota/check", r"POST /ota/upgrade", r"GET /ota/status",
        r"异步检查", r"异步触发", r"ota_check_task", r"ota_upgrade_task",
        r"ota_chk", r"ota_up", r"版本检查", r"升级任务",
    ],
    "WiFi/STA": [
        r"STA 连接", r"STA 连接成功", r"STA 连接失败", r"sta_event",
        r"radio_ctrl_sta", r"DHCP server assigned", r"got ip", r"sta:",
    ],
    "SNTP时间": [r"SNTP", r"时间未同步", r"时间同步", r"sntp", r"ntp"],
    "HTTPS/证书": [r"SSL", r"TLS", r"certificate", r"cert", r"crt_bundle", r"https"],
    "版本信息": [r"latest\.json", r"HTTP \d{3}", r"version", r"版本", r"3\.1\.\d", r"has_update"],
    "下载进度": [r"下载", r"写入", r"esp_https_ota", r"ota_begin", r"ota_write", r"ota_end"],
    "错误/异常": [
        r"E \(", r"ERROR", r"error in accept", r"error in send", r"error in recv",
        r"fail", r"FAIL", r"panic", r"abort", r"Backtrace", r"Guru", r"WDT",
        r"assert", r"EMFILE", r"ENOMEM",
    ],
    "系统状态": [r"state=", r"空闲堆", r"free heap", r"APP_EVT_WIFI", r"portal_done"],
}

# ===== 颜色 =====
class C:
    RESET = "\033[0m"
    RED = "\033[31m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    BLUE = "\033[34m"
    MAGENTA = "\033[35m"
    CYAN = "\033[36m"
    GRAY = "\033[90m"

def color_for_category(cat):
    return {
        "OTA流程": C.CYAN,
        "WiFi/STA": C.BLUE,
        "SNTP时间": C.MAGENTA,
        "HTTPS/证书": C.MAGENTA,
        "版本信息": C.GREEN,
        "下载进度": C.YELLOW,
        "错误/异常": C.RED,
        "系统状态": C.GRAY,
    }.get(cat, C.RESET)

def parse_args():
    p = argparse.ArgumentParser(description="OTA 测试串口日志抓取")
    p.add_argument("-p", "--port", default="/dev/ttyACM0", help="串口端口（默认 /dev/ttyACM0）")
    p.add_argument("-d", "--duration", type=float, default=1800, help="抓取时长秒（默认 1800）")
    p.add_argument("--no-color", action="store_true", help="禁用颜色输出")
    return p.parse_args()

def main():
    args = parse_args()
    use_color = not args.no_color and sys.stdout.isatty()

    # 创建 logs 目录
    logs_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")
    os.makedirs(logs_dir, exist_ok=True)

    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    serial_log = os.path.join(logs_dir, f"serial_{ts}.log")
    analysis_md = os.path.join(logs_dir, f"analysis_{ts}.md")

    print(f"{C.GREEN}=== OTA 串口监控启动 ==={C.RESET}" if use_color else "=== OTA 串口监控启动 ===")
    print(f"端口: {args.port}")
    print(f"时长: {args.duration}s")
    print(f"完整日志: {serial_log}")
    print(f"分析报告: {analysis_md}")
    print(f"按 Ctrl+C 提前结束并生成分析报告")
    print()

    # 分类统计
    stats = {cat: [] for cat in KEYWORDS}
    all_lines = []

    try:
        s = serial.Serial(args.port, 115200, timeout=0.5)
    except Exception as e:
        print(f"{C.RED}串口打开失败: {e}{C.RESET}" if use_color else f"串口打开失败: {e}", file=sys.stderr)
        sys.exit(1)

    # 触发硬复位（DTR/RTS 序列），抓启动日志
    print(f"{C.YELLOW}触发设备复位...{C.RESET}" if use_color else "触发设备复位...")
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.1)
    s.setRTS(False)
    time.sleep(0.1)
    s.flushInput()  # 清空输入缓冲

    start = time.time()
    f_log = open(serial_log, "wb")

    def c(text, color):
        return f"{color}{text}{C.RESET}" if use_color else text

    try:
        while time.time() - start < args.duration:
            data = s.read(4096)
            if not data:
                continue
            f_log.write(data)
            f_log.flush()

            text = data.decode("utf-8", "replace")
            for line in text.split("\n"):
                line = line.rstrip()
                if not line:
                    continue
                all_lines.append(line)

                # 分类匹配
                matched = False
                for cat, patterns in KEYWORDS.items():
                    for pat in patterns:
                        if re.search(pat, line, re.IGNORECASE):
                            stats[cat].append(line)
                            # 实时高亮打印
                            ts_str = datetime.now().strftime("%H:%M:%S")
                            print(f"{C.GRAY}[{ts_str}]{C.RESET} {c(f'[{cat}]', color_for_category(cat))} {line}")
                            matched = True
                            break
                    if matched:
                        break
    except KeyboardInterrupt:
        print(f"\n{C.YELLOW}用户中断，生成分析报告...{C.RESET}" if use_color else "\n用户中断，生成分析报告...")
    finally:
        f_log.close()
        s.close()

    # ===== 生成分析报告 =====
    with open(analysis_md, "w", encoding="utf-8") as f:
        f.write(f"# OTA 测试串口日志分析报告\n\n")
        f.write(f"- 抓取时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"- 端口: {args.port}\n")
        f.write(f"- 时长: {time.time()-start:.1f}s\n")
        f.write(f"- 完整日志: `{serial_log}`\n\n")

        f.write("## 分类统计\n\n")
        f.write("| 分类 | 命中数 |\n|------|--------|\n")
        for cat in KEYWORDS:
            f.write(f"| {cat} | {len(stats[cat])} |\n")
        f.write("\n")

        f.write("## 关键流程时间线\n\n")
        # 找关键节点
        key_patterns = [
            (r"GET /ota", "访问 /ota 页面"),
            (r"POST /ota/check", "触发版本检查"),
            (r"异步检查任务已启动", "异步任务启动"),
            (r"异步检查：STA 连接", "STA 开始连接"),
            (r"STA 连接成功|异步检查：STA 连接成功", "STA 连接成功"),
            (r"SNTP 同步成功", "SNTP 时间同步成功"),
            (r"SNTP 同步失败", "SNTP 时间同步失败"),
            (r"latest\.json", "拉取 latest.json"),
            (r"HTTP 200", "HTTP 200 成功"),
            (r"HTTP [45]\d\d", "HTTP 错误状态码"),
            (r"异步检查完成", "版本检查完成"),
            (r"POST /ota/upgrade", "触发升级"),
            (r"升级完成", "升级完成"),
            (r"esp_restart|REBOOT", "设备重启"),
        ]
        for pat, desc in key_patterns:
            hits = [l for l in all_lines if re.search(pat, l, re.IGNORECASE)]
            if hits:
                f.write(f"### {desc}\n")
                for h in hits[:5]:
                    f.write(f"- `{h}`\n")
                if len(hits) > 5:
                    f.write(f"- ... 共 {len(hits)} 条\n")
                f.write("\n")

        f.write("## 错误与异常\n\n")
        if stats["错误/异常"]:
            # 去重
            seen = set()
            unique_errors = []
            for e in stats["错误/异常"]:
                if e not in seen:
                    seen.add(e)
                    unique_errors.append(e)
            f.write(f"共 {len(stats['错误/异常'])} 条错误日志（{len(unique_errors)} 种唯一）:\n\n")
            for e in unique_errors[:20]:
                f.write(f"- `{e}`\n")
        else:
            f.write("无错误日志 ✅\n")
        f.write("\n")

        f.write("## fd 耗尽检查\n\n")
        fd_errors = [l for l in all_lines if "error in accept" in l or "error in send" in l or "error in recv" in l]
        if fd_errors:
            f.write(f"⚠️ 发现 {len(fd_errors)} 条 socket 错误:\n\n")
            for e in fd_errors[:10]:
                f.write(f"- `{e}`\n")
            f.write("\n**根因**: httpd max_open_sockets=7 耗尽，需 Connection:close 或异步化\n")
        else:
            f.write("无 socket 错误 ✅\n")
        f.write("\n")

        f.write("## 结论\n\n")
        ota_check_ok = bool([l for l in all_lines if "异步检查完成" in l])
        sntp_ok = bool([l for l in all_lines if "SNTP 同步成功" in l])
        https_ok = bool([l for l in all_lines if "HTTP 200" in l and "latest" in " ".join(all_lines).lower()])
        fd_ok = len(fd_errors) == 0

        f.write("| 检查项 | 状态 |\n|--------|------|\n")
        f.write(f"| /ota 页面访问 | {'✅' if [l for l in all_lines if 'GET /ota' in l] else '❌ 未访问'} |\n")
        f.write(f"| 异步任务启动 | {'✅' if [l for l in all_lines if '异步检查任务已启动' in l] else '❌ 未启动'} |\n")
        f.write(f"| STA 连接成功 | {'✅' if sntp_ok or [l for l in all_lines if 'STA 连接成功' in l] else '❌'} |\n")
        f.write(f"| SNTP 同步 | {'✅' if sntp_ok else '❌ 未同步'} |\n")
        f.write(f"| HTTPS 拉取 | {'✅' if https_ok else '❌ 未拉取'} |\n")
        f.write(f"| 版本检查完成 | {'✅' if ota_check_ok else '❌ 未完成'} |\n")
        f.write(f"| fd 耗尽 | {'✅ 无' if fd_ok else '⚠️ 有错误'} |\n")

    print(f"\n{C.GREEN}=== 分析报告已生成 ==={C.RESET}" if use_color else "\n=== 分析报告已生成 ===")
    print(f"完整日志: {serial_log}")
    print(f"分析报告: {analysis_md}")

if __name__ == "__main__":
    main()
