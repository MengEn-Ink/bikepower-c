#!/usr/bin/env python3
"""10 分钟核心流程耐久测试。

默认运行 10 分钟，循环覆盖：
- host 领域层测试（含模拟按钮、随机功率心率随动、浮动、配置持久化）
- mock 网页 API 操作（首页调参、用户资料、设备名、按钮步进、LED、静默模式）
- 设备模板扫描 / 读取 / 保存 / 应用 / 删除
- 配网续期、倒计时、关闭 WiFi 流程

无需真机；真机 BLE/串口/物理按键仍由 real-device-test 流程补充。
"""
import argparse
import json
import socket
import subprocess
import sys
import time
import urllib.request
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MOCK_SERVER = ROOT / "tests" / "mock_server.py"
REPORT_DIR = ROOT / "tests" / "reports"
DEFAULT_MINUTES = 10


class Recorder:
    def __init__(self):
        self.rows = []
        self.failures = []

    def check(self, cond, name, detail=""):
        status = "PASS" if cond else "FAIL"
        self.rows.append((datetime.now().strftime("%H:%M:%S"), status, name, detail))
        if not cond:
            self.failures.append(f"{name}: {detail}")
        print(f"[core-flow] {status} {name} {detail}".rstrip())

    def note(self, name, detail=""):
        self.rows.append((datetime.now().strftime("%H:%M:%S"), "INFO", name, detail))
        print(f"[core-flow] INFO {name} {detail}".rstrip())


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


def request_text(url, payload=None, timeout=5):
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
        return resp.read().decode("utf-8")


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


def run_cmd(cmd, timeout):
    return subprocess.run(
        cmd,
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
    )


def run_host_gate(rec):
    make = run_cmd(["make", "-C", "test"], timeout=60)
    rec.check(make.returncode == 0, "host make", tail(make.stdout))
    if make.returncode != 0:
        return
    test = run_cmd(["./test/test_domain"], timeout=60)
    rec.check(test.returncode == 0 and "0 失败" in test.stdout, "host domain", tail(test.stdout))


def tail(text, lines=3):
    return " / ".join(text.strip().splitlines()[-lines:]) if text.strip() else ""


def post_config(base, payload):
    saved = request_json(base + "/api/config", payload)
    if not saved.get("ok"):
        raise RuntimeError(f"/api/config not ok: {saved}")
    return request_json(base + "/api/state")


def simulate_button_flow(base, rec, cycle):
    state = request_json(base + "/api/state")
    step = int(state.get("power_step", 20))
    start = 220 + (cycle % 5) * 20
    post_config(base, {"mode": "steady", "power": start, "power_float": 0, "cadence_float": 0})

    after_short = post_config(base, {"power": max(0, start - step)})
    rec.check(after_short.get("power") == max(0, start - step),
              "模拟短按减功率", f"{start}W -> {after_short.get('power')}W, step={step}W")

    long_target = after_short.get("power", 0)
    repeat_count = 5 + (cycle % 4)
    for _ in range(repeat_count):
        long_target += step
        state = post_config(base, {"power": long_target})
    rec.check(state.get("power") == long_target,
              "模拟长按连续加功率", f"repeat={repeat_count}, target={long_target}W")


def expected_hr_for_power(power, ftp, thr):
    if ftp <= 0:
        return max(60, min(200, int(0.45 * power + 45)))
    ratio = power / float(ftp)
    pct = 0.45 + min(1.0, ratio) * 0.53
    if ratio > 1.0:
        pct += min(0.08, (ratio - 1.0) * 0.06)
    return max(60, min(200, int(thr * pct)))


def exercise_hr_following(base, rec, cycle):
    ftp = 280
    thr = 188
    powers = [90 + (cycle % 3) * 10, 420 + (cycle % 4) * 15, 140]
    hrs = [expected_hr_for_power(p, ftp, thr) for p in powers]
    post_config(base, {
        "mode": "steady",
        "profile_ftp": ftp,
        "profile_thr_hr": thr,
        "hr_lock": False,
        "power": powers[0],
        "heartrate": hrs[0],
    })
    observed = []
    for power, hr in zip(powers, hrs):
        state = post_config(base, {"power": power, "heartrate": hr, "hr_lock": False})
        observed.append((state.get("power"), state.get("heartrate")))
    rec.check(observed[1][1] > observed[0][1] + 20 and observed[2][1] < observed[1][1],
              "功率变化心率随动观测", str(observed))


