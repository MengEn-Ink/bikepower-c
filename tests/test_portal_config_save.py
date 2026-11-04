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


def exercise(base):
    state = request_json(base + "/api/state")
    revision = state["revision"]
    payload = dict(state["config"], revision=revision, power=345, device_name="配置测试")
    runtime = complete(base, request_json(base + "/api/config", payload))
    assert runtime["result"] == 0 and runtime["power"] == 345
    assert not request_json(base + "/api/config", payload)["ok"]
    state = request_json(base + "/api/state")
    request_json(base + "/api/mock/fault", {"fail_save": True})
    result = complete(base, request_json(base + "/api/config", {"revision": state["revision"], "power": 400}))
    assert result["result"] != 0 and result["power"] == 345
    scan = request_json(base + "/api/bridge/scan", {})
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
        revision=state["revision"], cycling_source=1, bridge_bound=1,
        bridge_address=candidate["address"], bridge_addr_type=candidate["addr_type"],
        bridge_name=candidate["name"], bridge_calibration=1, bridge_watts=12, broadcast_mask=3)))
    assert result["power"] == 250 and result["heartrate"] == 0
    request_json(base + "/api/mock/fault", {"fault": "zero"})
    assert request_json(base + "/api/runtime")["power"] == 0
    request_json(base + "/api/mock/fault", {"fault": "stale"})
    assert request_json(base + "/api/runtime")["power"] == 0
    request_json(base + "/api/mock/fault", {})
    state = request_json(base + "/api/state")
    complete(base, request_json(base + "/api/config", {"revision": state["revision"], "cycling_source": 0}))
    before = request_json(base + "/api/state")["remaining_sec"]
    for _ in range(5):
        request_json(base + "/api/state")
        request_json(base + "/api/runtime")
    after = request_json(base + "/api/state")["remaining_sec"]
    assert after <= before
    request_json(base + "/api/renew", {})
    assert request_json(base + "/api/state")["remaining_sec"] >= 299


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
