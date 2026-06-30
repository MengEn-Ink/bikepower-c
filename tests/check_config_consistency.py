#!/usr/bin/env python3
"""
TC-3c 常量一致性静态检查（无需设备）

校验 main/bikepower_config.h / domain_config.h / README.md / docs/ 之间的
常量值一致，防止文档与代码漂移。

用法:
    python3 tests/check_config_consistency.py
退出码: 0 全绿, 1 有失败
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG_H = ROOT / "main" / "bikepower_config.h"
DOMAIN_H = ROOT / "components" / "bikepower_domain" / "include" / "domain_config.h"
README = ROOT / "README.md"
PROGRESS = ROOT / "PROGRESS.md"
CHANGELOG = ROOT / "CHANGELOG.md"
DOC_INDEX = ROOT / "docs" / "INDEX.md"
QUICKSTART = ROOT / "docs" / "user" / "01-quickstart.md"
USER_MANUAL = ROOT / "docs" / "user" / "02-user-manual.md"
NIMBLE_REWRITE = ROOT / "docs" / "archive" / "01-cidf-nimble-rewrite.md"
MPY_COMPARISON = ROOT / "docs" / "archive" / "02-micropython-vs-c-comparison.md"
REVIEW_CHECKLIST = ROOT / "docs" / "archive" / "03-review-checklist.md"
HW_CONSTRAINTS = ROOT / ".trae" / "rules" / "hardware-constraints.md"

TAG = "[TC-3c]"
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


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def extract_define(src: str, name: str):
    """从 C 头文件提取 #define NAME value，返回字符串值（去引号）或 None。"""
    m = re.search(rf'^#define\s+{re.escape(name)}\s+(.+)$', src, re.MULTILINE)
    if not m:
        return None
    line = m.group(1)
    # 优先提取引号字符串（处理 URL 含 // 被误判为注释的情况）
    qm = re.search(r'"([^"]*)"', line)
    if qm:
        return qm.group(1)
    # 非字符串：取第一个 token（去除行内注释）
    token = line.split('//')[0].strip().split()[0] if line.split('//')[0].strip() else None
    return token


def extract_int(src: str, name: str):
    v = extract_define(src, name)
    if v is None:
        return None
    # 支持 0x 前缀十六进制
    m = re.search(r'0[xX]([0-9a-fA-F]+)', v)
    if m:
        return int(m.group(1), 16)
    m = re.search(r'-?\d+', v)
    return int(m.group()) if m else None


def extract_changelog_latest_version(changelog: str):
    """从 CHANGELOG 提取最新已发布版本号（## [x.y.z] 格式）。"""
    m = re.search(r'^##\s*\[(\d+\.\d+\.\d+)\]', changelog, re.MULTILINE)
    return m.group(1) if m else None


def extract_changelog_unreleased_has_version_bump(changelog: str, target_ver: str):
    """检查 [未发布] 段是否提到目标版本号。"""
    unreleased = re.search(r'##\s*\[未发布\](.*?)(?=\n##\s*\[)', changelog, re.DOTALL)
    if not unreleased:
        return False
    return target_ver in unreleased.group(1)


# ============================ 读取文件 ============================
config_src = read(CONFIG_H)
domain_src = read(DOMAIN_H)
readme_src = read(README)
progress_src = read(PROGRESS)
changelog_src = read(CHANGELOG)
doc_index_src = read(DOC_INDEX)
quickstart_src = read(QUICKSTART)
user_manual_src = read(USER_MANUAL)
nimble_rewrite_src = read(NIMBLE_REWRITE)
mpy_comparison_src = read(MPY_COMPARISON)
review_checklist_src = read(REVIEW_CHECKLIST)
hw_src = read(HW_CONSTRAINTS) if HW_CONSTRAINTS.exists() else ""


# ============================ 1. 固件版本一致性 ============================
print(f"\n{TAG} [S1] 固件版本号一致性")

fw_ver = extract_define(config_src, "FIRMWARE_VERSION")
check(fw_ver is not None, "FIRMWARE_VERSION 已定义")
print(f"{TAG}   FIRMWARE_VERSION = {fw_ver}")

latest_released = extract_changelog_latest_version(changelog_src)
print(f"{TAG}   CHANGELOG 最新已发布版本 = {latest_released}")

