#!/usr/bin/env python3
"""本地 OTA 页面模拟器。

用途：
1. 从 main/wifi_portal.c 提取 /ota 页面的真实 JavaScript。
2. 在 localhost 提供 /ota、/ota/check、/ota/status、/ota/upgrade mock 接口。
3. 用 Node 原生能力执行页面脚本，验证检查更新 -> 轮询状态 -> 触发升级的前端流程。

不覆盖：
- ESP32 真机 WiFi/APSTA/STA 切换。
- esp_https_ota 下载写入 flash。
- bootloader 回滚自检。

用法：
  python3 tests/ota_local_mock.py --self-test
  python3 tests/ota_local_mock.py --serve --port 8080
"""
import argparse
import http.server
import json
import os
import re
import shutil
import socketserver
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PORTAL_FILE = ROOT / "main" / "wifi_portal.c"
TAG = "[OTA-MOCK]"

C_STRING_PATTERN = re.compile(r'"((?:\\.|[^"\\])*)"')
SCRIPT_OPEN = "<script>"
SCRIPT_CLOSE = "</script>"


def extract_c_strings(src: str) -> str:
    parts = []
    for m in C_STRING_PATTERN.finditer(src):
        s = m.group(1)
        s = (s.replace("\\n", "\n")
             .replace("\\t", "\t")
             .replace('\\"', '"')
             .replace("\\'", "'")
             .replace("\\\\", "\\"))
        parts.append(s)
    return "".join(parts)


def find_h_ota_segment(src: str) -> str:
    idx = src.find("static esp_err_t h_ota(")
    if idx < 0:
        raise RuntimeError("找不到 h_ota 函数")
    next_handler = re.search(r"\nstatic esp_err_t h_\w+\(", src[idx + 10:])
    end = idx + 10 + next_handler.start() if next_handler else len(src)
    return src[idx:end]


def extract_ota_script() -> str:
    src = PORTAL_FILE.read_text(encoding="utf-8")
    text = extract_c_strings(find_h_ota_segment(src))
    cursor = 0
    while True:
        s = text.find(SCRIPT_OPEN, cursor)
        if s < 0:
            break
        e = text.find(SCRIPT_CLOSE, s + len(SCRIPT_OPEN))
        if e < 0:
            break
        js = text[s + len(SCRIPT_OPEN):e]
        if "function checkUpdate" in js and "function startUpgrade" in js:
            return js
        cursor = e + len(SCRIPT_CLOSE)
    raise RuntimeError("找不到 OTA 页面脚本块")


def find_h_ota_upgrade_segment(src: str) -> str:
    """定位 h_ota_upgrade 函数体（返回的「升级中」页面含进度轮询脚本）。"""
    idx = src.find("static esp_err_t h_ota_upgrade(")
    if idx < 0:
        raise RuntimeError("找不到 h_ota_upgrade 函数")
    next_handler = re.search(r"\nstatic esp_err_t h_\w+\(", src[idx + 10:])
    end = idx + 10 + next_handler.start() if next_handler else len(src)
    return src[idx:end]


def extract_upgrade_script() -> str:
    """提取「升级中」页面的进度轮询脚本块（含 poll + /ota/upgrade/status 轮询）。"""
    src = PORTAL_FILE.read_text(encoding="utf-8")
    text = extract_c_strings(find_h_ota_upgrade_segment(src))
    cursor = 0
    while True:
        s = text.find(SCRIPT_OPEN, cursor)
        if s < 0:
            break
        e = text.find(SCRIPT_CLOSE, s + len(SCRIPT_OPEN))
        if e < 0:
            break
        js = text[s + len(SCRIPT_OPEN):e]
        if "/ota/upgrade/status" in js and "function poll" in js:
            return js
        cursor = e + len(SCRIPT_CLOSE)
    raise RuntimeError("找不到升级页进度轮询脚本块")


