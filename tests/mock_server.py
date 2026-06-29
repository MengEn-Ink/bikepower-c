#!/usr/bin/env python3
"""BikePower 配网门户 Mock Server（v3.2.0 前后端分离重构版）

复刻设备端 wifi_portal.c 的全部 /api/xxx JSON 接口与首页静态资源，
让前端 main/web/ 三件套在本地不烧设备即可调试。

用法:
  python3 tests/mock_server.py                     # 默认 http://127.0.0.1:8080
  python3 tests/mock_server.py --port 8000
  python3 tests/mock_server.py --bind 0.0.0.0      # 局域网内手机可访问

行为说明:
  - GET /                 返回 main/web/index.html（自动内联 app.css + app.js）
  - GET /favicon.ico      204
  - GET /api/state        JSON 设备状态
  - GET /api/time         纯数字倒计时（每请求自减，最低 0）
  - GET /api/renew        重置倒计时为 180
  - GET /api/mem_stats    JSON 假内存数据
  - POST /api/config      {"ok":true}（保存到运行时状态）
  - POST /api/draft       {"ok":true}（仅运行时）
  - POST /api/close-wifi  {"ok":true}
  - POST /api/ota/check   触发异步检查（2s 后转 done，has_update=true）
  - GET /api/ota/status   状态机查询
  - POST /api/ota/upgrade {"ok":true}
  - POST /api/wifi/scan   触发异步扫描（1.5s 后转 done，返回 3 个假 AP）
  - GET /api/wifi/scan/status
  - POST /api/wifi/save   保存 SSID/PSK，2s 后转 done/connected
  - GET /api/wifi/connect/status
"""
import argparse
import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB_DIR = os.path.join(REPO_ROOT, "main", "web")

MOCK_STATE = {
    "device_name": "BikePower-Mock",
    "firmware_version": "3.2.0-mock",
    "power": 150,
    "cadence": 80,
    "heartrate": 130,
    "mode": "steady",
    "profile_weight": 70,
    "profile_ftp": 200,
    "profile_thr_hr": 170,
    "profile_ride_type": "coco",
    "remaining_sec": 180,
    "closing": False,
    "home_wifi_ssid": "",
    "pacers": [
        {"id": "coco", "name": "Coco", "wkg_low": 1.5, "wkg_high": 2.0},
        {"id": "flat", "name": "Flat Road", "wkg_low": 2.5, "wkg_high": 3.0},
        {"id": "hill", "name": "Hill", "wkg_low": 3.5, "wkg_high": 4.5},
    ],
}

LOCK = threading.Lock()
REMAINING_SEC = [180]
OTA_CHECK = {"state": "idle", "version": "", "changelog": "", "error": "", "has_update": False, "size": 0}
WIFI_SCAN = {"state": "idle", "networks": [], "error": ""}
WIFI_CONN = {"state": "idle", "ssid": "", "ip": "", "error": ""}


def _inline_html():
    """读取 index.html 并替换占位符为 app.css / app.js 内容（与 inline_static.py 一致）。"""
    with open(os.path.join(WEB_DIR, "index.html"), encoding="utf-8") as f:
        html = f.read()
    with open(os.path.join(WEB_DIR, "app.css"), encoding="utf-8") as f:
        css = f.read()
    with open(os.path.join(WEB_DIR, "app.js"), encoding="utf-8") as f:
        js = f.read()
    html = html.replace("<!-- INLINE_CSS -->", "<style>\n" + css + "\n</style>")
    html = html.replace("<!-- INLINE_JS -->", "<script>\n" + js + "\n</script>")
    return html.encode("utf-8")


def _tick_remaining():
    """每次读取倒计时自减 1（最低 0），模拟设备端 deadline 流逝。"""
    with LOCK:
        if REMAINING_SEC[0] > 0:
            REMAINING_SEC[0] -= 1
        return REMAINING_SEC[0]


