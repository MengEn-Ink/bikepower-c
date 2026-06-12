#!/usr/bin/env python3
"""阶段5 配网门户自测：连上 BikePower 热点后验证 HTTP 路由与行为。

前置：长按 + 二次确认进入配网模式，手机/电脑连上 `BikePower` AP（密码 12345678），
设备网关默认 192.168.4.1。本脚本只做只读 GET 校验，默认不提交 /config（提交会触发重启）。

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
    req = urllib.request.Request(url, headers={"Connection": "close"})
    # 不自动跟随重定向，便于校验 302
    opener = urllib.request.build_opener(_NoRedirect())
    with opener.open(req, timeout=timeout) as resp:
        return resp.read(), resp.status, dict(resp.headers)


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def _try(label, fn):
    try:
        ok, detail = fn()
        print(f"  {label}: {'通过' if ok else '失败'} {detail}")
        return ok
    except urllib.error.HTTPError as e:
        # 302/204 等非 2xx 会进这里，交给各用例自己判断
        print(f"  {label}: 失败 HTTPError {e.code}")
        return False
    except Exception as e:
        print(f"  {label}: 失败 {type(e).__name__}: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description="阶段5 配网门户自测")
    parser.add_argument("--ip", default="192.168.4.1", help="设备 AP 网关 IP")
    parser.add_argument("--port", type=int, default=80, help="HTTP 端口")
    parser.add_argument("--renew", action="store_true", help="额外测试 /renew 续期")
    args = parser.parse_args()

    base = f"http://{args.ip}:{args.port}"
    print("=" * 56)
    print("  BikePower 配网门户自测")
    print(f"  目标: {base}")
    print("=" * 56)

    results = []

    def home():
        body, status, _ = http_get(f"{base}/")
        ok = status == 200 and b"BikePower" in body and len(body) > 500
        return ok, f"status={status} len={len(body)}"
    results.append(_try("首页 /", home))

    def more():
        body, status, _ = http_get(f"{base}/more")
        ok = status == 200 and len(body) > 200
        return ok, f"status={status} len={len(body)}"
    results.append(_try("更多配置 /more", more))

    def ui_module():
        body, status, _ = http_get(f"{base}/ui/module?name=device")
        ok = status == 200 and len(body) > 0
        return ok, f"status={status} len={len(body)}"
    results.append(_try("碎片页 /ui/module?name=device", ui_module))

    def time_ep():
        body, status, _ = http_get(f"{base}/time")
        secs = int(body.strip())
        ok = status == 200 and 0 <= secs <= 180
        return ok, f"剩余={secs}s"
    results.append(_try("倒计时 /time", time_ep))

    def mem_stats():
        body, status, _ = http_get(f"{base}/mem_stats")
        data = json.loads(body.decode())
        ok = status == 200 and "free" in data and "min_free" in data
        return ok, f"free={data.get('free')} min_free={data.get('min_free')}"
    results.append(_try("内存统计 /mem_stats", mem_stats))

    def favicon():
        body, status, _ = http_get(f"{base}/favicon.ico")
        ok = status == 204 and len(body) == 0
        return ok, f"status={status}"
    results.append(_try("favicon /favicon.ico(204)", favicon))

    def success_guard():
        # 未提交配置直访 /success 应 302 回首页
        body, status, headers = http_get(f"{base}/success")
        ok = status == 302 and headers.get("Location") == "/"
        return ok, f"status={status} loc={headers.get('Location')}"
    results.append(_try("成功页直访守卫 /success(302)", success_guard))

    if args.renew:
        def renew():
            body, status, _ = http_get(f"{base}/renew")
            secs = int(body.strip())
            ok = status == 200 and secs >= 170
            return ok, f"续期后剩余={secs}s"
        results.append(_try("续期 /renew", renew))

    print("--- 结果 ---")
    passed = sum(1 for r in results if r)
    total = len(results)
    print(f"  {passed}/{total} 用例通过")
    print("总判定:", "通过" if passed == total else "失败")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