def build_mock_page() -> str:
    script = extract_ota_script()
    return f"""<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>固件升级 - BikePower 本地模拟</title>
  <style>
    *{{box-sizing:border-box}} body{{margin:0;background:#f2f2f7;font-family:-apple-system,sans-serif;color:#1d1d1f;padding:16px}}
    .box{{max-width:420px;margin:0 auto}} .hero,.card{{background:#fff;border-radius:16px;padding:16px;margin-bottom:10px}}
    h1{{font-size:22px;margin:0 0 4px}} .sub,p{{font-size:12px;color:#6e6e73;line-height:1.5}}
    .ver{{font-size:13px;color:#007aff;font-weight:700;margin:8px 0}}
    .cred-row{{display:flex;justify-content:space-between;padding:10px 12px;background:#f5f5f7;border-radius:12px;margin:8px 0}}
    .cred-label{{font-size:12px;color:#86868b}} .cred-val{{font-size:14px;font-weight:700}}
    .btn{{display:block;width:100%;padding:11px;border:0;border-radius:12px;background:#007aff;color:#fff;font-size:14px;font-weight:600}}
    .btn:disabled{{opacity:.4}} .warn{{margin-top:8px;font-size:12px;color:#856404;background:#fff3cd;border-radius:10px;padding:10px}}
    .spinner{{display:inline-block;width:14px;height:14px;border:2px solid #007aff;border-top:2px solid transparent;border-radius:50%;vertical-align:middle;margin-right:6px}}
  </style>
</head>
<body>
  <div class="box">
    <div class="hero"><h1>固件升级</h1><p class="sub">本地 mock：不连接 BikePower 热点，也不写入设备 flash。</p></div>
    <div class="card">
      <div class="ver">当前版本：v3.1.8</div>
      <div class="cred-row"><span class="cred-label">家庭网络</span><span class="cred-val">LocalMock</span></div>
      <button class="btn" type="button" id="check-btn" onclick="checkUpdate()">检查更新</button>
      <div id="check-result" style="margin-top:12px"></div>
      <button class="btn" type="button" id="upgrade-btn" onclick="startUpgrade()" disabled style="margin-top:12px">下载并升级</button>
      <div class="warn">本页模拟 /ota/check、/ota/status、/ota/upgrade 的 HTTP 合约，只验证网页流程。</div>
    </div>
  </div>
  <script>{script}</script>
</body>
</html>"""


class MockState:
    def __init__(self, mode: str):
        self.mode = mode
        self.status = "idle"
        self.poll_count = 0
        # 升级进度状态机（对应固件 /ota/upgrade/status 合约）
        self.up_state = "idle"      # idle/running/downloading/success/fail/rebooting
        self.up_read = 0
        self.up_total = 0
        self.up_poll = 0


def build_upgrade_page() -> str:
    """构造「升级中」mock 页：注入真实进度轮询脚本，便于 self-test 驱动。"""
    script = extract_upgrade_script()
    return f"""<!DOCTYPE html><html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>升级中 - BikePower 本地模拟</title></head><body>
<p id="phase">准备升级...</p>
<div><div id="bar" style="width:0"></div></div>
<p id="pct">0%</p>
<p id="tip">本地 mock：进度轮询验证，不写入真实 flash。</p>
<div id="act" style="display:none"><a href="/ota">返回固件升级</a></div>
<script>{script}</script>
</body></html>"""


