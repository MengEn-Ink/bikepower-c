#!/usr/bin/env python3
"""核心门户接口替身；配置事务、桥接临时测试及显式 300 秒续期。"""
import argparse
import copy
import json
import math
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB_DIR = ROOT / "main/web"
LOCK = threading.RLock()
CONFIG = {
    "schema_ver": 14, "power": 220, "cadence": 88, "mode": 0,
    "heartrate": 145, "has_user_hr": 0, "power_float": 50, "cadence_float": 8,
    "hr_float": 2, "power_step": 20, "weight": 70, "ftp": 300, "thr_hr": 190,
    "ride_type": 0, "custom_wkg_low": 25, "custom_wkg_high": 30,
    "custom_pacer_name": "自定义", "sport_profile": 0, "broadcast_mask": 7,
    "run_speed_x100": 1000, "run_pace_float_sec": 10, "run_cadence": 170,
    "run_cadence_float": 4, "run_mode": 0, "run_pacer_type": 0,
    "run_pace_low_sec": 390, "run_pace_high_sec": 450, "run_custom_name": "自定义",
    "device_name": "BikePower-Mock", "cycling_source": 0, "bridge_bound": 0,
    "bridge_addr_type": 0, "bridge_address": "000000000000",
    "bridge_calibration": 0, "bridge_watts": 0, "bridge_percent_x10": 1000, "bridge_name": "",
    "bridge_devices": [],
    "led_silent": 0,
}
for scene in ("wait", "conn", "wifi", "confirm", "error"):
    CONFIG[f"led_{scene}_mode"] = 2
    CONFIG[f"led_{scene}_color"] = 1
PACERS = [(24, 28, "Coco"), (27, 31, "Yumi"), (30, 35, "Jacques"),
          (35, 39, "Genie"), (40, 44, "Constance")]
RUN_PACERS = [(390, 450, 165, "慢跑"), (330, 390, 170, "有氧"),
              (270, 315, 174, "节奏"), (210, 255, 180, "间歇")]
DEVICES = [
    {"candidate": 16, "name": "P505-4A29", "rssi": -41, "addr_type": 0, "address": "294a11223344"},
    {"candidate": 17, "name": "Assioma Uno", "rssi": -55, "addr_type": 1, "address": "4db811223344"},
    {"candidate": 18, "name": "Stages L", "rssi": -61, "addr_type": 0, "address": "5ec911223344"},
    {"candidate": 19, "name": "Quarq DZero", "rssi": -67, "addr_type": 1, "address": "6fda11223344"},
]
IDENTITY = {"manufacturer": "Magene", "model": "P505", "serial": "MOCK-4A29",
            "hardware": "1.0", "firmware": "2.1", "software": ""}
BOARD_IDENTITY = {"manufacturer": "BikePower", "model": "Generic-C3", "serial": "",
                  "hardware": "1.0", "firmware": "4.3.0", "software": ""}
LIMITS = {
    "schema_ver": (14, 14),
    "power": (0, 2000), "cadence": (20, 120), "heartrate": (60, 200),
    "power_float": (0, 100), "cadence_float": (0, 20), "hr_float": (0, 20),
    "power_step": (5, 50), "weight": (40, 150), "ftp": (50, 500), "thr_hr": (100, 220),
    "custom_wkg_low": (10, 70), "custom_wkg_high": (10, 70),
    "run_speed_x100": (300, 2500), "run_pace_float_sec": (0, 60),
    "run_cadence": (120, 220), "run_cadence_float": (0, 20),
    "run_pace_low_sec": (144, 1200), "run_pace_high_sec": (144, 1200),
    "bridge_watts": (-200, 200), "bridge_percent_x10": (500, 1500),
    "mode": (0, 1), "run_mode": (0, 1), "sport_profile": (0, 1), "ride_type": (0, 5),
    "run_pacer_type": (0, 4), "bridge_calibration": (0, 2), "cycling_source": (0, 1),
    "bridge_bound": (0, 1), "bridge_addr_type": (0, 1), "has_user_hr": (0, 1),
    "broadcast_mask": (1, 7),
}
STATE = {"revision": 1, "deadline": time.monotonic() + 300, "closing": False,
         "transaction": 0, "pending": False, "result": 0, "restart": 0,
         "scanning_until": 0, "generation": 1, "test_at": 0, "test_until": 0,
         "peer": None, "temporary": False, "bridge_state": 0,
         "runtime_override": "",
         "fault": "", "fail_save": False, "next_config": None, "close_after": False,
         "complete_at": 0}


def remaining():
    return max(0, math.ceil(STATE["deadline"] - time.monotonic()))


def device_key(device):
    return (int(device.get("addr_type", -1)), str(device.get("address", "")).lower())


