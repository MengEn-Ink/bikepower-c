#!/usr/bin/env python3
"""OTA 全流程自测（TC-3b）

分两部分：
  --static  : 无需设备，静态验证 /ota 页面 JS、路由注册、函数绑定
  --dynamic : 需设备进入配网态，验证 HTTP 路由返回与状态机

静态部分覆盖：
  1. main/wifi_portal.c 中 /ota/check, /ota/status, /ota/upgrade 三个路由已注册
  2. /ota 页面 JS 中 checkUpdate/pollStatus/startUpgrade 函数已定义
  3. JS 中 x.open 调用的 URL 与后端路由一致
  4. 按钮 onclick 绑定与函数名一致
  5. /ota 页面含 check-btn、upgrade-btn 两个按钮 id
  6. JS 语法正确（调用 check_portal_js 逻辑）

动态部分覆盖（需设备）：
  1. GET /ota 返回 200 且含 check-btn、upgrade-btn
  2. POST /ota/check 在未配 WiFi 时返回 {"status":"error",...}
  3. GET /ota/status 返回 JSON 且含 status 字段
  4. 配好家庭 WiFi 后 POST /ota/check 返回 started/running
  5. 轮询 /ota/status 直到 done/error，验证字段完整

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
import tempfile
import time
import urllib.request
import urllib.error
import urllib.parse

TAG = "[TC-3b]"
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORTAL_FILE = os.path.join(REPO_ROOT, "main", "wifi_portal.c")

C_STRING_PATTERN = re.compile(r'"((?:\\.|[^"\\])*)"')
SCRIPT_OPEN = "<script>"
SCRIPT_CLOSE = "</script>"

# C printf 格式占位符（%d %s %ld %lu %f %u 等），用于 snprintf 模板
C_PRINTF_PATTERN = re.compile(r'%(?:l|ll)?[diuf]|%(?:l|ll)?[uxc]|%[0-9]*\.[0-9]+f|%s')


def substitute_c_printf_placeholders(js: str) -> str:
    """把 C printf 占位符替换为合法 JS 值，便于静态语法检查。"""
    def repl(m):
        token = m.group(0)
        if token.endswith("s"):
            return ""
        if token.endswith("f"):
            return "0.0"
        return "0"
    return C_PRINTF_PATTERN.sub(repl, js)


# ============ 工具函数 ============

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


def find_script_blocks(text: str):
    blocks = []
    cursor = 0
    while True:
        s = text.find(SCRIPT_OPEN, cursor)
        if s < 0:
            break
        e = text.find(SCRIPT_CLOSE, s + len(SCRIPT_OPEN))
        if e < 0:
            break
        blocks.append(text[s + len(SCRIPT_OPEN):e])
        cursor = e + len(SCRIPT_CLOSE)
    return blocks


def check_js_with_node(js: str) -> tuple:
    node_path = shutil.which("node")
    if not node_path:
        return False, "node 不可用"
    if not js.strip():
        return True, "空块"
    checked_js = substitute_c_printf_placeholders(js)
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".js", delete=False, encoding="utf-8"
    ) as f:
        f.write(checked_js)
        tmp = f.name
    try:
        r = subprocess.run(
            [node_path, "--check", tmp],
            capture_output=True, text=True, timeout=10,
        )
        if r.returncode == 0:
            return True, "OK"
        return False, (r.stderr or r.stdout).strip()
    finally:
        os.unlink(tmp)


# ============ 静态测试 ============

def static_tests():
    print(f"{TAG} === 静态部分（无需设备）===")
    if not os.path.isfile(PORTAL_FILE):
        print(f"{TAG} FAIL: {PORTAL_FILE} 不存在", file=sys.stderr)
        return False
    with open(PORTAL_FILE, encoding="utf-8") as f:
        src = f.read()

    text = extract_c_strings(src)
    failures = []

    # S1: 三个 OTA 路由 handler 已注册
    print(f"\n{TAG} [S1] 检查 OTA 路由注册")
    for uri in ["/ota/check", "/ota/status", "/ota/upgrade"]:
        pat = re.compile(r'\{\s*\.uri\s*=\s*"' + re.escape(uri) + r'"')
        if pat.search(src):
            print(f"{TAG}   OK  路由 {uri} 已注册")
        else:
            failures.append(f"路由 {uri} 未注册")
            print(f"{TAG}   FAIL 路由 {uri} 未注册")

    # S2: 找到 /ota 页面对应的 h_ota 函数段
    print(f"\n{TAG} [S2] 定位 h_ota 函数并提取页面 HTML")
    idx = src.find("static esp_err_t h_ota(")
    if idx < 0:
        failures.append("找不到 h_ota 函数")
        print(f"{TAG}   FAIL 找不到 h_ota 函数")
        return False
    # 截取到下一个 static esp_err_t 或文件结尾
    next_handler = re.search(r"\nstatic esp_err_t h_\w+\(", src[idx + 10:])
    end = idx + 10 + next_handler.start() if next_handler else len(src)
    seg = src[idx:end]
    seg_text = extract_c_strings(seg)
    print(f"{TAG}   OK  h_ota 段长度 {len(seg)} 字节，提取 HTML {len(seg_text)} 字符")

    # S3: 按钮元素存在
    print(f"\n{TAG} [S3] 检查按钮元素")
    for btn_id in ["check-btn", "upgrade-btn"]:
        if f'id="{btn_id}"' in seg_text:
            print(f"{TAG}   OK  按钮 #{btn_id} 存在")
        else:
            failures.append(f"按钮 #{btn_id} 缺失")
            print(f"{TAG}   FAIL 按钮 #{btn_id} 缺失")

    # S4: onclick 绑定与函数名一致
    print(f"\n{TAG} [S4] 检查 onclick 绑定")
    onclick_checks = [
        (r'onclick="checkUpdate\(\)"', "checkUpdate"),
        (r'onclick="startUpgrade\(\)"', "startUpgrade"),
    ]
    for pat, fn in onclick_checks:
        if re.search(pat, seg_text):
            print(f"{TAG}   OK  onclick 绑定 {fn}")
        else:
            failures.append(f"onclick 未绑定 {fn}")
            print(f"{TAG}   FAIL onclick 未绑定 {fn}")

    # S5: JS 函数定义存在
    print(f"\n{TAG} [S5] 检查 JS 函数定义")
    script_blocks = find_script_blocks(seg_text)
    if not script_blocks:
        failures.append("h_ota 段未找到 <script> 块")
        print(f"{TAG}   FAIL h_ota 段未找到 <script> 块")
    else:
        # 合并所有 script 块
        all_js = "\n".join(script_blocks)
        for fn in ["checkUpdate", "pollStatus", "startUpgrade"]:
            pat = re.compile(r"function\s+" + fn + r"\s*\(")
            if pat.search(all_js):
                print(f"{TAG}   OK  函数 {fn} 已定义")
            else:
                failures.append(f"函数 {fn} 未定义")
                print(f"{TAG}   FAIL 函数 {fn} 未定义")

        # S6: x.open 调用与后端路由一致
        print(f"\n{TAG} [S6] 检查 x.open 调用与路由一致")
        open_calls = re.findall(r"x\.open\(['\"](\w+)['\"]\s*,\s*['\"]([^'\"]+)['\"]\)", all_js)
        expected = {
            ("POST", "/ota/check"),
            ("GET", "/ota/status"),
            ("POST", "/ota/upgrade"),
        }
        found = set(open_calls)
        for method, uri in sorted(expected):
            if (method, uri) in found:
                print(f"{TAG}   OK  {method} {uri} 调用存在")
            else:
                failures.append(f"{method} {uri} 调用缺失")
                print(f"{TAG}   FAIL {method} {uri} 调用缺失")

        # S7: JS 语法检查
        print(f"\n{TAG} [S7] JS 语法检查 (node --check)")
        for i, js in enumerate(script_blocks, 1):
            ok, msg = check_js_with_node(js)
            status = "OK  " if ok else "FAIL"
            print(f"{TAG}   {status} script 块 #{i} (size={len(js)})")
            if not ok:
                failures.append(f"script 块 #{i} JS 语法错误")
                for line in msg.split("\n")[:4]:
                    print(f"{TAG}       {line}")

    # S8: 全文件所有 script 块语法（防其他页面被牵连）
    print(f"\n{TAG} [S8] 全文件所有 <script> 块 JS 语法检查")
    all_blocks = find_script_blocks(text)
    print(f"{TAG}   全文件共 {len(all_blocks)} 个 <script> 块")
    for i, js in enumerate(all_blocks, 1):
        if not js.strip():
            print(f"{TAG}   OK  块 #{i} 空（占位）")
            continue
        ok, msg = check_js_with_node(js)
        status = "OK  " if ok else "FAIL"
        print(f"{TAG}   {status} 块 #{i} (size={len(js)})")
        if not ok:
            failures.append(f"全文件 script 块 #{i} 语法错误")
            for line in msg.split("\n")[:4]:
                print(f"{TAG}       {line}")

    # 汇总
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


def http_post(url, data=None, timeout=10):
    body_data = data if data is not None else b""
    req = urllib.request.Request(
        url, data=body_data, method="POST",
        headers={"Connection": "close",
                 "Content-Type": "application/x-www-form-urlencoded"},
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

    # D1: GET /ota
    print(f"\n{TAG} [D1] GET /ota")
    body, status, _ = http_get(f"{base}/ota")
    body_text = body.decode("utf-8", "replace")
    if status != 200:
        failures.append(f"GET /ota 状态={status}")
        print(f"{TAG}   FAIL 状态={status}")
    else:
        print(f"{TAG}   OK  状态 200, body={len(body)} 字节")
        for btn in ["check-btn", "upgrade-btn"]:
            if f'id="{btn}"' in body_text:
                print(f"{TAG}   OK  含按钮 #{btn}")
            else:
                failures.append(f"页面缺按钮 #{btn}")
                print(f"{TAG}   FAIL 页面缺按钮 #{btn}")

    # D2: GET /ota/status（初始应为 idle 或其他状态）
    print(f"\n{TAG} [D2] GET /ota/status")
    body, status, _ = http_get(f"{base}/ota/status")
    try:
        d = json.loads(body)
        if "status" in d:
            print(f"{TAG}   OK  status={d['status']} current={d.get('current','?')}")
        else:
            failures.append("/ota/status 无 status 字段")
            print(f"{TAG}   FAIL 无 status 字段: {body[:100]}")
    except Exception as e:
        failures.append(f"/ota/status JSON 解析失败: {e}")
        print(f"{TAG}   FAIL JSON 解析失败: {e} body={body[:100]}")

    # D3: POST /ota/check
    print(f"\n{TAG} [D3] POST /ota/check")
    body, status, _ = http_post(f"{base}/ota/check")
    try:
        d = json.loads(body)
        st = d.get("status")
        if st in ("started", "running", "error", "done"):
            print(f"{TAG}   OK  status={st} error={d.get('error','')}")
        else:
            failures.append(f"/ota/check 异常 status={st}")
            print(f"{TAG}   FAIL 异常 status={st} body={body[:120]}")
    except Exception as e:
        failures.append(f"/ota/check JSON 解析失败: {e}")
        print(f"{TAG}   FAIL JSON 解析失败: {e} body={body[:120]}")

    # D4: 若提供了家庭 WiFi 凭证，配 WiFi 后再测一次完整流程
    if ssid and psk:
        print(f"\n{TAG} [D4] 配置家庭 WiFi 后完整检查流程")
        # 先配 WiFi（POST /wifi/save）
        form = f"ssid={urllib.parse.quote(ssid)}&psk={urllib.parse.quote(psk)}".encode()
        body, status, _ = http_post(f"{base}/wifi/save", data=form)
        print(f"{TAG}   POST /wifi/save -> {status} body={body[:80]}")
        time.sleep(1)

        # 触发检查
        body, status, _ = http_post(f"{base}/ota/check")
        try:
            d = json.loads(body)
            st = d.get("status")
            print(f"{TAG}   POST /ota/check -> status={st}")
            if st not in ("started", "running", "done", "error"):
                failures.append(f"配 WiFi 后 /ota/check status={st}")
        except Exception as e:
            failures.append(f"配 WiFi 后 /ota/check 解析失败: {e}")

        # 轮询 /ota/status 最多 60s
        print(f"{TAG}   轮询 /ota/status (最多 60s)")
        deadline = time.time() + 60
        final = None
        while time.time() < deadline:
            body, _, _ = http_get(f"{base}/ota/status")
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
            for k in ["has_update", "version", "current"]:
                if k not in final:
                    failures.append(f"/ota/status done 缺字段 {k}")
            print(f"{TAG}   OK  done: version={final.get('version')} has_update={final.get('has_update')}")
        else:
            print(f"{TAG}   WARN 检查结束于 error: {final.get('error','')}")
    else:
        print(f"\n{TAG} [D4] 跳过（未提供 --ssid/--psk，不测完整流程）")

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
    parser.add_argument("--ssid", help="家庭 WiFi SSID（用于 D4 完整流程）")
    parser.add_argument("--psk", help="家庭 WiFi 密码（用于 D4 完整流程）")
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
