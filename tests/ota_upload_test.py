#!/usr/bin/env python3
"""OTA 升级上传测试：向 /ota/upload 上传固件 bin，校验响应。

前置：设备已进入配网模式，本机已连上 BikePower AP（网关 192.168.4.1）。
用法:
  python3 tests/ota_upload_test.py --file /tmp/ota_target_v315.bin --expect success
  python3 tests/ota_upload_test.py --file /tmp/ota_baseline_v314.bin --expect fail
"""
import argparse
import sys
import urllib.request
import urllib.error


def upload(url, path):
    with open(path, "rb") as f:
        data = f.read()
    boundary = "----BikePowerOTAboundary123456"
    body = b"--" + boundary.encode() + b"\r\n"
    body += b'Content-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\n'
    body += b"Content-Type: application/octet-stream\r\n\r\n"
    body += data
    body += b"\r\n--" + boundary.encode() + b"--\r\n"
    req = urllib.request.Request(
        url, data=body, method="POST",
        headers={
            "Content-Type": f"multipart/form-data; boundary={boundary}",
            "Connection": "close",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            return resp.read(), resp.status
    except urllib.error.HTTPError as e:
        return e.read(), e.code


def main():
    p = argparse.ArgumentParser(description="OTA 上传测试")
    p.add_argument("--ip", default="192.168.4.1")
    p.add_argument("--port", type=int, default=80)
    p.add_argument("--file", required=True)
    p.add_argument("--expect", default="success", choices=["success", "fail"])
    args = p.parse_args()
    url = f"http://{args.ip}:{args.port}/ota/upload"
    print(f"上传 {args.file} ({__import__('os').path.getsize(args.file)} 字节) -> {url}")
    body, status = upload(url, args.file)
    text = body.decode("utf-8", errors="replace")
    print(f"HTTP status={status}")
    print(f"响应前 400 字符:\n{text[:400]}")
    if args.expect == "success":
        ok = status == 200 and "成功" in text
    else:
        ok = status == 200 and ("失败" in text or "降级" in text or "拒绝" in text)
    print("判定:", "通过" if ok else "失败")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