def valid_device(device):
    if not isinstance(device, dict):
        return False
    address = device.get("address")
    addr_type = device.get("addr_type")
    name = device.get("name", "")
    if type(addr_type) is not int or addr_type not in (0, 1):
        return False
    if not isinstance(address, str) or len(address) != 12:
        return False
    try:
        valid_address = int(address, 16) != 0
    except ValueError:
        return False
    return valid_address and isinstance(name, str) and len(name.encode()) < 32


def normalize_devices(devices):
    if not isinstance(devices, list) or len(devices) > 3:
        return None
    result = []
    seen = set()
    for item in devices:
        if not valid_device(item) or device_key(item) in seen:
            return None
        seen.add(device_key(item))
        result.append({
            "addr_type": item["addr_type"],
            "address": item["address"].lower(),
            "name": item.get("name", ""),
        })
    return result


def sync_primary(config):
    devices = config["bridge_devices"]
    primary = devices[0] if devices else {
        "addr_type": 0, "address": "000000000000", "name": "",
    }
    config["bridge_bound"] = int(bool(devices))
    config["bridge_addr_type"] = primary["addr_type"]
    config["bridge_address"] = primary["address"]
    config["bridge_name"] = primary["name"]
    if not devices:
        config["cycling_source"] = 0


def failover():
    configured = bool(CONFIG["cycling_source"] and CONFIG["bridge_devices"]
                      and CONFIG["sport_profile"] == 0)
    fault = STATE["fault"]
    override = STATE["runtime_override"]
    if not configured:
        state_name = "disabled"
    elif override == "fast_recovery":
        state_name = override
    elif fault in ("all_missing", "stale", "disconnect"):
        state_name = "fallback_simulation"
    elif fault == "backup_live":
        state_name = "live"
    elif override:
        state_name = override
    elif STATE["temporary"]:
        state_name = "paused"
    else:
        state_name = "live"
    active = 1 if (state_name == "live" and fault == "backup_live"
                   and len(CONFIG["bridge_devices"]) > 1) else (
        0 if state_name == "live" else -1)
    candidate = 1 if state_name in ("fallback_simulation", "background_probe") else (
        0 if state_name == "fast_recovery" else -1)
    simulated = state_name in (
        "disabled", "fallback_simulation", "background_probe", "paused")
    return {
        "state": state_name,
        "configured_real": configured,
        "actual_source": "simulation" if simulated else "real",
        "active_slot": active,
        "candidate_slot": candidate,
        "next_attempt_ms": 42000 if state_name in (
            "fallback_simulation", "background_probe") else 0,
    }


def bridge():
    runtime_state = failover()
    peer = STATE["peer"] if STATE["temporary"] else None
    if not peer and runtime_state["active_slot"] >= 0:
        peer = CONFIG["bridge_devices"][runtime_state["active_slot"]]
    peer = peer or {"address": "000000000000", "addr_type": 0, "name": ""}
    elapsed = time.monotonic() - STATE["test_at"]
    if STATE["temporary"] and STATE["peer"] and elapsed >= 0.4 and STATE["bridge_state"] not in (0, 9):
        STATE["bridge_state"] = 5
    if STATE["temporary"] and time.monotonic() > STATE["test_until"]:
        STATE["bridge_state"] = 0
    tested = bool(peer["address"] != "000000000000"
                  and (not STATE["temporary"] or STATE["bridge_state"] == 5))
    fault = STATE["fault"]
    valid = tested and fault not in ("all_missing", "stale", "disconnect")
    raw = 0 if fault == "zero" else 238
    c = CONFIG
    corrected = raw
    if raw > 0:
        if c["bridge_calibration"] == 1:
            corrected += c["bridge_watts"]
        if c["bridge_calibration"] == 2:
            corrected = math.floor(raw * c["bridge_percent_x10"] / 1000 + 0.5)
    return {"state": 6 if fault == "stale" else 7 if fault == "disconnect" else STATE["bridge_state"],
            "connected": tested and fault != "disconnect", "temporary": STATE["temporary"],
            "tested": tested, "valid": valid, "raw_power": raw if valid else 0,
            "power": max(0, min(2000, corrected)) if valid else 0,
            "cadence": 92 if valid and raw and peer["name"] else 0, "age_ms": 4000 if fault == "stale" else 100,
            "error": 0, "device_name": peer["name"], "address": peer["address"],
            "addr_type": peer["addr_type"], "name": peer["name"],
            "identity": IDENTITY if peer["name"] else {k: "" for k in IDENTITY}}


