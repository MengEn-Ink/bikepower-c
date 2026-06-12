"""临时串口抓取脚本：复位设备并捕获启动日志。"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem101"
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 13.0

s = serial.Serial(port, 115200, timeout=1)
s.setDTR(False)
s.setRTS(True)
time.sleep(0.1)
s.setRTS(False)
time.sleep(0.1)

t = time.time()
while time.time() - t < duration:
    line = s.readline().decode("utf-8", "replace").rstrip()
    if line:
        print(line)
s.close()
