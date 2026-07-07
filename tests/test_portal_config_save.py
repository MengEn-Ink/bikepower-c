#!/usr/bin/env python3
"""配网门户配置保存 mock 测试。

启动 tests/mock_server.py 后，通过 HTTP 模拟网页端保存配置，
再读取 /api/state 验证运行时状态已生效。无需真机。
"""
import json
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MOCK_SERVER = ROOT / "tests" / "mock_server.py"


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request_json(url, payload=None, timeout=5):
    if payload is None:
        req = urllib.request.Request(url, headers={"Connection": "close"})
    else:
        body = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            url,
            data=body,
            method="POST",
            headers={"Connection": "close", "Content-Type": "application/json"},
        )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = resp.read()
    return json.loads(data.decode("utf-8"))


def wait_ready(base_url, proc):
    deadline = time.time() + 5
    last_err = None
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"mock server exited early code={proc.returncode}")
        try:
            return request_json(base_url + "/api/state", timeout=1)
        except Exception as exc:
            last_err = exc
            time.sleep(0.1)
    raise RuntimeError(f"mock server not ready: {last_err}")


def check(cond, msg, failures):
    if not cond:
        failures.append(msg)
        print(f"[portal-config] FAIL {msg}")


def main():
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    proc = subprocess.Popen(
        [sys.executable, str(MOCK_SERVER), "--port", str(port)],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    failures = []
    try:
        wait_ready(base, proc)
        payload = {
            "device_name": "BikePower-WebTest",
            "sport_profile": "running",
            "mode": "steady",
            "power": 345,
            "cadence": 96,
            "heartrate": 166,
            "hr_lock": True,
            "power_float": 18,
            "cadence_float": 9,
            "hr_float": 7,
            "broadcast_mask": 5,
            "power_step": 35,
            "run_speed_x100": 1234,
            "run_pace_float_sec": 15,
            "run_cadence": 181,
            "run_cadence_float": 6,
            "run_incline_x10": 35,
            "profile_weight": 82,
            "profile_ftp": 280,
            "profile_thr_hr": 188,
            "profile_ride_type": "custom",
            "custom_wkg_low": 31,
            "custom_wkg_high": 37,
            "custom_pacer_name": "冲刺档",
            "led_silent": 1,
            "led_wait_mode": 0,
            "led_wait_color": 5,
            "led_conn_mode": 5,
            "led_conn_color": 6,
        }
        saved = request_json(base + "/api/config", payload)
        check(saved.get("ok") is True, "POST /api/config 返回 ok=true", failures)
        state = request_json(base + "/api/state")

        for key, expected in payload.items():
            if key == "hr_lock":
                check(state.get("hr_locked") is True, "hr_lock 保存后回显为 hr_locked=true", failures)
            else:
                check(state.get(key) == expected, f"{key} 保存后回显一致", failures)

        custom = next((p for p in state.get("pacers", []) if p.get("id") == "custom"), None)
        check(custom is not None, "pacers 含 custom 档", failures)
        if custom:
            check(custom.get("name") == "冲刺档", "custom 档名称随保存更新", failures)
            check(abs(custom.get("wkg_low", 0) - 3.1) < 0.001, "custom wkg_low 随保存更新", failures)
            check(abs(custom.get("wkg_high", 0) - 3.7) < 0.001, "custom wkg_high 随保存更新", failures)

        request_json(base + "/api/renew", {})
        renewed = urllib.request.urlopen(base + "/api/time", timeout=5).read().decode("utf-8")
        check(renewed == "179", "/api/renew 后倒计时恢复并继续递减", failures)

        before_invalid = request_json(base + "/api/state")
        invalid_payload = {
            "sport_profile": "swim",
            "mode": "invalid",
            "power": 99999,
            "cadence": -1,
            "heartrate": "bad",
            "power_step": 0,
            "run_pace_float_sec": 999,
            "run_cadence_float": "bad",
            "custom_wkg_low": 70,
            "custom_wkg_high": 10,
            "led_wait_mode": 99,
            "broadcast_mask": 0,
        }
        request_json(base + "/api/config", invalid_payload)
        after_invalid = request_json(base + "/api/state")
        check(after_invalid.get("sport_profile") == before_invalid.get("sport_profile"),
              "非法 sport_profile 保持旧值", failures)
        check(after_invalid.get("mode") == before_invalid.get("mode"),
              "非法 mode 保持旧值", failures)
        check(after_invalid.get("power") == 2000, "power 越界夹到上限", failures)
        check(after_invalid.get("cadence") == 20, "cadence 越界夹到下限", failures)
        check(after_invalid.get("heartrate") == before_invalid.get("heartrate"),
              "非数字 heartrate 保持旧值", failures)
        check(after_invalid.get("power_step") == 5, "power_step 越界夹到下限", failures)
        check(after_invalid.get("run_pace_float_sec") == 60, "跑步配速浮动夹到上限", failures)
        check(after_invalid.get("run_cadence_float") == before_invalid.get("run_cadence_float"),
              "非数字跑步步频浮动保持旧值", failures)
        check(after_invalid.get("custom_wkg_low") == 70 and after_invalid.get("custom_wkg_high") == 70,
              "自定义功体比上限自动抬到不低于下限", failures)
        check(after_invalid.get("led_wait_mode") == 5, "LED 模式越界夹到上限", failures)
        check(after_invalid.get("broadcast_mask") == before_invalid.get("broadcast_mask"),
              "广播项全关非法，保持旧值", failures)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)

    if failures:
        print(f"[portal-config] 结果: {len(failures)} 失败")
        return 1
    print("[portal-config] PASS: 网页端配置保存回显测试通过")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