# 当前版本必须 > CHANGELOG 最新已发布版本（发版前未发布段含新版本）
if latest_released:
    def parse_ver(s):
        m = re.match(r'(\d+)\.(\d+)\.(\d+)', s)
        return tuple(int(x) for x in m.groups()) if m else (0, 0, 0)
    cur = parse_ver(fw_ver)
    rel = parse_ver(latest_released)
    check(cur >= rel, f"固件版本 {fw_ver} >= CHANGELOG 最新发布 {latest_released}")

# DEVICE_NAME 一致性
device_name_cfg = extract_define(config_src, "DEVICE_NAME")
device_name_domain = extract_define(domain_src, "DEVICE_NAME_DEFAULT")
check(device_name_cfg == device_name_domain,
      f"DEVICE_NAME({device_name_cfg}) == DEVICE_NAME_DEFAULT({device_name_domain})")

# WIFI_SSID 与 DEVICE_NAME 关系（AP 名沿用设备名前缀）
wifi_ssid = extract_define(config_src, "WIFI_SSID")
check(wifi_ssid is not None and "BikePower" in wifi_ssid,
      f"WIFI_SSID 含 BikePower 前缀 (实际={wifi_ssid})")


# ============================ 2. 按钮时间阈值 ↔ README ============================
print(f"\n{TAG} [S2] 按钮时间阈值 ↔ README 描述")

short_press = extract_int(config_src, "SHORT_PRESS_MS")
hold_ms = extract_int(config_src, "WIFI_BTN_HOLD_MS")
confirm_ms = extract_int(config_src, "CONFIRM_WINDOW_MS")
auto_repeat = extract_int(config_src, "BTN_AUTO_REPEAT_MS")

check(short_press == 500, f"SHORT_PRESS_MS = 500 (实际={short_press})")
check(hold_ms == 2000, f"WIFI_BTN_HOLD_MS = 2000 (实际={hold_ms})")
check(confirm_ms == 3000, f"CONFIRM_WINDOW_MS = 3000 (实际={confirm_ms})")
check(auto_repeat == 200, f"BTN_AUTO_REPEAT_MS = 200 (实际={auto_repeat})")

# README 描述对齐
check("0.5s" in readme_src or "500ms" in readme_src, "README 含短按 0.5s/500ms 描述")
check(re.search(r'2\s*s|2000\s*ms', readme_src), "README 含长按 2s/2000ms 描述")
check(re.search(r'3\s*s|3000\s*ms', readme_src), "README 含确认窗口 3s/3000ms 描述")
check("20W" in readme_src or "20 W" in readme_src, "README 含调功步进 20W 描述")
check("200ms" in readme_src, "README 含自动重复 200ms 描述")


# ============================ 3. 调功步进单一来源 ============================
print(f"\n{TAG} [S3] 领域常量单一来源（不重复定义）")

# bikepower_config.h 应该 include domain_config.h 而非重复定义 POWER_STEP_W 等
check('#include "domain_config.h"' in config_src,
      "bikepower_config.h include domain_config.h（领域常量单一来源）")

# 检查 bikepower_config.h 没有重复定义 domain_config.h 的常量
domain_defines = re.findall(r'^#define\s+(\w+)', domain_src, re.MULTILINE)
config_defines = re.findall(r'^#define\s+(\w+)', config_src, re.MULTILINE)
duplicates = set(domain_defines) & set(config_defines)
# DEVICE_NAME_DEFAULT 在 domain，DEVICE_NAME 在 config，允许不同名
allowed_overlap = set()
real_duplicates = duplicates - allowed_overlap
check(len(real_duplicates) == 0,
      f"bikepower_config.h 与 domain_config.h 无重复定义 (重复={real_duplicates})")


# ============================ 4. WiFi AP 凭证一致性 ============================
print(f"\n{TAG} [S4] WiFi AP 凭证一致性")

wifi_password = extract_define(config_src, "WIFI_PASSWORD")
check(wifi_password == "12345678", f"WIFI_PASSWORD = 12345678 (实际={wifi_password})")