def exercise_web_edits(base, rec, cycle):
    suffix = cycle % 1000
    payload = {
        "device_name": f"BikePower-Endurance-{suffix:03d}",
        "mode": "steady" if cycle % 2 == 0 else "road",
        "power": 180 + (cycle % 10) * 15,
        "cadence": 80 + (cycle % 20),
        "heartrate": 145 + (cycle % 25),
        "hr_lock": cycle % 3 == 0,
        "power_float": (cycle % 31),
        "cadence_float": (cycle % 11),
        "hr_float": (cycle % 9),
        "power_step": 5 + (cycle % 10) * 5,
        "profile_weight": 60 + (cycle % 35),
        "profile_ftp": 180 + (cycle % 120),
        "profile_thr_hr": 160 + (cycle % 35),
        "profile_ride_type": "custom",
        "custom_wkg_low": 20 + (cycle % 20),
        "custom_wkg_high": 35 + (cycle % 25),
        "custom_pacer_name": f"耐久档{suffix:03d}",
        "led_silent": 0,
        "led_wait_mode": cycle % 6,
        "led_wait_color": cycle % 7,
        "led_conn_mode": (cycle + 1) % 6,
        "led_conn_color": (cycle + 1) % 7,
        "led_wifi_mode": (cycle + 2) % 6,
        "led_wifi_color": (cycle + 2) % 7,
        "led_confirm_mode": (cycle + 3) % 6,
        "led_confirm_color": (cycle + 3) % 7,
        "led_error_mode": 3,
        "led_error_color": 0,
    }
    if payload["custom_wkg_high"] < payload["custom_wkg_low"]:
        payload["custom_wkg_high"] = payload["custom_wkg_low"]

    state = post_config(base, payload)
    for key in ("device_name", "mode", "power", "cadence", "heartrate", "power_step",
                "profile_weight", "profile_ftp", "profile_thr_hr", "custom_pacer_name"):
        rec.check(state.get(key) == payload[key], f"网页编辑保存 {key}", str(state.get(key)))
    rec.check(state.get("hr_locked") == payload["hr_lock"], "网页编辑保存 hr_lock", str(state.get("hr_locked")))

    silent_state = post_config(base, {"led_silent": 1})
    rec.check(silent_state.get("led_silent") == 1, "LED 静默模式保存", "选项显隐由前端 syncLedSilentVisibility 处理")
    normal_state = post_config(base, {"led_silent": 0})
    rec.check(normal_state.get("led_silent") == 0, "LED 非静默模式恢复", "可继续编辑模式/颜色")


def exercise_template_flow(base, rec, cycle):
    start = request_json(base + "/api/ble-scan/start", {})
    rec.check(start.get("ok") is True, "设备模板扫描启动")
    results = request_json(base + "/api/ble-scan/results")
    ride_devices = [d for d in results.get("devices", []) if d.get("service_flags", 0)]
    rec.check(len(ride_devices) >= 1, "设备模板扫描结果含骑行设备", f"count={len(ride_devices)}")
    if not ride_devices:
        return

    tpl = request_json(base + "/api/ble-scan/read", {"addr": ride_devices[cycle % len(ride_devices)]["addr"]})
    rec.check(tpl.get("ok") is True and tpl.get("template"), "读取外部设备信息")
    template = tpl["template"]
    template["alias"] = f"耐久模板{cycle:03d}"
    saved = request_json(base + "/api/device-templates/save", template)
    rec.check(saved.get("ok") is True, "保存设备模板", str(saved))
    if not saved.get("ok"):
        return
    tid = int(saved["id"])
    listed = request_json(base + "/api/device-templates")
    rec.check(any(item.get("id") == tid for item in listed.get("items", [])), "模板列表回显保存项")
    applied = request_json(base + "/api/device-templates/apply", {"id": tid})
    rec.check(applied.get("ok") is True, "应用设备模板")
    deleted = request_json(base + "/api/device-templates/delete", {"id": tid})
    rec.check(deleted.get("ok") is True, "删除设备模板")


def exercise_wifi_session(base, rec):
    request_text(base + "/api/renew", {})
    sec = int(request_text(base + "/api/time"))
    rec.check(0 <= sec <= 179, "配网续期倒计时", f"remaining={sec}s")
    closed = request_json(base + "/api/close-wifi", {})
    state = request_json(base + "/api/state")
    rec.check(closed.get("ok") is True and state.get("closing") is True, "保存并关闭 WiFi 流程")
    post_config(base, {"closing": False})