def _ota_check_thread():
    """模拟设备端 ota_check_task：2s running → done has_update=true。"""
    with LOCK:
        OTA_CHECK["state"] = "running"
        OTA_CHECK["version"] = ""
        OTA_CHECK["error"] = ""
    time.sleep(2.0)
    with LOCK:
        OTA_CHECK["state"] = "done"
        OTA_CHECK["version"] = "3.2.1"
        OTA_CHECK["changelog"] = "Mock 更新说明：修复若干 bug。"
        OTA_CHECK["has_update"] = True
        OTA_CHECK["size"] = 655360


def _wifi_scan_thread():
    """模拟设备端 wifi_scan_task：1.5s running → done，返回 3 个假 AP。"""
    with LOCK:
        WIFI_SCAN["state"] = "running"
        WIFI_SCAN["error"] = ""
    time.sleep(1.5)
    with LOCK:
        WIFI_SCAN["state"] = "done"
        WIFI_SCAN["networks"] = [
            {"ssid": "MockHome-5G", "rssi": -45, "auth": 4},
            {"ssid": "MockHome-2.4G", "rssi": -56, "auth": 4},
            {"ssid": "Neighbor-Free", "rssi": -78, "auth": 0},
        ]


def _wifi_connect_thread(ssid):
    """模拟设备端 wifi_connect_task：2s running → done connected=true。"""
    with LOCK:
        WIFI_CONN["state"] = "running"
        WIFI_CONN["ssid"] = ssid
        WIFI_CONN["error"] = ""
    time.sleep(2.0)
    with LOCK:
        WIFI_CONN["state"] = "done"
        WIFI_CONN["ip"] = "192.168.1.42"
        MOCK_STATE["home_wifi_ssid"] = ssid


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        # 简化日志，避免默认 stderr 噪音
        print(f"[mock] {self.command} {self.path} -> {args[1] if len(args) > 1 else ''}")

    def _send(self, status, body=b"", content_type="text/plain; charset=utf-8", extra_headers=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        if extra_headers:
            for k, v in extra_headers.items():
                self.send_header(k, v)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _send_json(self, obj, status=200):
        body = json.dumps(obj).encode("utf-8")
        self._send(status, body, "application/json")

    def _send_text(self, text, status=200):
        self._send(status, text.encode("utf-8"), "text/plain; charset=utf-8")

    def _read_body(self):
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length <= 0:
            return b""
        return self.rfile.read(length)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/" or path == "/index.html":
            self._send(200, _inline_html(), "text/html; charset=utf-8")
            return
        if path == "/favicon.ico":
            self._send(204)
            return
        if path == "/api/state":
            with LOCK:
                MOCK_STATE["remaining_sec"] = REMAINING_SEC[0]
            self._send_json(MOCK_STATE)
            return
        if path == "/api/time":
            sec = _tick_remaining()
            self._send_text(str(sec))
            return
        if path == "/api/renew":
            with LOCK:
                REMAINING_SEC[0] = 180
            self._send_text("180")
            return
        if path == "/api/mem_stats":
            self._send_json({"free": 120000, "min_free": 98000})
            return
        if path == "/api/ota/status":
            with LOCK:
                st = OTA_CHECK["state"]
                resp = {"status": st, "current": MOCK_STATE["firmware_version"]}
                if st == "done":
                    resp.update({
                        "has_update": OTA_CHECK["has_update"],
                        "version": OTA_CHECK["version"],
                        "size": OTA_CHECK["size"],
                        "changelog": OTA_CHECK["changelog"],
                    })
                elif st == "error":
                    resp["error"] = OTA_CHECK["error"]
            self._send_json(resp)
            return
        if path == "/api/wifi/scan/status":
            with LOCK:
                st = WIFI_SCAN["state"]
                resp = {"status": st}
                if st == "done":
                    resp["count"] = len(WIFI_SCAN["networks"])
                    resp["networks"] = WIFI_SCAN["networks"]
                elif st == "error":
                    resp["error"] = WIFI_SCAN["error"]
            self._send_json(resp)
            return
        if path == "/api/wifi/connect/status":
            with LOCK:
                st = WIFI_CONN["state"]
                resp = {"status": st}
                if st == "done":
                    resp["connected"] = True
                    resp["ssid"] = WIFI_CONN["ssid"]
                    resp["ip"] = WIFI_CONN["ip"]
                elif st == "error":
                    resp["connected"] = False
                    resp["error"] = WIFI_CONN["error"]
            self._send_json(resp)
            return
        # Captive Portal 检测端点：302 回首页
        if path in ("/generate_204", "/gen_204", "/hotspot-detect.html",
                    "/library/test/success.html", "/ncsi.txt",
                    "/connecttest.txt", "/canonical.html",
                    "/success.txt", "/redirect",
                    "/kindle-wifi/wifistub.html"):
            self._send(302, b"", "text/html", {"Location": "/"})
            return
        self._send_text("Not Found", 404)

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        body = self._read_body()

        if path == "/api/config":
            try:
                data = json.loads(body) if body else {}
            except Exception:
                data = {}
            with LOCK:
                for k in ("device_name", "mode", "power", "cadence",
                          "heartrate", "profile_weight", "profile_ftp",
                          "profile_thr_hr", "profile_ride_type"):
                    if k in data:
                        MOCK_STATE[k] = data[k]
            self._send_json({"ok": True})
            return
        if path == "/api/draft":
            try:
                data = json.loads(body) if body else {}
            except Exception:
                data = {}
            with LOCK:
                for k in ("device_name", "mode", "power", "cadence",
                          "heartrate", "profile_weight", "profile_ftp",
                          "profile_thr_hr", "profile_ride_type"):
                    if k in data:
                        MOCK_STATE[k] = data[k]
            self._send_json({"ok": True})
            return
        if path == "/api/close-wifi":
            with LOCK:
                MOCK_STATE["closing"] = True
            self._send_json({"ok": True})
            return
        if path == "/api/ota/check":
            with LOCK:
                if OTA_CHECK["state"] == "running":
                    self._send_json({"status": "running",
                                     "current": MOCK_STATE["firmware_version"]})
                    return
            threading.Thread(target=_ota_check_thread, daemon=True).start()
            self._send_json({"status": "started",
                             "current": MOCK_STATE["firmware_version"]})
            return
        if path == "/api/ota/upgrade":
            self._send_json({"ok": True})
            return
        if path == "/api/wifi/scan":
            with LOCK:
                if WIFI_SCAN["state"] == "running":
                    self._send_json({"status": "running"})
                    return
            threading.Thread(target=_wifi_scan_thread, daemon=True).start()
            self._send_json({"status": "started"})
            return
        if path == "/api/wifi/save":
            try:
                data = json.loads(body) if body else {}
            except Exception:
                data = {}
            ssid = data.get("ssid", "")
            psk = data.get("psk", "")
            if not ssid:
                self._send_json({"status": "error", "error": "请输入 WiFi 名称"})
                return
            with LOCK:
                if WIFI_CONN["state"] == "running":
                    self._send_json({"status": "running"})
                    return
            threading.Thread(target=_wifi_connect_thread, args=(ssid,), daemon=True).start()
            self._send_json({"status": "started"})
            return
        self._send_text("Not Found", 404)


def main():
    parser = argparse.ArgumentParser(description="BikePower 配网门户 Mock Server")
    parser.add_argument("--bind", default="127.0.0.1", help="监听地址（默认 127.0.0.1）")
    parser.add_argument("--port", type=int, default=8080, help="监听端口（默认 8080）")
    args = parser.parse_args()

    if not os.path.isdir(WEB_DIR):
        print(f"FAIL: web 目录不存在 {WEB_DIR}", flush=True)
        return 1

    httpd = HTTPServer((args.bind, args.port), Handler)
    print(f"Mock server: http://{args.bind}:{args.port}")
    print(f"  web 目录: {WEB_DIR}")
    print(f"  倒计时: 180s 自动递减，/api/renew 重置")
    print(f"  OTA: POST /api/ota/check 2s 后转 done has_update=true")
    print(f"  WiFi: POST /api/wifi/scan 1.5s 后返回 3 个假 AP")
    print(f"  WiFi: POST /api/wifi/save 2s 后连接成功")
    print(f"  Ctrl+C 退出")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[mock] 已停止")
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
