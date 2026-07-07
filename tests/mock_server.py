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
  - POST /api/renew       重置倒计时为 180
  - GET /api/mem_stats    JSON 假内存数据
  - POST /api/config      {"ok":true}（保存到运行时状态）
  - POST /api/close-wifi  {"ok":true}
  - /api/device-templates* 和 /api/ble-scan* 模拟设备信息模板库与 BLE 扫描
"""
import argparse
import json
import os
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB_DIR = os.path.join(REPO_ROOT, "main", "web")

MOCK_STATE = {
    "device_name": "BikePower-Mock",
    "firmware_version": "4.0.3-mock",
    "firmware_name": "generic-c3-v4.0.3-stable4.0.0-fix4.0.3-g6e90860",
    "board": "generic-c3",
    "stable_version": "4.0.0",
    "fix_version": "4.0.3",
    "git_sha": "6e90860",
    "build_time": "2026-07-07 15:50:00 CST",
    "sport_profile": "running",
    "broadcast_mask": 7,
    "power": 150,
    "cadence": 80,
    "heartrate": 130,
    "run_speed_x100": 1000,
    "run_pace_float_sec": 10,
    "run_cadence": 170,
    "run_cadence_float": 4,
    "mode": "steady",
    "power_float": 5,
    "cadence_float": 2,
    "hr_float": 2,
    "power_step": 20,
    "profile_weight": 70,
    "profile_ftp": 200,
    "profile_thr_hr": 170,
    "profile_ride_type": "coco",
    "custom_wkg_low": 25,
    "custom_wkg_high": 30,
    "custom_pacer_name": "自定义",
    "led_silent": 0,
    "led_wait_mode": 2,
    "led_wait_color": 2,
    "led_conn_mode": 1,
    "led_conn_color": 1,
    "led_wifi_mode": 2,
    "led_wifi_color": 4,
    "led_confirm_mode": 3,
    "led_confirm_color": 3,
    "led_error_mode": 3,
    "led_error_color": 0,
    "remaining_sec": 180,
    "closing": False,
    "hr_locked": False,
    "pacers": [
        {"id": "coco", "name": "Coco", "wkg_low": 2.4, "wkg_high": 2.8},
        {"id": "yumi", "name": "Yumi", "wkg_low": 2.7, "wkg_high": 3.1},
        {"id": "jacques", "name": "Jacques", "wkg_low": 3.0, "wkg_high": 3.5},
        {"id": "genie", "name": "Genie", "wkg_low": 3.5, "wkg_high": 3.9},
        {"id": "constance", "name": "Constance", "wkg_low": 4.0, "wkg_high": 4.4},
        {"id": "custom", "name": "自定义", "wkg_low": 2.5, "wkg_high": 3.0, "custom": True},
    ],
}

LOCK = threading.Lock()
REMAINING_SEC = [180]
TEMPLATES = []
SCAN_DEVICES = [
    {
        "id": 0,
        "alias": "",
        "adv_name": "Magene P325",
        "device_name": "",
        "manufacturer": "",
        "model": "",
        "firmware": "",
        "hardware": "",
        "software": "",
        "addr": "AA:BB:CC:DD:EE:FF",
        "addr_type": 1,
        "last_rssi": -54,
        "service_flags": 5,
    },
    {
        "id": 1,
        "alias": "",
        "adv_name": "Zwift Hub",
        "device_name": "",
        "manufacturer": "",
        "model": "",
        "firmware": "",
        "hardware": "",
        "software": "",
        "addr": "12:34:56:78:9A:BC",
        "addr_type": 0,
        "last_rssi": -68,
        "service_flags": 37,
    },
    {
        "id": 2,
        "alias": "",
        "adv_name": "Office Keyboard",
        "device_name": "",
        "manufacturer": "",
        "model": "",
        "firmware": "",
        "hardware": "",
        "software": "",
        "addr": "66:55:44:33:22:11",
        "addr_type": 0,
        "last_rssi": -48,
        "service_flags": 0,
    },
    {
        "id": 3,
        "alias": "",
        "adv_name": "Polar H10",
        "device_name": "",
        "manufacturer": "",
        "model": "",
        "firmware": "",
        "hardware": "",
        "software": "",
        "addr": "20:21:22:23:24:25",
        "addr_type": 0,
        "last_rssi": -61,
        "service_flags": 8,
    },
]
SCANNING = [False]

LIMITS = {
    "power": (0, 2000),
    "cadence": (20, 120),
    "heartrate": (60, 200),
    "power_float": (0, 100),
    "cadence_float": (0, 20),
    "hr_float": (0, 20),
    "power_step": (5, 50),
    "profile_weight": (40, 150),
    "profile_ftp": (50, 500),
    "profile_thr_hr": (100, 220),
    "custom_wkg_low": (10, 70),
    "custom_wkg_high": (10, 70),
    "run_speed_x100": (300, 2500),
    "run_pace_float_sec": (0, 60),
    "run_cadence": (120, 220),
    "run_cadence_float": (0, 20),
    "led_wait_mode": (0, 5),
    "led_wait_color": (0, 6),
    "led_conn_mode": (0, 5),
    "led_conn_color": (0, 6),
    "led_wifi_mode": (0, 5),
    "led_wifi_color": (0, 6),
    "led_confirm_mode": (0, 5),
    "led_confirm_color": (0, 6),
    "led_error_mode": (0, 5),
    "led_error_color": (0, 6),
}

ENUM_VALUES = {
    "sport_profile": {"cycling", "running"},
    "mode": {"steady", "road", "erg"},
    "profile_ride_type": {"coco", "yumi", "jacques", "genie", "constance", "custom"},
}


def _clamp(value, lo, hi):
    return min(max(value, lo), hi)


def _apply_config_field(key, value):
    if key in LIMITS:
        if not isinstance(value, int):
            return
        lo, hi = LIMITS[key]
        MOCK_STATE[key] = _clamp(value, lo, hi)
        return
    if key in ENUM_VALUES:
        if isinstance(value, str) and value in ENUM_VALUES[key]:
            MOCK_STATE[key] = value
        return
    if key == "led_silent":
        if isinstance(value, int):
            MOCK_STATE[key] = 1 if value else 0
        return
    if key == "broadcast_mask":
        if isinstance(value, int) and 1 <= (value & 7) <= 7:
            MOCK_STATE[key] = value & 7
        return
    if key == "hr_lock":
        MOCK_STATE["hr_locked"] = bool(value)
        return
    MOCK_STATE[key] = value


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
        if path == "/api/mem_stats":
            self._send_json({"free": 120000, "min_free": 98000})
            return
        if path == "/api/device-templates":
            self._send_json({"max": 5, "count": len(TEMPLATES), "items": TEMPLATES})
            return
        if path == "/api/ble-scan/results":
            self._send_json({"scanning": SCANNING[0], "devices": SCAN_DEVICES})
            SCANNING[0] = False
            return
        if path == "/.well-known/captive-portal":
            self._send(200, b'{"captive":true,"user-portal-url":"http://192.168.4.1/"}',
                       "application/captive+json", {})
            return
        # Captive Portal 检测端点：303 回首页，带短 body 兼容 iOS CNA
        if path in ("/generate_204", "/gen_204", "/hotspot-detect.html",
                    "/library/test/success.html", "/ncsi.txt",
                    "/connecttest.txt", "/canonical.html",
                    "/success.txt", "/redirect",
                    "/kindle-wifi/wifistub.html"):
            self._send(303, b"Redirect to the captive portal", "text/plain; charset=utf-8", {"Location": "/"})
            return
        self._send(303, b"Redirect to the captive portal", "text/plain; charset=utf-8", {"Location": "/"})

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        body = self._read_body()

        if path == "/api/config":
            try:
                data = json.loads(body) if body else {}
            except Exception:
                data = {}
            with LOCK:
                for k in ("device_name", "sport_profile", "mode", "power", "cadence",
                          "heartrate", "run_speed_x100", "run_pace_float_sec",
                          "run_cadence", "run_cadence_float",
                          "broadcast_mask",
                          "power_float", "cadence_float", "hr_float",
                          "power_step",
                          "profile_weight", "profile_ftp",
                          "profile_thr_hr", "profile_ride_type",
                          "custom_wkg_low", "custom_wkg_high", "custom_pacer_name",
                          "led_silent",
                          "led_wait_mode", "led_wait_color",
                          "led_conn_mode", "led_conn_color",
                          "led_wifi_mode", "led_wifi_color",
                          "led_confirm_mode", "led_confirm_color",
                          "led_error_mode", "led_error_color",
                          "closing"):
                    if k in data:
                        _apply_config_field(k, data[k])
                for p in MOCK_STATE["pacers"]:
                    if p.get("id") == "custom":
                        if MOCK_STATE.get("custom_wkg_low", 25) > MOCK_STATE.get("custom_wkg_high", 30):
                            MOCK_STATE["custom_wkg_high"] = MOCK_STATE["custom_wkg_low"]
                        p["name"] = MOCK_STATE.get("custom_pacer_name") or "自定义"
                        p["wkg_low"] = MOCK_STATE.get("custom_wkg_low", 25) / 10
                        p["wkg_high"] = MOCK_STATE.get("custom_wkg_high", 30) / 10
                if "hr_lock" in data:
                    _apply_config_field("hr_lock", data["hr_lock"])
            self._send_json({"ok": True})
            return
        if path == "/api/renew":
            with LOCK:
                REMAINING_SEC[0] = 180
            self._send_text("180")
            return
        if path == "/api/close-wifi":
            with LOCK:
                MOCK_STATE["closing"] = True
            self._send_json({"ok": True})
            return
        if path == "/api/device-templates/save":
            try:
                data = json.loads(body) if body else {}
            except Exception:
                data = {}
            if len(TEMPLATES) >= 5:
                self._send_json({"ok": False, "error": "模板已满，请先删除旧模板"})
                return
            tpl = dict(data)
            tpl["id"] = len(TEMPLATES)
            if not tpl.get("alias"):
                tpl["alias"] = tpl.get("device_name") or tpl.get("adv_name") or tpl.get("addr") or "未命名设备"
            TEMPLATES.append(tpl)
            self._send_json({"ok": True, "id": tpl["id"]})
            return
        if path == "/api/device-templates/delete":
            try:
                data = json.loads(body) if body else {}
            except Exception:
                data = {}
            tid = int(data.get("id", -1))
            if 0 <= tid < len(TEMPLATES):
                TEMPLATES.pop(tid)
                for i, tpl in enumerate(TEMPLATES):
                    tpl["id"] = i
                self._send_json({"ok": True})
            else:
                self._send_json({"ok": False, "error": "模板不存在"})
            return
        if path == "/api/device-templates/apply":
            try:
                data = json.loads(body) if body else {}
            except Exception:
                data = {}
            tid = int(data.get("id", -1))
            if 0 <= tid < len(TEMPLATES):
                MOCK_STATE["device_name"] = TEMPLATES[tid].get("device_name") or TEMPLATES[tid].get("adv_name") or MOCK_STATE["device_name"]
                self._send_json({"ok": True})
            else:
                self._send_json({"ok": False, "error": "模板不存在"})
            return
        if path == "/api/ble-scan/start":
            SCANNING[0] = True
            self._send_json({"ok": True, "scanning": True})
            return
        if path == "/api/ble-scan/stop":
            SCANNING[0] = False
            self._send_json({"ok": True, "scanning": False})
            return
        if path == "/api/ble-scan/read":
            try:
                data = json.loads(body) if body else {}
            except Exception:
                data = {}
            dev = next((d for d in SCAN_DEVICES if d["addr"] == data.get("addr")), SCAN_DEVICES[0])
            tpl = dict(dev)
            tpl.update({
                "alias": tpl.get("adv_name") or "外部设备",
                "device_name": tpl.get("adv_name") or "ExternalPower",
                "manufacturer": "MockPower",
                "model": "PWR-1",
                "firmware": "1.2.3",
                "hardware": "A1",
                "software": "BLE",
                "service_flags": tpl.get("service_flags", 0) | 1,
            })
            self._send_json({"ok": True, "template": tpl})
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