def tick():
    if STATE["pending"] and time.monotonic() >= STATE["complete_at"]:
        STATE["pending"] = False
        STATE["result"] = 5 if STATE["fail_save"] else 0
        if not STATE["fail_save"]:
            identity = bridge()["identity"]
            CONFIG.update(STATE["next_config"])
            sync_primary(CONFIG)
            if STATE.get("copy_identity"):
                BOARD_IDENTITY.update({k: v for k, v in identity.items() if v})
            STATE["revision"] += 1
            if CONFIG["cycling_source"] and CONFIG["sport_profile"] == 0 and CONFIG["bridge_devices"]:
                STATE["peer"] = copy.deepcopy(CONFIG["bridge_devices"][0])
                STATE["temporary"] = False
                STATE["test_at"] = time.monotonic()
                STATE["bridge_state"] = 5
                STATE["runtime_override"] = "live"
            else:
                STATE["peer"] = None
                STATE["bridge_state"] = 0
                STATE["temporary"] = False
                STATE["runtime_override"] = ""
            if STATE["close_after"]:
                STATE["closing"] = True
        STATE["fail_save"] = False
    if remaining() == 0:
        STATE["closing"] = True


def runtime():
    tick()
    b = bridge()
    c = CONFIG
    bridge_state = failover()
    active = (bridge_state["actual_source"] == "real" and b["valid"]
              and not b["temporary"])
    power = b["power"] if active else c["power"]
    cadence = b["cadence"] if active else c["cadence"]
    if c["sport_profile"]:
        cadence = c["run_cadence"]
    return {k: STATE[k] for k in ("revision", "transaction", "pending", "result", "restart")} | {
        "source": c["cycling_source"], "power": power, "cadence": cadence,
        "heartrate": c["heartrate"] if c["broadcast_mask"] & 4 else 0,
        "connections": 1, "remaining_sec": remaining(), "closing": STATE["closing"],
        "bridge": b, "bridge_runtime": bridge_state,
        "run_speed_x100": c["run_speed_x100"], "run_distance_m": 123, "run_elapsed_sec": 60}


def state():
    tick()
    return {"config": copy.deepcopy(CONFIG), "revision": STATE["revision"], "bridge": bridge(),
            "identity": copy.deepcopy(BOARD_IDENTITY),
            "firmware_version": "4.3.0", "firmware_name": "generic-c3-v4.3.0",
            "board": "generic-c3", "git_sha": "mock", "build_time": "mock",
            "remaining_sec": remaining(), "closing": STATE["closing"],
            "pacers": [{"id": i, "low": lo, "high": hi, "name": name} for i, (lo, hi, name) in enumerate(PACERS)],
            "run_pacers": [{"id": i, "low": lo, "high": hi, "cadence": cadence, "name": name}
                           for i, (lo, hi, cadence, name) in enumerate(RUN_PACERS)]}


def submit(body, close):
    tick()
    if STATE["closing"] or STATE["pending"] or body.get("revision") != STATE["revision"]:
        return {"ok": False, "error": "配置已变化或设备忙"}
    requested = copy.deepcopy(CONFIG)
    for key, value in body.items():
        if key not in CONFIG:
            continue
        if key == "bridge_devices":
            devices = normalize_devices(value)
            if devices is None:
                return {"ok": False, "error": "功率计列表无效"}
            requested[key] = devices
        elif isinstance(CONFIG[key], int):
            if type(value) is not int:
                return {"ok": False, "error": "参数类型无效"}
            lo, hi = LIMITS.get(key, (0, 13))
            requested[key] = min(hi, max(lo, value))
        elif not isinstance(value, str) or len(value.encode()) > (23 if key == "device_name" else 31):
            return {"ok": False, "error": "名称过长"}
        else:
            requested[key] = value
    old_keys = {device_key(item) for item in CONFIG["bridge_devices"]}
    new_devices = [item for item in requested["bridge_devices"]
                   if device_key(item) not in old_keys]
    if len(new_devices) > 1:
        return {"ok": False, "error": "每次只能新增一台功率计"}
    if len(CONFIG["bridge_devices"]) == 3 and new_devices:
        slot = body.get("replace_slot")
        if type(slot) is not int or slot < 0 or slot >= 3:
            return {"ok": False, "error": "列表已满，请选择替换槽位"}
        if device_key(CONFIG["bridge_devices"][slot]) in {
                device_key(item) for item in requested["bridge_devices"]}:
            return {"ok": False, "error": "替换槽位与设备列表不一致"}
    if new_devices:
        b = bridge()
        if (not b["temporary"] or not b["tested"]
                or device_key(b) != device_key(new_devices[0])):
            return {"ok": False, "error": "功率计尚未通过测试"}
    if body.get("copy_identity") and not new_devices:
        return {"ok": False, "error": "身份只能复制自本次测试的新功率计"}
    if requested["cycling_source"] and not requested["bridge_devices"]:
        return {"ok": False, "error": "未配置真实功率计"}
    sync_primary(requested)
    STATE["transaction"] += 1
    STATE.update(pending=True, next_config=requested, close_after=close, result=1,
                 copy_identity=bool(body.get("copy_identity")),
                 complete_at=time.monotonic() + 0.1)
    return {"ok": True, "pending": True, "transaction": STATE["transaction"]}