class OtaMockHandler(http.server.BaseHTTPRequestHandler):
    server_version = "BikePowerOtaMock/1.0"

    def log_message(self, fmt, *args):
        print(f"{TAG} {self.address_string()} {fmt % args}")

    @property
    def state(self) -> MockState:
        return self.server.state

    def send_bytes(self, status: int, body: bytes, ctype: str):
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_json(self, obj):
        self.send_bytes(200, json.dumps(obj, ensure_ascii=False).encode("utf-8"), "application/json")

    def do_GET(self):
        if self.path == "/" or self.path == "/ota":
            self.send_bytes(200, build_mock_page().encode("utf-8"), "text/html; charset=utf-8")
            return
        if self.path == "/ota/status":
            if self.state.status == "running":
                self.state.poll_count += 1
                if self.state.mode == "error" and self.state.poll_count >= 2:
                    self.state.status = "error"
                elif self.state.poll_count >= 2:
                    self.state.status = "done"
            if self.state.status == "done":
                self.send_json({
                    "status": "done",
                    "has_update": self.state.mode != "no-update",
                    "version": "3.1.9",
                    "size": 1310720,
                    # 注入恶意 changelog：含引号/反斜杠/<script>，验证 JSON 合法 + 前端转义不执行
                    "changelog": "v3.1.9 \"quote\" \\back <script>alert('xss')</script> & <b>bold</b>",
                    "current": "3.1.8",
                })
            elif self.state.status == "error":
                self.send_json({"status": "error", "error": "本地模拟版本检查失败", "current": "3.1.8"})
            elif self.state.status == "running":
                self.send_json({"status": "running", "current": "3.1.8"})
            else:
                self.send_json({"status": "idle", "current": "3.1.8"})
            return
        if self.path in ("/more", "/about"):
            self.send_bytes(200, b"mock ok", "text/plain; charset=utf-8")
            return
        if self.path == "/ota/upgrade/status":
            self.serve_upgrade_status()
            return
        if self.path == "/__mock/upgrade_polls":
            # 测试内省端点：返回 /ota/upgrade/status 被轮询的次数，用于断言终态后停止轮询
            self.send_json({"polls": self.state.up_poll})
            return
        self.send_bytes(404, b"not found", "text/plain")

    def serve_upgrade_status(self):
        """模拟固件 /ota/upgrade/status 进度合约，按 mode 推进状态机。

        update：running → downloading（0→25→50→75→100%）→ success → rebooting。
        error ：running → downloading（部分）→ fail。
        前端轮询读到 success/rebooting/fail 即应置 done 停止轮询，不会无限轮询。
        """
        s = self.state
        if s.up_state == "idle":
            # 升级尚未触发时返回 idle，绝不误触发升级
            self.send_json({"state": "idle", "read": 0, "total": 0, "phase": "", "error": ""})
            return
        s.up_poll += 1
        total = 1310720
        if s.up_state == "running":
            if s.up_poll >= 1:
                s.up_state = "downloading"
                s.up_total = total
                s.up_read = 0
            self.send_json({"state": "running", "read": 0, "total": 0,
                            "phase": "连接家庭WiFi", "error": ""})
            return
        if s.up_state == "downloading":
            s.up_read = min(total, s.up_read + total // 4)
            if s.mode == "error" and s.up_read >= total // 2:
                s.up_state = "fail"
                self.send_json({"state": "fail", "read": s.up_read, "total": total,
                                "phase": "下载失败", "error": "固件下载失败"})
                return
            if s.up_read >= total:
                s.up_state = "success"
            self.send_json({"state": "downloading", "read": s.up_read, "total": total,
                            "phase": "下载固件", "error": ""})
            return
        if s.up_state == "success":
            s.up_state = "rebooting"
            self.send_json({"state": "success", "read": total, "total": total,
                            "phase": "下载完成", "error": ""})
            return
        # rebooting / fail 终态
        self.send_json({"state": s.up_state, "read": s.up_read, "total": s.up_total,
                        "phase": "即将重启" if s.up_state == "rebooting" else "升级失败",
                        "error": "" if s.up_state == "rebooting" else "固件下载失败"})

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length:
            self.rfile.read(length)
        if self.path == "/ota/check":
            self.state.status = "running"
            self.state.poll_count = 0
            self.send_json({"status": "started", "current": "3.1.8"})
            return
        if self.path == "/ota/upgrade":
            # 触发升级：进入 running，返回带进度轮询脚本的真实「升级中」页
            self.state.up_state = "running"
            self.state.up_poll = 0
            self.state.up_read = 0
            self.state.up_total = 0
            body = build_upgrade_page().encode("utf-8")
            self.send_bytes(200, body, "text/html; charset=utf-8")
            return
        self.send_bytes(404, b"not found", "text/plain")


class ThreadingTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True


def run_server(port: int, mode: str):
    httpd = ThreadingTCPServer(("127.0.0.1", port), OtaMockHandler)
    httpd.state = MockState(mode)
    return httpd


NODE_SELF_TEST = r"""
const base = process.argv[2];

function makeElement(id) {
  return { id, disabled: false, textContent: "", innerHTML: "", style: {} };
}

const elements = {
  "check-btn": makeElement("check-btn"),
  "upgrade-btn": makeElement("upgrade-btn"),
  "check-result": makeElement("check-result"),
};
elements["check-btn"].textContent = "检查更新";
elements["upgrade-btn"].textContent = "下载并升级";
elements["upgrade-btn"].disabled = true;

let writtenDocument = "";

global.document = {
  getElementById(id) { return elements[id] || null; },
  open() { writtenDocument = ""; },
  write(html) { writtenDocument += html; },
  close() {},
};
global.confirm = () => true;
global.alert = (msg) => { throw new Error("alert: " + msg); };
global.console = console;

class MockXHR {
  constructor() {
    this.status = 0;
    this.responseText = "";
    this.timeout = 0;
    this.onload = null;
    this.onerror = null;
    this.ontimeout = null;
  }
  open(method, url) {
    this.method = method;
    this.url = new URL(url, base).toString();
  }
  async send(body) {
    try {
      const resp = await fetch(this.url, { method: this.method, body });
      this.status = resp.status;
      this.responseText = await resp.text();
      if (this.onload) this.onload();
    } catch (e) {
      if (this.onerror) this.onerror(e);
    }
  }
}
global.XMLHttpRequest = MockXHR;

function waitFor(cond, timeoutMs, label) {
  return new Promise((resolve, reject) => {
    const start = Date.now();
    const timer = setInterval(() => {
      if (cond()) {
        clearInterval(timer);
        resolve();
      } else if (Date.now() - start > timeoutMs) {
        clearInterval(timer);
        reject(new Error("timeout: " + label));
      }
    }, 100);
  });
}

(async () => {
  const page = await fetch(base + "/ota").then(r => r.text());
  const m = page.match(/<script>([\s\S]*?)<\/script>/);
  if (!m) throw new Error("未找到 script");
  eval(m[1]);

  checkUpdate();
  await waitFor(
    () => elements["upgrade-btn"].disabled === false || elements["check-result"].innerHTML.includes("已是最新版本"),
    8000,
    "checkUpdate 完成"
  );
  if (!elements["check-result"].innerHTML.includes("服务器最新版本")) {
    throw new Error("检查结果未展示服务器版本");
  }
  if (elements["upgrade-btn"].disabled) {
    throw new Error("升级按钮未解锁");
  }
  // XSS 加固断言：恶意 changelog 必须被转义，innerHTML 不得含可执行的原始 <script>
  const html = elements["check-result"].innerHTML;
  if (html.includes("<script>alert")) {
    throw new Error("changelog 未转义，存在 XSS 风险: " + html);
  }
  if (!html.includes("&lt;script&gt;")) {
    throw new Error("changelog 未按预期 HTML 转义: " + html);
  }

  startUpgrade();
  await waitFor(() => writtenDocument.includes("正在升级") || writtenDocument.includes("准备升级"), 3000, "upgrade 页面");
  console.log("PASS local OTA JS flow");
})().catch(e => {
  console.error(e.stack || e.message || e);
  process.exit(1);
});
"""


# 升级进度页自测：驱动真实「升级中」页脚本轮询 /ota/upgrade/status，
# 验证进度推进、success/fail 终态置 done（停止轮询），不会无限轮询。
NODE_UPGRADE_TEST = r"""
const base = process.argv[2];
const expect = process.argv[3];   // "success" | "fail"

function makeElement(id) { return { id, textContent: "", innerHTML: "", style: {} }; }
const elements = {
  phase: makeElement("phase"), bar: makeElement("bar"),
  pct: makeElement("pct"), tip: makeElement("tip"), act: makeElement("act"),
};
elements.act.style.display = "none";

global.document = { getElementById(id) { return elements[id] || null; } };
global.console = console;

class MockXHR {
  constructor() { this.status = 0; this.responseText = ""; this.timeout = 0;
    this.onload = null; this.onerror = null; this.ontimeout = null; }
  open(method, url) { this.method = method; this.url = new URL(url, base).toString(); }
  async send(body) {
    try {
      const resp = await fetch(this.url, { method: this.method, body });
      this.status = resp.status; this.responseText = await resp.text();
      if (this.onload) this.onload();
    } catch (e) { if (this.onerror) this.onerror(e); }
  }
}
global.XMLHttpRequest = MockXHR;

let intervals = [];
const realSetInterval = setInterval;
global.setInterval = (fn, ms) => { const t = realSetInterval(fn, ms); intervals.push(t); return t; };

function waitFor(cond, timeoutMs, label) {
  return new Promise((resolve, reject) => {
    const start = Date.now();
    const timer = realSetInterval(() => {
      if (cond()) { clearInterval(timer); resolve(); }
      else if (Date.now() - start > timeoutMs) { clearInterval(timer); reject(new Error("timeout: " + label)); }
    }, 50);
  });
}

(async () => {
  const page = await fetch(base + "/ota/upgrade", { method: "POST" }).then(r => r.text());
  const m = page.match(/<script>([\s\S]*?)<\/script>/);
  if (!m) throw new Error("升级页未找到 script");
  let sawProgress = false;
  // 监视进度文本变化，确认下载过程有百分比推进
  const watch = realSetInterval(() => {
    if (/\d+%/.test(elements.pct.textContent) && elements.pct.textContent !== "0%") sawProgress = true;
  }, 30);

  eval(m[1]);   // 注入并启动真实 poll()+setInterval

  // done 由脚本内部置位：成功(success/rebooting)或失败(fail)后 poll() 提前 return
  await waitFor(() => /done\s*=\s*1/.test("") || elements.phase.textContent.includes("成功")
    || elements.phase.textContent.includes("失败") || elements.act.style.display === "block",
    25000, "升级终态");
  clearInterval(watch);

  // 终态后再等 4s，确认轮询已停止（不会无限轮询）：记录终态后的请求计数
  const reqBefore = await fetch(base + "/__mock/upgrade_polls").then(r => r.json()).then(d => d.polls);
  await new Promise(r => realSetInterval(() => r(), 4000));
  const reqAfter = await fetch(base + "/__mock/upgrade_polls").then(r => r.json()).then(d => d.polls);
  intervals.forEach(clearInterval);

  if (reqAfter - reqBefore > 1) {
    throw new Error("终态后仍在轮询，可能无限轮询: before=" + reqBefore + " after=" + reqAfter);
  }

  if (expect === "success") {
    if (!elements.phase.textContent.includes("成功")) throw new Error("未进入升级成功终态: " + elements.phase.textContent);
    if (!sawProgress) throw new Error("下载过程未出现百分比进度");
    console.log("PASS upgrade progress success flow (progress seen, polling stopped)");
  } else {
    if (!elements.phase.textContent.includes("失败")) throw new Error("未进入升级失败终态: " + elements.phase.textContent);
    if (elements.act.style.display !== "block") throw new Error("失败态未显示返回按钮");
    console.log("PASS upgrade progress fail flow (fail shown, retry entry, polling stopped)");
  }
  process.exit(0);
})().catch(e => { console.error(e.stack || e.message || e); process.exit(1); });
"""


def _run_node(node: str, script: str, *args) -> int:
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False, encoding="utf-8") as f:
        f.write(script)
        js_path = f.name
    try:
        r = subprocess.run([node, js_path, *args], text=True, capture_output=True, timeout=40)
        if r.stdout:
            print(r.stdout.rstrip())
        if r.stderr:
            print(r.stderr.rstrip(), file=sys.stderr)
        return r.returncode
    finally:
        os.unlink(js_path)


