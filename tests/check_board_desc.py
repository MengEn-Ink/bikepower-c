#!/usr/bin/env python3
"""
TC-3f 板型描述表静态一致性检查（无需设备）

校验 main/board_detect.c 的板型描述表 s_board_desc[] 与
main/bikepower_config.h 的板型枚举/引脚/名称宏保持一致，防止：
- 新增板型枚举但描述表漏写一行
- 描述表使用未定义的引脚宏
- 板型名称宏与固件包命名不一致

校验项：
1. config 头文件定义的 BOARD_*_C3 枚举数量 = 描述表行数。
2. 描述表每行使用的 BTN_PIN_* / LED_PIN_* / LED_DRV_* / BOARD_NAME_* 必须在 config 中定义。
3. Kconfig 板型宏 CONFIG_BOARD_*_C3 与枚举值一一对应。
4. BOARD_NAME_* 字符串符合 kebab-case + 末尾 -c3 命名规范。

设计：纯静态文本匹配，不依赖编译器。

用法:
    python3 tests/check_board_desc.py
退出码: 0 全绿, 1 有失败
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG_H = ROOT / "main" / "bikepower_config.h"
BOARD_C = ROOT / "main" / "board_detect.c"

TAG = "[TC-3f]"
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
    if not CONFIG_H.is_file() or not BOARD_C.is_file():
        print(f"{TAG} FAIL: 文件不存在", file=sys.stderr)
        return 1

    config_src = CONFIG_H.read_text(encoding="utf-8")
    board_src = BOARD_C.read_text(encoding="utf-8")

    # 1. 提取 config 中的板型枚举
    board_enums = re.findall(r'\b(BOARD_\w+_C3)\b', config_src)
    board_enums = sorted(set(board_enums))
    # 排除哨兵
    board_enums = [e for e in board_enums if e != "BOARD_TYPE_COUNT"]

    # 2. 提取 config 中的引脚/名称宏定义（LED_DRV_* 是 enum 值，单独处理）
    pin_macros = set(re.findall(r'#define\s+((?:BTN_PIN|LED_PIN|BOARD_NAME)_\w+)', config_src))
    led_drv_enums = set(re.findall(r'\b(LED_DRV_\w+)\b', config_src))

    # 3. 提取描述表行
    desc_rows = re.findall(r'\[(BOARD_\w+_C3)\]\s*=\s*\{([^}]+)\}', board_src)

    print(f"{TAG} config 定义板型枚举: {board_enums}")
    print(f"{TAG} config 定义引脚/驱动/名称宏: {sorted(pin_macros)}")
    print(f"{TAG} board_detect.c 描述表行数: {len(desc_rows)}")

    # 校验 1：枚举数量 = 描述表行数
    print(f"\n{TAG} [1/3] 枚举 vs 描述表行数")
    check(len(board_enums) == len(desc_rows),
          f"枚举数({len(board_enums)}) != 描述表行数({len(desc_rows)})")

    # 校验 2：描述表每行使用的枚举必须在 config 中定义
    print(f"{TAG} [2/3] 描述表枚举合法性")
    for enum, body in desc_rows:
        check(enum in board_enums,
              f"描述表使用枚举 {enum} 未在 config 中定义")
        # 提取行内使用的所有宏/枚举（BTN_PIN_*, LED_PIN_*, BOARD_NAME_* 是 #define；LED_DRV_* 是 enum）
        used = re.findall(r'\b(BTN_PIN_\w+|LED_PIN_\w+|LED_DRV_\w+|BOARD_NAME_\w+)\b', body)
        for u in used:
            if u.startswith("LED_DRV_"):
                check(u in led_drv_enums,
                      f"描述表 [{enum}] 使用枚举 {u} 未在 config 中定义")
            else:
                check(u in pin_macros,
                      f"描述表 [{enum}] 使用宏 {u} 未在 config 中定义")

    # 校验 3：Kconfig 板型宏与枚举一一对应
    print(f"{TAG} [3/3] Kconfig 板型宏一致性")
    for enum in board_enums:
        # BOARD_GENERIC_C3 -> CONFIG_BOARD_GENERIC_C3
        kconfig_macro = f"CONFIG_{enum}"
        check(kconfig_macro in board_src,
              f"Kconfig 宏 {kconfig_macro} 未在 board_detect.c 中使用（无法切换到该板型）")

    # 校验 4：BOARD_NAME_* 命名规范（kebab-case + -c3 结尾）
    name_defs = dict(re.findall(r'#define\s+(BOARD_NAME_\w+)\s+"([^"]+)"', config_src))
    for macro, name in name_defs.items():
        ok = bool(re.match(r'^[a-z0-9]+(-[a-z0-9]+)*-c3$', name))
        check(ok,
              f"{macro} = {name!r} 不符合 kebab-case + '-c3' 后缀命名规范")

    # 总结
    print(f"\n{TAG} 结果: {g_pass} 通过, {g_fail} 失败")
    if g_fail == 0:
        print(f"{TAG} PASS: 板型描述表静态一致性全部通过")
        return 0
    else:
        print(f"{TAG} FAIL: 存在不一致，禁止编译/烧录", file=sys.stderr)
        for m in failures:
            print(f"{TAG}   - {m}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
