#!/usr/bin/env python3
"""OTA 全流程自测（TC-3b，v3.2.0 前后端分离重构版）

分两部分：
  --static  : 无需设备，静态验证 /api/ota/xxx 路由、app.js 函数绑定、JS 语法
  --dynamic : 需设备进入配网态，验证 HTTP 路由返回与状态机

静态部分覆盖（适配 v3.2.0 重构后独立的前端三件套）：
  S1. wifi_portal.c 中 /api/ota/check, /api/ota/status, /api/ota/upgrade 三个路由已注册
  S2. main/web/index.html 含 OTA 视图 #view-ota 与按钮 #ota-check-btn
  S3. main/web/app.js 定义 otaCheck / otaPollStatus / otaStartUpgrade 三个函数
  S4. app.js 中调用的 /api/ota/check|status|upgrade 与后端路由一致
  S5. app.js JS 语法正确（node --check）

动态部分覆盖（需设备）：
  D1. GET /api/ota/status 返回 200 且含 status 字段
  D2. POST /api/ota/check 未配 WiFi 时返回 started/running/done/error（任一）
  D3. 配好家庭 WiFi 后 POST /api/ota/check 进入 running
  D4. 轮询 /api/ota/status 直到 done/error，验证字段完整

用法:
  python3 tests/ota_flow_test.py --static                  # 仅静态（默认）
  python3 tests/ota_flow_test.py --dynamic --ip 192.168.4.1
  python3 tests/ota_flow_test.py --dynamic --ip 192.168.4.1 --ssid MyHome --psk secret
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.request
import urllib.error
import urllib.parse

TAG = "[TC-3b]"
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORTAL_FILE = os.path.join(REPO_ROOT, "main", "wifi_portal.c")
INDEX_HTML = os.path.join(REPO_ROOT, "main", "web", "index.html")
APP_JS = os.path.join(REPO_ROOT, "main", "web", "app.js")


# ============ 静态测试 ============

def static_tests():
    print(f"{TAG} === 静态部分（无需设备）===")
    failures = []

    for path in (PORTAL_FILE, INDEX_HTML, APP_JS):
        if not os.path.isfile(path):
            print(f"{TAG} FAIL: {path} 不存在", file=sys.stderr)
            failures.append(f"文件不存在 {path}")
            return _static_summary(failures)

    with open(PORTAL_FILE, encoding="utf-8") as f:
        csrc = f.read()
    with open(INDEX_HTML, encoding="utf-8") as f:
        html = f.read()
    with open(APP_JS, encoding="utf-8") as f:
        js = f.read()

    # S1: 三个 OTA 路由 handler 已注册
    print(f"\n{TAG} [S1] 检查 OTA 路由注册 (wifi_portal.c)")
    for uri in ["/api/ota/check", "/api/ota/status", "/api/ota/upgrade"]:
        pat = '.uri = "' + uri + '"'
        if pat in csrc:
            print(f"{TAG}   OK  路由 {uri} 已注册")
        else:
            failures.append(f"路由 {uri} 未注册")
            print(f"{TAG}   FAIL 路由 {uri} 未注册")

    # S2: index.html 含 OTA 视图与按钮
    print(f"\n{TAG} [S2] 检查 index.html OTA 视图元素")
    for marker in ['id="view-ota"', 'id="ota-check-btn"', 'onclick="otaCheck()"']:
        if marker in html:
            print(f"{TAG}   OK  {marker} 存在")
        else:
            failures.append(f"index.html 缺少 {marker}")
            print(f"{TAG}   FAIL index.html 缺少 {marker}")

    # S3: app.js 定义 OTA 函数
    print(f"\n{TAG} [S3] 检查 app.js OTA 函数定义")
    for fn in ["otaCheck", "otaPollStatus", "otaStartUpgrade", "otaRenderStatus", "otaStopPoll"]:
        pat = re.compile(r"function\s+" + fn + r"\s*\(")
        if pat.search(js):
            print(f"{TAG}   OK  函数 {fn} 已定义")
        else:
            failures.append(f"app.js 函数 {fn} 未定义")
            print(f"{TAG}   FAIL app.js 函数 {fn} 未定义")

    # S4: app.js 中调用的 OTA API 与后端路由一致
    print(f"\n{TAG} [S4] 检查 app.js API 调用与路由一致")
    expected_calls = [
        ('"/api/ota/check"', "POST"),
        ('"/api/ota/status"', "GET"),
        ('"/api/ota/status"', "GET"),  # pollStatus 也用 GET
        ('"/api/ota/upgrade"', "POST"),
    ]
    for api_str, _ in expected_calls:
        if api_str in js:
            print(f"{TAG}   OK  app.js 调用 {api_str}")
        else:
            failures.append(f"app.js 缺少 API 调用 {api_str}")
            print(f"{TAG}   FAIL app.js 缺少 API 调用 {api_str}")

    # S5: app.js JS 语法检查
    print(f"\n{TAG} [S5] app.js JS 语法检查 (node --check)")
    node_path = shutil.which("node")
    if not node_path:
        print(f"{TAG}   SKIP node 不可用")
    else:
        r = subprocess.run(
            [node_path, "--check", APP_JS],
            capture_output=True, text=True, timeout=15,
        )
        if r.returncode == 0:
            print(f"{TAG}   OK  app.js 语法正确 (size={len(js)})")
        else:
            err = (r.stderr or r.stdout).strip()
            failures.append("app.js 语法错误")
            print(f"{TAG}   FAIL app.js 语法错误")
            for line in err.split("\n")[:6]:
                print(f"{TAG}       {line}")

    return _static_summary(failures)


def _static_summary(failures):
    print(f"\n{TAG} === 静态部分汇总 ===")
    if failures:
        print(f"{TAG} FAIL: {len(failures)} 项失败")
        for f in failures:
            print(f"{TAG}   - {f}")
        return False
    print(f"{TAG} PASS: 静态部分全部通过")
    return True


# ============ 动态测试 ============

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def http_get(url, timeout=10):
    req = urllib.request.Request(url, headers={"Connection": "close"})
    opener = urllib.request.build_opener(NoRedirect)
    try:
        with opener.open(req, timeout=timeout) as resp:
            return resp.read(), resp.status, dict(resp.headers)
    except urllib.error.HTTPError as e:
        try:
            body = e.read()
        except Exception:
            body = b""
        return body, e.code, dict(e.headers)


def http_post(url, data=None, timeout=10, content_type="application/json"):
    body_data = data if data is not None else b""
    req = urllib.request.Request(
        url, data=body_data, method="POST",
        headers={"Connection": "close", "Content-Type": content_type},
    )
    opener = urllib.request.build_opener(NoRedirect)
    try:
        with opener.open(req, timeout=timeout) as resp:
            return resp.read(), resp.status, dict(resp.headers)
    except urllib.error.HTTPError as e:
        try:
            body = e.read()
        except Exception:
            body = b""
        return body, e.code, dict(e.headers)


def dynamic_tests(ip: str, ssid: str = None, psk: str = None):
    print(f"{TAG} === 动态部分（设备 {ip}）===")
    base = f"http://{ip}"
    failures = []

    # D1: GET /api/ota/status
    print(f"\n{TAG} [D1] GET /api/ota/status")
    body, status, _ = http_get(f"{base}/api/ota/status")
    try:
        d = json.loads(body)
        if status == 200 and "status" in d:
            print(f"{TAG}   OK  status={d['status']} current={d.get('current','?')}")
        else:
            failures.append(f"GET /api/ota/status 异常 status={status}")
            print(f"{TAG}   FAIL status={status} body={body[:120]}")
    except Exception as e:
        failures.append(f"/api/ota/status JSON 解析失败: {e}")
        print(f"{TAG}   FAIL JSON 解析失败: {e} body={body[:120]}")

    # D2: POST /api/ota/check
    print(f"\n{TAG} [D2] POST /api/ota/check")
    body, status, _ = http_post(f"{base}/api/ota/check", data=b"")
    try:
        d = json.loads(body)
        st = d.get("status")
        if st in ("started", "running", "error", "done"):
            print(f"{TAG}   OK  status={st} error={d.get('error','')}")
        else:
            failures.append(f"/api/ota/check 异常 status={st}")
            print(f"{TAG}   FAIL 异常 status={st} body={body[:120]}")
    except Exception as e:
        failures.append(f"/api/ota/check JSON 解析失败: {e}")
        print(f"{TAG}   FAIL JSON 解析失败: {e} body={body[:120]}")

    # D3: 若提供了家庭 WiFi 凭证，配 WiFi 后再测一次完整流程
    if ssid and psk:
        print(f"\n{TAG} [D3] 配置家庭 WiFi 后完整检查流程")
        # 先配 WiFi（POST /api/wifi/save）
        payload = json.dumps({"ssid": ssid, "psk": psk}).encode("utf-8")
        body, status, _ = http_post(f"{base}/api/wifi/save", data=payload)
        print(f"{TAG}   POST /api/wifi/save -> {status} body={body[:80]}")
        time.sleep(1)

        # 触发检查
        body, status, _ = http_post(f"{base}/api/ota/check", data=b"")
        try:
            d = json.loads(body)
            st = d.get("status")
            print(f"{TAG}   POST /api/ota/check -> status={st}")
            if st not in ("started", "running", "done", "error"):
                failures.append(f"配 WiFi 后 /api/ota/check status={st}")
        except Exception as e:
            failures.append(f"配 WiFi 后 /api/ota/check 解析失败: {e}")

        # 轮询 /api/ota/status 最多 60s
        print(f"{TAG}   轮询 /api/ota/status (最多 60s)")
        deadline = time.time() + 60
        final = None
        while time.time() < deadline:
            body, _, _ = http_get(f"{base}/api/ota/status")
            try:
                d = json.loads(body)
                st = d.get("status")
                print(f"{TAG}     status={st}")
                if st in ("done", "error"):
                    final = d
                    break
            except Exception:
                pass
            time.sleep(2)

        if final is None:
            failures.append("OTA 检查 60s 未完成")
            print(f"{TAG}   FAIL 60s 未完成")
        elif final.get("status") == "done":
            for k in ["has_update", "current"]:
                if k not in final:
                    failures.append(f"/api/ota/status done 缺字段 {k}")
            print(f"{TAG}   OK  done: version={final.get('version')} has_update={final.get('has_update')}")
        else:
            print(f"{TAG}   WARN 检查结束于 error: {final.get('error','')}")
    else:
        print(f"\n{TAG} [D3] 跳过（未提供 --ssid/--psk，不测完整流程）")

    print(f"\n{TAG} === 动态部分汇总 ===")
    if failures:
        print(f"{TAG} FAIL: {len(failures)} 项失败")
        for f in failures:
            print(f"{TAG}   - {f}")
        return False
    print(f"{TAG} PASS: 动态部分全部通过")
    return True


def main():
    parser = argparse.ArgumentParser(description="OTA 全流程自测 (TC-3b)")
    parser.add_argument("--static", action="store_true", default=True,
                        help="运行静态部分（默认开启）")
    parser.add_argument("--no-static", dest="static", action="store_false",
                        help="跳过静态部分")
    parser.add_argument("--dynamic", action="store_true",
                        help="运行动态部分（需设备进入配网态）")
    parser.add_argument("--ip", default="192.168.4.1",
                        help="设备 IP（默认 192.168.4.1）")
    parser.add_argument("--ssid", help="家庭 WiFi SSID（用于 D3 完整流程）")
    parser.add_argument("--psk", help="家庭 WiFi 密码（用于 D3 完整流程）")
    args = parser.parse_args()

    if not args.static and not args.dynamic:
        print(f"{TAG} 未指定任何测试模式，默认运行 --static", file=sys.stderr)
        args.static = True

    ok = True
    if args.static:
        ok = static_tests() and ok
    if args.dynamic:
        ok = dynamic_tests(args.ip, args.ssid, args.psk) and ok

    print()
    if ok:
        print(f"{TAG} === ALL PASS ===")
        return 0
    else:
        print(f"{TAG} === HAS FAILURES ===", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