captive_host = extract_define(config_src, "CAPTIVE_PORTAL_HOST")
check(captive_host == "192.168.4.1", f"CAPTIVE_PORTAL_HOST = 192.168.4.1 (实际={captive_host})")
check("192.168.4.1" in readme_src, "README 含网关 192.168.4.1")

wifi_shutdown = extract_int(config_src, "WIFI_SHUTDOWN_MS")
check(wifi_shutdown == 180000, f"WIFI_SHUTDOWN_MS = 180000 (实际={wifi_shutdown})")


# ============================ 4b. 用户文档配网描述一致性 ============================
print(f"\n{TAG} [S4b] 用户文档配网描述一致性")

check(f"v{fw_ver}" in user_manual_src,
      f"docs/user/02-user-manual.md 固件版本 v{fw_ver} 与 FIRMWARE_VERSION 一致")

legacy_wifi_phrases = [
    "保存并重启",
    "设备关闭蓝牙",
    "设备自动重启并恢复蓝牙模式",
    "保存后等设备重启",
    "配网时会暂时关闭 BLE",
    "配网完成后设备重启",
]
user_wifi_docs = {
    "docs/user/01-quickstart.md": quickstart_src,
    "docs/user/02-user-manual.md": user_manual_src,
}
for doc_name, doc_src in user_wifi_docs.items():
    for phrase in legacy_wifi_phrases:
        check(phrase not in doc_src, f"{doc_name} 不含旧版普通配网短语：{phrase}")


# ============================ 4c. 开发者文档配网策略一致性 ============================
print(f"\n{TAG} [S4c] 开发者文档配网策略一致性")

legacy_dev_wifi_phrases = [
    "默认仍采用“配网时停 BLE”",
    "默认仍建议配网时停 BLE",
    "当前项目规则已经把“进入 WiFi 配网前停止 BLE”",
    "保存重启",
    "二者不能同时 init",
    "先停 BLE → 延时 200ms → 启 WiFi",
]
dev_wifi_docs = {
    "docs/archive/01-cidf-nimble-rewrite.md": nimble_rewrite_src,
    "docs/archive/03-review-checklist.md": review_checklist_src,
}
for doc_name, doc_src in dev_wifi_docs.items():
    for phrase in legacy_dev_wifi_phrases:
        check(phrase not in doc_src, f"{doc_name} 不含旧版开发者配网结论：{phrase}")


# ============================ 4d. 技术栈版本文档一致性 ============================
print(f"\n{TAG} [S4d] 技术栈版本文档一致性")

check("ESP-IDF v5.5.1" not in mpy_comparison_src,
      "docs/archive/02-micropython-vs-c-comparison.md 不含过时 ESP-IDF v5.5.1")
check("ESP-IDF v5.5.4" in mpy_comparison_src,
      "docs/archive/02-micropython-vs-c-comparison.md 含当前 ESP-IDF v5.5.4")
check("建议改为 v5.5.4" not in review_checklist_src,
      "docs/archive/03-review-checklist.md 不再以未完成语气建议修复 sdkconfig 版本注释")


# ============================ 4e. 文档索引阶段范围一致性 ============================
print(f"\n{TAG} [S4e] 文档索引阶段范围一致性")

check("阶段 0-9 完成情况" not in doc_index_src,
      "docs/INDEX.md 不含过时阶段 0-9 项目进度描述")
check("阶段 0-10 完成情况" in doc_index_src,
      "docs/INDEX.md 含阶段 0-10 项目进度描述")
if "| 10 |" in progress_src:
    check("阶段 0-10" in doc_index_src,
          "PROGRESS.md 含阶段 10 时 docs/INDEX.md 同步到阶段 0-10")


# ============================ 6. BLE 通知间隔一致性 ============================
print(f"\n{TAG} [S6] BLE 通知间隔一致性")

notify_ms = extract_int(config_src, "BLE_NOTIFY_INTERVAL_MS")
check(notify_ms == 1000, f"BLE_NOTIFY_INTERVAL_MS = 1000 (实际={notify_ms})")

advert_ms = extract_int(config_src, "BLE_ADVERTISING_INTERVAL_MS")
check(advert_ms == 500, f"BLE_ADVERTISING_INTERVAL_MS = 500 (实际={advert_ms})")


