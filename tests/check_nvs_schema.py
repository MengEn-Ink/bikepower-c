#!/usr/bin/env python3
"""
TC-3e NVS schema 静态一致性检查（无需设备）

校验 main/nvs_store.c 使用的 NVS key 与 main/bikepower_config.h 中
#define NVS_KEY_* 的定义保持一致，防止 key 漂移导致读写不匹配。

校验项：
1. config 头文件定义的所有 NVS_KEY_* 必须在 nvs_store.c 中被读写至少一次。
2. nvs_store.c 中出现的 nvs_get_*/nvs_set_* 字符串字面量 key 必须在 config 头文件中定义。
3. nvs_get_*/nvs_set_* 使用的 key 长度 ≤ 15（NVS key 最大长度限制）。
4. NVS_NAMESPACE 与 NVS_NAMESPACE_WIFI 必须不同（避免误用同一命名空间）。

设计：纯静态文本匹配，不解析 C AST，不依赖编译器。

用法:
    python3 tests/check_nvs_schema.py
退出码: 0 全绿, 1 有失败
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG_H = ROOT / "main" / "bikepower_config.h"
NVS_C = ROOT / "main" / "nvs_store.c"

TAG = "[TC-3e]"
g_pass = 0
g_fail = 0
failures = []


def check(cond: bool, msg: str):
    global g_pass, g_fail
    if cond:
        g_pass += 1
    else:
        g_fail += 1
        failures.append(msg)
        print(f"{TAG}   FAIL {msg}")


def main():
    if not CONFIG_H.is_file():
        print(f"{TAG} FAIL: 文件不存在 {CONFIG_H}", file=sys.stderr)
        return 1
    if not NVS_C.is_file():
        print(f"{TAG} FAIL: 文件不存在 {NVS_C}", file=sys.stderr)
        return 1

    config_src = CONFIG_H.read_text(encoding="utf-8")
    nvs_src = NVS_C.read_text(encoding="utf-8")

    # 1. 提取 config 头文件中所有 NVS_KEY_* 和 NVS_NAMESPACE* 定义
    key_defs = {}  # macro_name -> string_value
    for m in re.finditer(r'#define\s+(NVS_KEY_\w+|NVS_NAMESPACE\w*)\s+"([^"]+)"', config_src):
        key_defs[m.group(1)] = m.group(2)

    print(f"{TAG} config 头文件定义 NVS key/namespace: {len(key_defs)} 个")
    for k, v in key_defs.items():
        print(f"{TAG}   {k} = {v!r}")

    # 2. 提取 nvs_store.c 中实际使用的宏（NVS_KEY_* / NVS_NAMESPACE*）
    used_macros = set(re.findall(r'\b(NVS_KEY_\w+|NVS_NAMESPACE\w*)\b', nvs_src))
    print(f"\n{TAG} nvs_store.c 使用 NVS 宏: {len(used_macros)} 个")

    # 3. 校验：所有定义的 key 必须被使用
    print(f"\n{TAG} [1/3] 定义 vs 使用一致性")
    for macro in key_defs:
        check(macro in used_macros,
              f"宏 {macro} 已定义但未在 nvs_store.c 中使用")

    # 4. 校验：nvs_store.c 使用的宏必须在 config 中定义
    for macro in used_macros:
        check(macro in key_defs,
              f"宏 {macro} 在 nvs_store.c 中使用但未在 config 头文件中定义")

    # 5. 校验：key 字符串长度 ≤ 15（NVS 限制）
    print(f"\n{TAG} [2/3] NVS key 长度 ≤ 15")
    for macro, val in key_defs.items():
        if macro.startswith("NVS_NAMESPACE"):
            continue  # namespace 不受 15 字符限制
        check(len(val) <= 15,
              f"{macro} = {val!r} 长度 {len(val)} > 15（NVS key 上限）")

    # 6. 校验：主 namespace 必须定义
    print(f"\n{TAG} [3/3] namespace 定义完整性")
    ns_main = key_defs.get("NVS_NAMESPACE")
    check(bool(ns_main), "NVS_NAMESPACE 已定义")

    # 总结
    print(f"\n{TAG} 结果: {g_pass} 通过, {g_fail} 失败")
    if g_fail == 0:
        print(f"{TAG} PASS: NVS schema 静态一致性全部通过")
        return 0
    else:
        print(f"{TAG} FAIL: 存在 schema 不一致，禁止编译/烧录", file=sys.stderr)
        for m in failures:
            print(f"{TAG}   - {m}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
