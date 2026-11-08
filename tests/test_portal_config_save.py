#!/usr/bin/env python3
"""HTTP 事务回归：版本冲突、保存失败、临时测试与显式续期。"""
import json
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def request_json(url, payload=None):
    body = None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=5) as response:
        return json.load(response)


def wait_ready(base, proc):
    for _ in range(50):
        try:
            return request_json(base + "/api/state")
        except OSError:
            if proc.poll() is not None:
                raise RuntimeError("mock 启动失败")
            time.sleep(0.05)
    raise RuntimeError("mock 启动超时")


def complete(base, result):
    assert result["ok"], result
    for _ in range(50):
        runtime = request_json(base + "/api/runtime")
        if not runtime["pending"] and runtime["transaction"] == result["transaction"]:
            return runtime
        time.sleep(0.02)
    raise RuntimeError("保存结果超时")


def exercise(base, *, fresh=True, close_wifi=True):
    state = request_json(base + "/api/state")
    original = dict(state["config"])
    assert state["config"]["schema_ver"] == 14
    assert state["config"]["bridge_devices"] == []
    if fresh:
        assert state["config"]["broadcast_mask"] == 15
    assert not request_json(base + "/api/config", {
        "revision": state["revision"], "broadcast_mask": 8,
    })["ok"]
    for mask in (7, 15):
        state = request_json(base + "/api/state")
        saved = complete(base, request_json(base + "/api/config", {
            "revision": state["revision"], "broadcast_mask": mask,
        }))
        assert saved["result"] == 0
        state = request_json(base + "/api/state")
        assert state["config"]["broadcast_mask"] == mask
    revision = state["revision"]
    payload = dict(state["config"], revision=revision, power=345, device_name="配置测试")
    runtime = complete(base, request_json(base + "/api/config", payload))
    assert runtime["result"] == 0 and runtime["power"] == 345
    assert request_json(base + "/api/state")["config"]["schema_ver"] == 14
    assert not request_json(base + "/api/config", payload)["ok"]
    state = request_json(base + "/api/state")
    request_json(base + "/api/mock/fault", {"fail_save": True})
    result = complete(base, request_json(base + "/api/config", {"revision": state["revision"], "power": 400}))
    assert result["result"] != 0 and result["power"] == 345
    request_json(base + "/api/bridge/scan", {})
    scan = request_json(base + "/api/bridge/scan")
    candidate = scan["devices"][0]
    request_json(base + "/api/bridge/control", {"action": "test", "candidate": candidate["candidate"]})
    for _ in range(50):
        result = request_json(base + "/api/runtime")
        if result["bridge"]["tested"]:
            break
        time.sleep(0.02)
    assert result["power"] == 345 and result["bridge"]["tested"]
    state = request_json(base + "/api/state")
    result = complete(base, request_json(base + "/api/config", dict(
        revision=state["revision"], cycling_source=1,
        bridge_devices=[{"address": candidate["address"],
                         "addr_type": candidate["addr_type"],
                         "name": candidate["name"]}],
        bridge_calibration=1, bridge_watts=12, broadcast_mask=3)))
    assert result["power"] == 250 and result["heartrate"] == 0
    assert result["bridge_runtime"]["configured_real"]
    assert result["bridge_runtime"]["actual_source"] == "real"
    state = request_json(base + "/api/state")
    calibration = complete(base, request_json(base + "/api/config", {
        "revision": state["revision"], "bridge_watts": 20,
    }))
    assert calibration["result"] == 0 and calibration["power"] == 258
    for expected in (1, 2):
        request_json(base + "/api/bridge/scan", {})
        scan = request_json(base + "/api/bridge/scan")
        candidate = scan["devices"][expected]
        request_json(base + "/api/bridge/control", {
            "action": "test", "candidate": candidate["candidate"],
        })
        for _ in range(50):
            if request_json(base + "/api/runtime")["bridge"]["tested"]:
                break
            time.sleep(0.02)
        state = request_json(base + "/api/state")
        devices = state["config"]["bridge_devices"] + [{
            "address": candidate["address"], "addr_type": candidate["addr_type"],
            "name": candidate["name"],
        }]
        complete(base, request_json(base + "/api/config", {
            "revision": state["revision"], "bridge_devices": devices,
        }))
    state = request_json(base + "/api/state")
    assert len(state["config"]["bridge_devices"]) == 3
    request_json(base + "/api/bridge/scan", {})
    scan = request_json(base + "/api/bridge/scan")
    candidate = scan["devices"][3]
    request_json(base + "/api/bridge/control", {
        "action": "test", "candidate": candidate["candidate"],
    })
    for _ in range(50):
        if request_json(base + "/api/runtime")["bridge"]["tested"]:
            break
        time.sleep(0.02)
    replacement = list(state["config"]["bridge_devices"])
    replacement[1] = {
        "address": candidate["address"], "addr_type": candidate["addr_type"],
        "name": candidate["name"],
    }
    rejected = request_json(base + "/api/config", {
        "revision": state["revision"], "bridge_devices": replacement,
    })
    assert not rejected["ok"]
    replaced = complete(base, request_json(base + "/api/config", {
        "revision": state["revision"], "replace_slot": 1,
        "bridge_devices": replacement,
    }))
    assert replaced["result"] == 0
    request_json(base + "/api/mock/fault", {"fault": "all_missing"})
    fallback = request_json(base + "/api/runtime")
    assert fallback["bridge_runtime"]["state"] == "fallback_simulation"
    assert fallback["bridge_runtime"]["actual_source"] == "simulation"
    assert fallback["power"] > 0
    request_json(base + "/api/bridge/control", {"action": "retry"})
    assert request_json(base + "/api/runtime")["bridge_runtime"]["state"] == "fast_recovery"
    request_json(base + "/api/mock/fault", {"fault": "backup_live"})
    backup = request_json(base + "/api/runtime")
    assert backup["bridge_runtime"]["state"] == "live"
    assert backup["bridge_runtime"]["active_slot"] == 1
    request_json(base + "/api/mock/fault", {"fault": "zero"})
    assert request_json(base + "/api/runtime")["power"] == 0
    request_json(base + "/api/mock/fault", {"fault": "stale"})
    stale = request_json(base + "/api/runtime")
    assert stale["power"] > 0
    assert stale["bridge_runtime"]["actual_source"] == "simulation"
    request_json(base + "/api/mock/fault", {})
    state = request_json(base + "/api/state")
    complete(base, request_json(base + "/api/config", {
        "revision": state["revision"],
        "cycling_source": 0,
        "bridge_devices": [],
    }))
    before = request_json(base + "/api/state")["remaining_sec"]
    for _ in range(5):
        request_json(base + "/api/state")
        request_json(base + "/api/runtime")
    after = request_json(base + "/api/state")["remaining_sec"]
    assert after <= before
    request_json(base + "/api/renew", {})
    assert request_json(base + "/api/state")["remaining_sec"] >= 299
    state = request_json(base + "/api/state")
    complete(base, request_json(base + "/api/config", dict(original, revision=state["revision"])))
    assert request_json(base + "/api/state")["config"] == original
    if close_wifi:
        exercise_close(base)


def exercise_close(base):
    state = request_json(base + "/api/state")
    assert state["idle_timeout_sec"] == 300
    closed = complete(base, request_json(base + "/api/close-wifi", {"revision": state["revision"]}))
    assert closed["closing"] and 0 < closed["remaining_sec"] <= 5
    assert request_json(base + "/api/renew", {})["closing"]
    assert not request_json(base + "/api/config", {"revision": state["revision"]})["ok"]


def main():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    base = f"http://127.0.0.1:{port}"
    proc = subprocess.Popen([sys.executable, str(ROOT / "tests/mock_server.py"), "--port", str(port)],
                            stdout=subprocess.DEVNULL)
    try:
        wait_ready(base, proc)
        exercise(base)
        print("[portal-config] PASS: 配置事务、冲突、失败保护、桥接和续期")
    finally:
        proc.terminate()
        proc.wait(timeout=5)


if __name__ == "__main__":
    main()