def inline_html():
    import sys
    sys.path.insert(0, str(ROOT / "scripts"))
    from inline_static import inline_html as merge, minify_html, minify_css, minify_js
    html = merge(minify_html((WEB_DIR / "index.html").read_text()),
                 minify_css((WEB_DIR / "app.css").read_text()), minify_js((WEB_DIR / "app.js").read_text()))
    return html.encode()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def send(self, body, content_type="application/json", status=200, headers=None):
        if not isinstance(body, bytes):
            body = json.dumps(body, ensure_ascii=False).encode() if content_type == "application/json" else str(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?")[0]
        with LOCK:
            if path in ("/", "/index.html"):
                return self.send(inline_html(), "text/html; charset=utf-8")
            if path == "/favicon.ico":
                return self.send(b"", status=204)
            if path == "/api/state":
                return self.send(state())
            if path == "/api/runtime":
                return self.send(runtime())
            if path == "/api/time":
                return self.send(remaining(), "text/plain")
            if path == "/api/mem_stats":
                return self.send({"free": 120000, "min_free": 98000})
            if path == "/api/bridge/scan":
                return self.send({"scanning": STATE["scanning_until"] > time.monotonic(), "devices": DEVICES})
            if path.startswith("/api/"):
                return self.send({"ok": False, "error": "路由不存在"}, status=404)
            return self.send(b"Redirect to the captive portal", "text/plain", 303, {"Location": "/"})

    def do_POST(self):
        try:
            length = int(self.headers.get("Content-Length", 0))
            if length <= 0 or length > 4096:
                raise ValueError("无效请求长度")
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict):
                raise ValueError("请求必须为对象")
        except (ValueError, json.JSONDecodeError):
            return self.send({"ok": False, "error": "无效请求"})
        path = self.path.split("?")[0]
        with LOCK:
            if path == "/api/mock/fault":
                STATE["fault"] = body.get("fault", "")
                STATE["fail_save"] = bool(body.get("fail_save"))
                if STATE["fault"] == "backup_live":
                    STATE["runtime_override"] = "live"
                elif STATE["fault"] in ("all_missing", "stale", "disconnect"):
                    STATE["runtime_override"] = "fallback_simulation"
                elif STATE["fault"] == "zero" and CONFIG["cycling_source"]:
                    STATE["runtime_override"] = "live"
                elif not STATE["fault"] and CONFIG["cycling_source"]:
                    STATE["runtime_override"] = "live"
                return self.send({"ok": True})
            if path == "/api/renew":
                STATE["deadline"] = time.monotonic() + 300
                return self.send(300, "text/plain")
            if path in ("/api/config", "/api/close-wifi"):
                return self.send(submit(body, path.endswith("close-wifi")))
            if path == "/api/bridge/scan":
                if not body.get("stop"):
                    STATE["generation"] += 1
                    for i, device in enumerate(DEVICES):
                        device["candidate"] = STATE["generation"] * 16 + i
                STATE["scanning_until"] = 0 if body.get("stop") else time.monotonic() + 1
                if not body.get("stop"):
                    STATE["runtime_override"] = "paused"
                return self.send({"ok": True, "pending": True})
            if path == "/api/bridge/control":
                action = body.get("action")
                if action == "stop":
                    STATE.update(peer=None, temporary=False, bridge_state=0,
                                 runtime_override="paused")
                elif action == "test":
                    peer = next((d for d in DEVICES if d["candidate"] == body.get("candidate")), None)
                    if not peer:
                        return self.send({"ok": False, "error": "候选已失效"})
                    STATE.update(peer=copy.deepcopy(peer), temporary=True, test_at=time.monotonic(),
                                 test_until=time.monotonic() + 30, bridge_state=1,
                                 runtime_override="paused")
                elif action in ("resume", "retry") and CONFIG["cycling_source"]:
                    STATE.update(peer=None, temporary=False, test_at=time.monotonic(),
                                 bridge_state=1, runtime_override="fast_recovery")
                else:
                    return self.send({"ok": False, "error": "操作无效"})
                return self.send({"ok": True, "pending": True})
            return self.send({"ok": False, "error": "路由不存在"}, status=404)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    print(f"Mock server: http://{args.bind}:{args.port}（300 秒，显式续期）", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