def run_self_test(port: int, mode: str) -> int:
    if mode != "update":
        print(f"{TAG} --self-test 当前只验证 update 成功路径，mode={mode} 不适用", file=sys.stderr)
        return 1
    node = shutil.which("node")
    if not node:
        print(f"{TAG} node 不可用，无法执行页面 JS 自测", file=sys.stderr)
        return 1

    # 1) /ota 检查页流程（update 成功路径）
    httpd = run_server(port, "update")
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{port}"
    print(f"{TAG} mock server: {base}/ota")
    time.sleep(0.2)
    try:
        rc = _run_node(node, NODE_SELF_TEST, base)
        if rc != 0:
            return rc
    finally:
        httpd.shutdown()
        httpd.server_close()

    # 2) /ota/upgrade/status 进度页：成功分支
    httpd = run_server(port, "update")
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    print(f"{TAG} 升级进度自测（成功分支）")
    time.sleep(0.2)
    try:
        rc = _run_node(node, NODE_UPGRADE_TEST, base, "success")
        if rc != 0:
            return rc
    finally:
        httpd.shutdown()
        httpd.server_close()

    # 3) /ota/upgrade/status 进度页：失败分支
    httpd = run_server(port, "error")
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    print(f"{TAG} 升级进度自测（失败分支）")
    time.sleep(0.2)
    try:
        rc = _run_node(node, NODE_UPGRADE_TEST, base, "fail")
        if rc != 0:
            return rc
    finally:
        httpd.shutdown()
        httpd.server_close()

    print(f"{TAG} ALL PASS: /ota 检查 + 升级进度成功/失败分支全部通过")
    return 0


def main():
    parser = argparse.ArgumentParser(description="BikePower OTA 本地页面模拟器")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--mode", choices=["update", "no-update", "error"], default="update")
    parser.add_argument("--serve", action="store_true", help="启动 mock server，手动浏览页面")
    parser.add_argument("--self-test", action="store_true", help="启动 mock server 并自动执行页面 JS 流程")
    args = parser.parse_args()

    if args.self_test:
        return run_self_test(args.port, args.mode)
    if args.serve:
        httpd = run_server(args.port, args.mode)
        print(f"{TAG} mock server: http://127.0.0.1:{args.port}/ota")
        print(f"{TAG} mode={args.mode}，按 Ctrl+C 退出")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print()
        finally:
            httpd.server_close()
        return 0

    parser.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
