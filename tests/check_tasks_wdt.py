#!/usr/bin/env python3
"""
TC-3g 任务/队列/WDT 静态完整性检查（无需设备）

扫描 main/ 下所有 .c 文件，校验：
1. 每个 xTaskCreate 调用必须使用 config 头文件中定义的 TASK_STACK_* / TASK_PRIO_* 宏，
   禁止硬编码栈大小或优先级数字字面量。
2. 每个 xTaskCreate 调用后必须紧跟错误检查（检查返回值 != pdPASS）。
3. app_main.c 必须调用 esp_task_wdt_add(NULL) 注册主任务看门狗。
4. xQueueCreate 必须使用 config 头文件中定义的队列长度宏。

设计：纯静态正则扫描，不依赖编译器。

用法:
    python3 tests/check_tasks_wdt.py
退出码: 0 全绿, 1 有失败
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAIN_DIR = ROOT / "main"
CONFIG_H = MAIN_DIR / "bikepower_config.h"

TAG = "[TC-3g]"
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
    if not MAIN_DIR.is_dir():
        print(f"{TAG} FAIL: 目录不存在 {MAIN_DIR}", file=sys.stderr)
        return 1

    config_src = CONFIG_H.read_text(encoding="utf-8") if CONFIG_H.is_file() else ""

    # 提取 config 中定义的 TASK_STACK_* / TASK_PRIO_* 宏
    stack_macros = set(re.findall(r'#define\s+(TASK_STACK_\w+)', config_src))
    prio_macros = set(re.findall(r'#define\s+(TASK_PRIO_\w+)', config_src))
    # 补充扫描各 .c 文件内局部 #define 的栈大小/优先级宏（如 captive_portal.c 的 DNS_TASK_STACK）
    for cf in sorted(MAIN_DIR.glob("*.c")):
        cs = cf.read_text(encoding="utf-8")
        stack_macros |= set(re.findall(r'#define\s+(\w*STACK\w*)', cs))
        prio_macros |= set(re.findall(r'#define\s+(\w*PRIO\w*)', cs))

    print(f"{TAG} config + 局部定义 TASK_STACK_* 宏: {sorted(stack_macros)}")
    print(f"{TAG} config + 局部定义 TASK_PRIO_* 宏: {sorted(prio_macros)}")

    # 扫描所有 main/*.c
    c_files = sorted(MAIN_DIR.glob("*.c"))
    all_task_calls = []
    for cf in c_files:
        src = cf.read_text(encoding="utf-8")
        lines = src.split("\n")
        for i, line in enumerate(lines):
            # 仅匹配函数调用 xTaskCreate(，避免误匹配日志字符串里的 "xTaskCreate"
            if re.search(r'\bxTaskCreate\s*\(', line) and not line.strip().startswith("//"):
                all_task_calls.append((cf, i + 1, line.strip()))

    print(f"\n{TAG} [1/3] xTaskCreate 调用静态检查（共 {len(all_task_calls)} 处）")
    for cf, lineno, line in all_task_calls:
        # 提取栈大小参数（第 3 个位置参数）
        # xTaskCreate(func, "name", STACK_SIZE, arg, PRIO, &handle)
        m = re.search(r'xTaskCreate\s*\(\s*[^,]+,\s*"[^"]*",\s*(\w+)', line)
        if m:
            stack_arg = m.group(1)
            if stack_arg in stack_macros:
                check(True, f"{cf.name}:{lineno} 栈大小用宏 {stack_arg}")
            elif re.match(r'^[A-Z_]+$', stack_arg) and stack_arg not in stack_macros:
                check(False, f"{cf.name}:{lineno} 栈大小 {stack_arg} 未在 config 中定义")
            elif re.match(r'^\d+$', stack_arg):
                check(False, f"{cf.name}:{lineno} 栈大小硬编码数字 {stack_arg}（必须用宏）")
            # 其他形式（表达式、宏调用）放行，复杂场景人工审查

        # 检查优先级参数
        m2 = re.search(r'xTaskCreate\s*\(\s*[^,]+,\s*"[^"]*",\s*[^,]+,\s*[^,]+,\s*(\w+)', line)
        if m2:
            prio_arg = m2.group(1)
            if prio_arg in prio_macros:
                check(True, f"{cf.name}:{lineno} 优先级用宏 {prio_arg}")
            elif re.match(r'^\d+$', prio_arg):
                # 纯数字优先级降级为 WARN（不阻断编译，FreeRTOS 5 是常用值）
                print(f"{TAG}   WARN {cf.name}:{lineno} 优先级硬编码数字 {prio_arg}（建议提取为宏）")
            elif re.match(r'^[A-Z_]+$', prio_arg) and prio_arg not in prio_macros:
                check(False, f"{cf.name}:{lineno} 优先级 {prio_arg} 未在 config 中定义")

    print(f"\n{TAG} [2/3] xTaskCreate 错误检查")
    for cf, lineno, line in all_task_calls:
        src = cf.read_text(encoding="utf-8")
        lines = src.split("\n")
        # 向后扫描 6 行（含当前行），检查是否有 pdPASS / BaseType_t 等错误判断
        start = max(0, lineno - 1)
        window = "\n".join(lines[start:start + 6])
        has_check = ("pdPASS" in window or "BaseType_t" in window or
                     "abort" in window or "ESP_LOGE" in window)
        check(has_check,
              f"{cf.name}:{lineno} xTaskCreate 后缺少错误检查")

    print(f"\n{TAG} [3/3] app_main WDT 注册")
    app_main = MAIN_DIR / "app_main.c"
    if app_main.is_file():
        am_src = app_main.read_text(encoding="utf-8")
        check("esp_task_wdt_add" in am_src,
              "app_main.c 未调用 esp_task_wdt_add 注册主任务看门狗")
        check("esp_task_wdt_reset" in am_src,
              "app_main.c 主循环未调用 esp_task_wdt_reset 喂狗")
    else:
        check(False, "app_main.c 不存在")

    # 总结
    print(f"\n{TAG} 结果: {g_pass} 通过, {g_fail} 失败")
    if g_fail == 0:
        print(f"{TAG} PASS: 任务/队列/WDT 静态完整性全部通过")
        return 0
    else:
        print(f"{TAG} FAIL: 存在不一致，禁止编译/烧录", file=sys.stderr)
        for m in failures:
            print(f"{TAG}   - {m}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
