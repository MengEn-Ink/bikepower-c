#!/usr/bin/env python3
"""从生产源码生成事实摘要；不会根据历史测试报告推断当前验收结果。"""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "docs/dev/21-generated-project-facts.md"


def define(source, name):
    match = re.search(rf"^#define\s+{name}\s+(.+?)(?:\s*//.*)?$", source, re.M)
    if not match:
        raise ValueError(f"缺少宏 {name}")
    return match.group(1).strip().strip('"')


def render(root):
    config = (root / "main/bikepower_config.h").read_text()
    domain = (root / "components/bikepower_domain/include/domain_config.h").read_text()
    cmake = (root / "main/CMakeLists.txt").read_text()
    srclist = cmake.split("SRCS", 1)[1].split("INCLUDE_DIRS", 1)[0]
    modules = re.findall(r'"([^"]+\.c)"', srclist)
    domain_modules = sorted((root / "components/bikepower_domain").glob("*.c"))
    portal = (root / "main/wifi_portal.c").read_text()
    routes = re.findall(r'\.uri\s*=\s*("[^"]+"|CAPTIVE_PORTAL_API_PATH)\s*,\s*\.method\s*=\s*HTTP_(\w+)', portal)
    lines = [
        "# 源码事实摘要", "",
        "> 由 `python3 scripts/tools/doc_facts.py --write` 生成；请修改源码后重新生成，不手工编辑。", "",
        f"- 固件版本：v{define(config, 'FIRMWARE_VERSION')}",
        f"- 配置 schema：{define(domain, 'CONFIG_VERSION')}",
        f"- 配网超时：{int(define(config, 'WIFI_SHUTDOWN_MS')) // 1000} 秒，仅显式续期",
        f"- BLE 通知周期：{define(config, 'BLE_NOTIFY_INTERVAL_MS')} 毫秒",
        f"- 已注册 HTTP 路由：{len(routes)} 个（按 method + URI 计数）",
        f"- 板级生产模块：{len(modules)} 个",
        f"- 领域生产模块：{len(domain_modules)} 个", "",
        "## HTTP 路由", "",
    ]
    for uri, method in routes:
        if uri == "CAPTIVE_PORTAL_API_PATH":
            uri = define(config, uri)
        else:
            uri = uri.strip('"')
        lines.append(f"- `{method} {uri}`")
    lines += ["", "## 生产模块", ""]
    lines += [f"- `main/{module}`" for module in modules]
    lines += [f"- `{module.relative_to(root).as_posix()}`" for module in domain_modules]
    lines += [
        "", "## 验收边界", "",
        "断言数与 PASS 数量来自具体测试运行，不能由文件数量推断。"
        "本摘要不代表构建或真机测试通过；验收结果见 PROGRESS 与对应镜像的证据。", "",
    ]
    return "\n".join(lines)


def matches(root, output):
    return output.is_file() and output.read_text() == render(root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="重新生成摘要")
    parser.add_argument("--check", action="store_true", help="校验摘要与源码一致（默认）")
    args = parser.parse_args()
    if args.write:
        OUTPUT.write_text(render(ROOT), encoding="utf-8")
        print(f"[doc-facts] 已生成 {OUTPUT.relative_to(ROOT)}")
        return 0
    if not matches(ROOT, OUTPUT):
        print("[doc-facts] FAIL 源码摘要缺失或过期，请运行 --write")
        return 1
    print("[doc-facts] PASS 源码摘要一致")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
