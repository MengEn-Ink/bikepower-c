"""串口抓取脚本：复位设备并捕获启动日志。"""
import argparse
import sys
import time

import serial

def parse_args():
    parser = argparse.ArgumentParser(description="复位设备并捕获串口日志")
    parser.add_argument("port_pos", nargs="?", help="串口端口（兼容旧版位置参数）")
    parser.add_argument("duration_pos", nargs="?", type=float, help="抓取时长秒数（兼容旧版位置参数）")
    parser.add_argument("-p", "--port", help="串口端口")
    parser.add_argument("-d", "--duration", type=float, help="抓取时长秒数")
    parser.add_argument("-o", "--output", help="日志输出文件；不指定则输出到 stdout")
    args = parser.parse_args()
    port = args.port or args.port_pos or "/dev/ttyACM0"
    duration = args.duration if args.duration is not None else (args.duration_pos if args.duration_pos is not None else 13.0)
    if duration < 0:
        parser.error("duration 必须大于等于 0")
    return port, duration, args.output

port, duration, output = parse_args()

s = serial.Serial(port, 115200, timeout=1)
s.setDTR(False)
s.setRTS(True)
time.sleep(0.1)
s.setRTS(False)
time.sleep(0.1)

t = time.time()
out = open(output, "a", encoding="utf-8") if output else sys.stdout
try:
    while time.time() - t < duration:
        line = s.readline().decode("utf-8", "replace").rstrip()
        if line:
            print(line, file=out, flush=True)
finally:
    if output:
        out.close()
    s.close()
