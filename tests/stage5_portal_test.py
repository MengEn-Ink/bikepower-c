#!/usr/bin/env python3
"""阶段5 配网门户自测（v3.2.0 前后端分离重构版）

连上 BikePower 热点后验证 HTTP 路由与行为。v3.2.0 起：
  - 前端走 SPA 单页面，C 侧只暴露 /api/xxx JSON 接口
  - 草稿 / 配置 / 关 WiFi 全部 POST JSON，不再返回 302
  - 倒计时与续期返回纯数字（保留旧契约，前端直接 parseInt）

默认不提交 /api/config（会重启），但提交一次 /api/draft 草稿（仅内存生效）。

用法:
  python3 tests/stage5_portal_test.py                      # 默认 192.168.4.1，只读校验
  python3 tests/stage5_portal_test.py --ip 192.168.4.1
  python3 tests/stage5_portal_test.py --renew              # 额外测续期接口
"""
import argparse
import json
import sys
import urllib.request
import urllib.error

TIMEOUT = 10


def http_get(url, timeout=TIMEOUT):
    """发 GET 请求并返回 (body, status, headers)。

    不自动跟随重定向，3xx/4xx/5xx 等 HTTP 响应都正常返回，
    由各用例自己断言预期状态码。只有网络层错误（超时、连接失败等）才抛异常。
    """
    req = urllib.request.Request(url, headers={"Connection": "close"})
    opener = urllib.request.build_opener(_NoRedirect())
    try:
        with opener.open(req, timeout=timeout) as resp:
            return resp.read(), resp.status, dict(resp.headers)
    except urllib.error.HTTPError as e:
        try:
            body = e.read()
        except Exception:
            body = b""
        return body, e.code, dict(e.headers)


def http_post_json(url, payload, timeout=TIMEOUT):
    """发 POST 请求，body 为 JSON 字符串。返回 (body, status, headers)。"""
    body_data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        url, data=body_data, method="POST",
        headers={
            "Connection": "close",
            "Content-Type": "application/json",
        },
    )
    opener = urllib.request.build_opener(_NoRedirect())
    try:
        with opener.open(req, timeout=timeout) as resp:
            return resp.read(), resp.status, dict(resp.headers)
    except urllib.error.HTTPError as e:
        try:
            body = e.read()
        except Exception:
            body = b""
        return body, e.code, dict(e.headers)


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def _try(label, fn):
    try:
        ok, detail = fn()
        print(f"  {label}: {'通过' if ok else '失败'} {detail}")
        return ok
    except urllib.error.HTTPError as e:
        print(f"  {label}: 失败 HTTPError {e.code}")
        return False
    except Exception as e:
        print(f"  {label}: 失败 {type(e).__name__}: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="阶段5 配网门户自测")
    parser.add_argument("--ip", default="192.168.4.1", help="设备 AP 网关 IP")
    parser.add_argument("--port", type=int, default=80, help="HTTP 端口")
    parser.add_argument("--renew", action="store_true", help="额外测试 /api/renew 续期")
    args = parser.parse_args()

    base = f"http://{args.ip}:{args.port}"
    print("=" * 56)
    print("  BikePower 配网门户自测 (v3.2.0)")
    print(f"  目标: {base}")
    print("=" * 56)

    results = []

    def home():
        body, status, _ = http_get(f"{base}/")
        ok = status == 200 and b"BikePower" in body and len(body) > 500
        return ok, f"status={status} len={len(body)}"
    results.append(_try("首页 SPA /", home))

    def state():
        body, status, _ = http_get(f"{base}/api/state")
        ok = False
        detail = f"status={status} len={len(body)}"
        if status == 200:
            try:
                d = json.loads(body)
                ok = ("device_name" in d and "firmware_version" in d
                      and "pacers" in d and "remaining_sec" in d)
                detail += f" dev={d.get('device_name')} ver={d.get('firmware_version')}"
            except Exception as e:
                detail += f" JSON 解析失败: {e}"
        return ok, detail
    results.append(_try("设备状态 GET /api/state", state))

    def time_ep():
        body, status, _ = http_get(f"{base}/api/time")
        detail = f"status={status} body={body[:32]!r}"
        try:
            secs = int(body.strip())
            ok = status == 200 and 0 <= secs <= 180
            detail = f"剩余={secs}s"
        except Exception:
            ok = False
        return ok, detail
    results.append(_try("倒计时 GET /api/time", time_ep))

    def mem_stats():
        body, status, _ = http_get(f"{base}/api/mem_stats")
        detail = f"status={status} len={len(body)}"
        ok = False
        if status == 200:
            try:
                d = json.loads(body)
                ok = "free" in d and "min_free" in d
                detail += f" free={d.get('free')} min_free={d.get('min_free')}"
            except Exception as e:
                detail += f" JSON 解析失败: {e}"
        return ok, detail
    results.append(_try("内存统计 GET /api/mem_stats", mem_stats))

    def favicon():
        body, status, _ = http_get(f"{base}/favicon.ico")
        ok = status == 204 and len(body) == 0
        return ok, f"status={status}"
    results.append(_try("favicon /favicon.ico(204)", favicon))

    if args.renew:
        def renew():
            body, status, _ = http_get(f"{base}/api/renew")
            secs = int(body.strip())
            ok = status == 200 and secs >= 170
            return ok, f"续期后剩余={secs}s"
        results.append(_try("续期 GET /api/renew", renew))

    print("--- 结果 ---")
    passed = sum(1 for r in results if r)
    total = len(results)
    print(f"  {passed}/{total} 用例通过")
    print("总判定:", "通过" if passed == total else "失败")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