def write_report(path, rec, started_at, ended_at, minutes, cycles):
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# BikePower C 核心流程耐久测试报告",
        "",
        f"- 开始时间：{started_at}",
        f"- 结束时间：{ended_at}",
        f"- 计划时长：{minutes} 分钟",
        f"- 完成循环：{cycles}",
        f"- 结果：{'失败' if rec.failures else '通过'}",
        "",
        "## 覆盖流程",
        "",
        "- 模拟按钮短按减功率、长按连续加功率。",
        "- 改变功率数值并记录心率随动趋势。",
        "- 网页端保存首页参数、用户资料、设备名称、按钮步进、自定义配速、LED 灯效。",
        "- LED 静默模式开启后隐藏普通灯效选项的前端契约由 `check_portal_structure.py` 守护，本测试验证保存链路。",
        "- 设备模板扫描、读取、保存、应用、删除。",
        "- 配网续期、倒计时、保存并关闭 WiFi。",
        "- host 单测覆盖真实领域算法：按钮、心率模型、浮动、持久化。",
        "",
        "## 明细",
        "",
        "| 时间 | 状态 | 项目 | 说明 |",
        "|------|------|------|------|",
    ]
    for ts, status, name, detail in rec.rows:
        safe = detail.replace("|", "\\|").replace("\n", " ")
        lines.append(f"| {ts} | {status} | {name} | {safe} |")
    if rec.failures:
        lines.extend(["", "## 失败项", ""])
        for failure in rec.failures:
            lines.append(f"- {failure}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="BikePower C 10 分钟核心流程耐久测试")
    parser.add_argument("--minutes", type=float, default=DEFAULT_MINUTES, help="运行时长，默认 10")
    parser.add_argument("--interval", type=float, default=30.0, help="循环间隔秒数，默认 30")
    parser.add_argument("--cycles", type=int, default=0, help="指定循环次数；用于快速验证")
    parser.add_argument("--host-every-minutes", type=float, default=15.0, help="host 单测间隔，默认 15 分钟")
    parser.add_argument("--skip-host", action="store_true", help="跳过 host 单测，仅调试 mock 流程")
    parser.add_argument("--port", type=int, default=0, help="mock server 端口，默认随机")
    parser.add_argument("--report", default="", help="报告输出路径")
    args = parser.parse_args()

    port = args.port or free_port()
    base = f"http://127.0.0.1:{port}"
    proc = subprocess.Popen(
        [sys.executable, str(MOCK_SERVER), "--port", str(port)],
        cwd=str(ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    rec = Recorder()
    started = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    cycles_done = 0
    report = Path(args.report) if args.report else REPORT_DIR / f"core_flow_endurance_{datetime.now().strftime('%Y%m%d_%H%M%S')}.md"

    try:
        wait_ready(base, proc)
        rec.note("mock server ready", base)
        if not args.skip_host:
            run_host_gate(rec)
        next_host = time.time() + args.host_every_minutes * 60
        deadline = time.time() + args.minutes * 60

        while True:
            if args.cycles and cycles_done >= args.cycles:
                break
            if not args.cycles and time.time() >= deadline:
                break
            cycle = cycles_done + 1
            rec.note("cycle start", str(cycle))
            simulate_button_flow(base, rec, cycle)
            exercise_hr_following(base, rec, cycle)
            exercise_web_edits(base, rec, cycle)
            exercise_template_flow(base, rec, cycle)
            exercise_wifi_session(base, rec)
            cycles_done += 1
            if not args.skip_host and time.time() >= next_host:
                run_host_gate(rec)
                next_host = time.time() + args.host_every_minutes * 60
            if args.cycles and cycles_done >= args.cycles:
                break
            sleep_for = min(args.interval, max(0.0, deadline - time.time()))
            if sleep_for > 0:
                time.sleep(sleep_for)
    finally:
        ended = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        write_report(report, rec, started, ended, args.minutes, cycles_done)
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)

    print(f"[core-flow] report: {report}")
    if rec.failures:
        print(f"[core-flow] FAIL: {len(rec.failures)} failures")
        return 1
    print(f"[core-flow] PASS: cycles={cycles_done}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
