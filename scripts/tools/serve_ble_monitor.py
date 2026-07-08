#!/usr/bin/env python3
"""启动 BikePower BLE 浏览器监控页面的本地服务（tools/web）。"""
import argparse
import functools
import os
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WEB_DIR = ROOT / "tools" / "web"


class NoCacheHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description="BikePower BLE 浏览器监控本地服务")
    parser.add_argument("--host", default="127.0.0.1", help="监听地址，默认 127.0.0.1")
    parser.add_argument("--port", type=int, default=8766, help="监听端口，默认 8766")
    args = parser.parse_args()

    if not WEB_DIR.is_dir():
        raise SystemExit(f"目录不存在: {WEB_DIR}")

    handler = functools.partial(NoCacheHandler, directory=str(WEB_DIR))
    server = ThreadingHTTPServer((args.host, args.port), handler)
    url = f"http://{args.host}:{args.port}/ble-monitor.html"
    print(f"BikePower BLE monitor: {url}")
    print(f"web 目录: {WEB_DIR}")
    print("请用 Chrome / Edge 打开；Web Bluetooth 首次连接需要用户点击授权。")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n已停止")
    finally:
        server.server_close()


if __name__ == "__main__":
    os.chdir(ROOT)
    main()