# ============================ 7. 内存阈值一致性 ============================
print(f"\n{TAG} [S7] 内存阈值一致性")

heap_warn = extract_int(config_src, "HEAP_WARN_BYTES")
heap_crit = extract_int(config_src, "HEAP_CRIT_BYTES")
check(heap_warn == 40960, f"HEAP_WARN_BYTES = 40960 (实际={heap_warn})")
check(heap_crit == 20480, f"HEAP_CRIT_BYTES = 20480 (实际={heap_crit})")

# hardware-constraints.md 应提到堆告警阈值（40KB/20KB 与 HEAP_WARN/CRIT 对齐）
if hw_src:
    check("40KB" in hw_src or "40 KB" in hw_src or "20480" in hw_src or "20KB" in hw_src,
          "hardware-constraints.md 提到堆告警阈值 40KB/20KB")


# ============================ 8. 任务栈大小非零 ============================
print(f"\n{TAG} [S8] 任务栈大小合理性")

for stk_name in ["TASK_STACK_BTN", "TASK_STACK_LED", "TASK_STACK_HTTPD"]:
    v = extract_int(config_src, stk_name)
    check(v is not None and v >= 1024, f"{stk_name} >= 1024 (实际={v})")


# ============================ 9. FTMS OpCode 唯一性 ============================
print(f"\n{TAG} [S9] FTMS OpCode 值不冲突")

ftms_ops = {
    "FTMS_OP_REQUEST_CONTROL": extract_int(config_src, "FTMS_OP_REQUEST_CONTROL"),
    "FTMS_OP_RESET": extract_int(config_src, "FTMS_OP_RESET"),
    "FTMS_OP_SET_TARGET_POWER": extract_int(config_src, "FTMS_OP_SET_TARGET_POWER"),
    "FTMS_OP_START_RESUME": extract_int(config_src, "FTMS_OP_START_RESUME"),
    "FTMS_OP_STOP_PAUSE": extract_int(config_src, "FTMS_OP_STOP_PAUSE"),
}
values = [v for v in ftms_ops.values() if v is not None]
check(len(values) == len(set(values)), f"FTMS OpCode 值唯一 (实际={values})")


# ============================ 10. 板型枚举完整性 ============================
print(f"\n{TAG} [S10] 板型枚举完整性")

check("BOARD_GENERIC_C3" in config_src, "BOARD_GENERIC_C3 枚举存在")
check("BOARD_M5STAMP_C3" in config_src, "BOARD_M5STAMP_C3 枚举存在")
check("BOARD_TYPE_COUNT" in config_src, "BOARD_TYPE_COUNT 哨兵存在")

btn_generic = extract_int(config_src, "BTN_PIN_GENERIC")
btn_m5stamp = extract_int(config_src, "BTN_PIN_M5STAMP")
check(btn_generic == 9, f"BTN_PIN_GENERIC = 9 (实际={btn_generic})")
check(btn_m5stamp == 3, f"BTN_PIN_M5STAMP = 3 (实际={btn_m5stamp})")

led_generic = extract_int(config_src, "LED_PIN_GENERIC")
led_m5stamp = extract_int(config_src, "LED_PIN_M5STAMP")
check(led_generic == 12, f"LED_PIN_GENERIC = 12 (实际={led_generic})")
check(led_m5stamp == 2, f"LED_PIN_M5STAMP = 2 (实际={led_m5stamp})")


# ============================ 11. NVS key 长度限制 ============================
print(f"\n{TAG} [S11] NVS key 长度 <= 15（ESP-IDF 限制）")

nvs_keys = re.findall(r'^#define\s+(NVS_KEY_\w+)\s+"([^"]+)"', config_src, re.MULTILINE)
for key_name, key_val in nvs_keys:
    check(len(key_val) <= 15, f"NVS key {key_name}='{key_val}' 长度 <= 15 (实际={len(key_val)})")


# ============================ 结果汇总 ============================
print(f"\n{TAG} =============================")
print(f"{TAG} 结果: {g_pass} 通过, {g_fail} 失败")
if g_fail > 0:
    print(f"{TAG} 失败项:")
    for f in failures:
        print(f"{TAG}   - {f}")
    sys.exit(1)
print(f"{TAG} 全绿")
sys.exit(0)
