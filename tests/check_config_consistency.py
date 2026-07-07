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
RADIO_CTRL = ROOT / "main" / "radio_ctrl.c"
DOMAIN_H = ROOT / "components" / "bikepower_domain" / "include" / "domain_config.h"
APP_JS = ROOT / "main" / "web" / "app.js"
MOCK_SERVER = ROOT / "tests" / "mock_server.py"
AGENTS = ROOT / "AGENTS.md"
README = ROOT / "README.md"
PROGRESS = ROOT / "PROGRESS.md"
CHANGELOG = ROOT / "CHANGELOG.md"
DOC_INDEX = ROOT / "docs" / "INDEX.md"
TECH_REF = ROOT / "docs" / "dev" / "01-technical-reference.md"
QUICKSTART = ROOT / "docs" / "user" / "01-quickstart.md"
USER_MANUAL = ROOT / "docs" / "user" / "02-user-manual.md"
NIMBLE_REWRITE = ROOT / "docs" / "archive" / "01-cidf-nimble-rewrite.md"
MPY_COMPARISON = ROOT / "docs" / "archive" / "02-micropython-vs-c-comparison.md"
REVIEW_CHECKLIST = ROOT / "docs" / "archive" / "03-review-checklist.md"
HW_CONSTRAINTS = ROOT / ".trae" / "rules" / "hardware-constraints.md"
BUILD_ALL = ROOT / "scripts" / "build_all.sh"

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


def extract_js_range(src: str, key: str):
    m = re.search(rf'{re.escape(key)}:\s*\{{\s*min:\s*(-?\d+),\s*max:\s*(-?\d+)', src)
    return (int(m.group(1)), int(m.group(2))) if m else None


def extract_mock_limit(src: str, key: str):
    m = re.search(rf'"{re.escape(key)}"\s*:\s*\((-?\d+),\s*(-?\d+)\)', src)
    return (int(m.group(1)), int(m.group(2))) if m else None


# ============================ 读取文件 ============================
config_src = read(CONFIG_H)
radio_ctrl_src = read(RADIO_CTRL)
domain_src = read(DOMAIN_H)
app_js_src = read(APP_JS)
mock_src = read(MOCK_SERVER)
agents_src = read(AGENTS)
readme_src = read(README)
progress_src = read(PROGRESS)
changelog_src = read(CHANGELOG)
doc_index_src = read(DOC_INDEX)
tech_ref_src = read(TECH_REF)
quickstart_src = read(QUICKSTART)
user_manual_src = read(USER_MANUAL)
nimble_rewrite_src = read(NIMBLE_REWRITE)
mpy_comparison_src = read(MPY_COMPARISON)
review_checklist_src = read(REVIEW_CHECKLIST)
hw_src = read(HW_CONSTRAINTS) if HW_CONSTRAINTS.exists() else ""
build_all_src = read(BUILD_ALL)


# ============================ 1. 固件版本一致性 ============================
print(f"\n{TAG} [S1] 固件版本号一致性")

fw_ver = extract_define(config_src, "FIRMWARE_VERSION")
check(fw_ver is not None, "FIRMWARE_VERSION 已定义")
print(f"{TAG}   FIRMWARE_VERSION = {fw_ver}")

stable_ver = extract_define(config_src, "FIRMWARE_STABLE_VERSION")
fix_ver = extract_define(config_src, "FIRMWARE_FIX_VERSION")
check(stable_ver is not None and re.match(r'\d+\.\d+\.\d+', stable_ver),
      f"FIRMWARE_STABLE_VERSION 已定义为语义版本 (实际={stable_ver})")
check(fix_ver is not None and re.match(r'\d+\.\d+\.\d+', fix_ver),
      f"FIRMWARE_FIX_VERSION 已定义为语义版本 (实际={fix_ver})")
def parse_semver(s):
    m = re.match(r'(\d+)\.(\d+)\.(\d+)', s or "")
    return tuple(int(x) for x in m.groups()) if m else (0, 0, 0)

check(fw_ver == fix_ver, f"FIRMWARE_VERSION({fw_ver}) 与修复版({fix_ver}) 对齐")
check(parse_semver(stable_ver) <= parse_semver(fw_ver),
      f"稳定版({stable_ver}) 不高于当前固件版本({fw_ver})")

latest_released = extract_changelog_latest_version(changelog_src)
print(f"{TAG}   CHANGELOG 最新已发布版本 = {latest_released}")

# 当前版本必须 > CHANGELOG 最新已发布版本（发版前未发布段含新版本）
if latest_released:
    cur = parse_semver(fw_ver)
    rel = parse_semver(latest_released)
    check(cur >= rel, f"固件版本 {fw_ver} >= CHANGELOG 最新发布 {latest_released}")

check(f"当前固件版本 v{fw_ver}" in agents_src or f"当前固件版本：v{fw_ver}" in agents_src,
      f"AGENTS.md 当前固件版本与 FIRMWARE_VERSION({fw_ver}) 一致")
check(f"当前固件版本：v{fw_ver}" in progress_src,
      f"PROGRESS.md 当前固件版本与 FIRMWARE_VERSION({fw_ver}) 一致")
check(f"当前固件版本为 `v{fw_ver}`" in tech_ref_src,
      f"技术参考当前固件版本与 FIRMWARE_VERSION({fw_ver}) 一致")

# DEVICE_NAME 一致性
device_name_cfg = extract_define(config_src, "DEVICE_NAME")
device_name_domain = extract_define(domain_src, "DEVICE_NAME_DEFAULT")
check(device_name_cfg == device_name_domain,
      f"DEVICE_NAME({device_name_cfg}) == DEVICE_NAME_DEFAULT({device_name_domain})")
check("BIKEPOWER_GIT_SHA" in config_src or "BIKEPOWER_GIT_SHA" in read(CONFIG_H.parent / "CMakeLists.txt"),
      "构建系统必须注入 BIKEPOWER_GIT_SHA")
check("FIRMWARE_STABLE_VERSION" in build_all_src and "FIRMWARE_FIX_VERSION" in build_all_src,
      "Release 构建脚本必须读取稳定版和修复版")
check("git -C \"$PROJECT_DIR\" rev-parse --short HEAD" in build_all_src,
      "Release 构建脚本必须读取最后 Git 短提交 ID")
check("bikepower-c-%s-v%s-stable-fix-g%s.bin" in build_all_src,
      "Release 合并固件文件名必须包含板型、单一版本号、stable/fix 标识和 Git ID")
check("bikepower-c-release-v${VERSION}-stable-fix-g${GIT_SHA}.zip" in build_all_src,
      "Release 构建脚本必须生成包含双板型固件、测试报告和用户手册的总包")
check("zip -qr \"$PACKAGE_NAME\"" in build_all_src,
      "Release 构建脚本必须使用 zip 生成可直接分发的总包")
check("stable%s-fix%s" not in build_all_src,
      "Release 合并固件文件名不得重复拼接稳定版/修复版版本号")

# WIFI_SSID_PREFIX 与 DEVICE_NAME 关系（AP 名沿用设备名前缀，运行时追加 MAC 后缀）
wifi_ssid_prefix = extract_define(config_src, "WIFI_SSID_PREFIX")
check(wifi_ssid_prefix == "BikePower",
      f"WIFI_SSID_PREFIX = BikePower (实际={wifi_ssid_prefix})")
check(extract_define(config_src, "WIFI_SSID") is None,
      "不再定义固定 WIFI_SSID，避免手机 Captive Portal 缓存同名热点")
check("esp_wifi_get_mac(WIFI_IF_AP" in radio_ctrl_src,
      "radio_ctrl.c 使用 SoftAP MAC 生成热点后缀")
check('"%s-%02X%02X%02X"' in radio_ctrl_src,
      "radio_ctrl.c 按 BikePower-XXXXXX 规则格式化热点名")
check("WIFI_SSID_PREFIX" in radio_ctrl_src and "ap_ssid" in radio_ctrl_src,
      "radio_ctrl.c 使用 WIFI_SSID_PREFIX 生成实际 SSID")
check("ap_cfg.ap.ssid_len = strlen(ap_ssid)" in radio_ctrl_src,
      "radio_ctrl.c 的 ssid_len 使用实际 SSID 长度")
check("ssid=%s" in radio_ctrl_src and "ap_ssid" in radio_ctrl_src,
      "SoftAP 启动日志打印实际热点名")


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


# ============================ 3b. Mock/前端范围 ↔ 领域常量 ============================
print(f"\n{TAG} [S3b] Mock/前端范围 ↔ 领域常量")

range_expectations = {
    "power": (extract_int(domain_src, "POWER_MIN"), extract_int(domain_src, "POWER_MAX")),
    "cadence": (extract_int(domain_src, "CADENCE_MIN"), extract_int(domain_src, "CADENCE_MAX")),
    "heartrate": (extract_int(domain_src, "HR_MIN"), extract_int(domain_src, "HR_MAX")),
    "power_float": (extract_int(domain_src, "POWER_FLOAT_MIN"), extract_int(domain_src, "POWER_FLOAT_MAX")),
    "cadence_float": (extract_int(domain_src, "CADENCE_FLOAT_MIN"), extract_int(domain_src, "CADENCE_FLOAT_MAX")),
    "hr_float": (extract_int(domain_src, "HR_FLOAT_MIN"), extract_int(domain_src, "HR_FLOAT_MAX")),
    "power_step": (extract_int(domain_src, "POWER_STEP_MIN"), extract_int(domain_src, "POWER_STEP_MAX")),
    "profile_weight": (extract_int(domain_src, "PROFILE_WEIGHT_MIN"), extract_int(domain_src, "PROFILE_WEIGHT_MAX")),
    "profile_ftp": (extract_int(domain_src, "PROFILE_FTP_MIN"), extract_int(domain_src, "PROFILE_FTP_MAX")),
    "profile_thr_hr": (extract_int(domain_src, "PROFILE_THR_HR_MIN"), extract_int(domain_src, "PROFILE_THR_HR_MAX")),
    "custom_wkg_low": (extract_int(domain_src, "CUSTOM_WKG_MIN"), extract_int(domain_src, "CUSTOM_WKG_MAX")),
    "custom_wkg_high": (extract_int(domain_src, "CUSTOM_WKG_MIN"), extract_int(domain_src, "CUSTOM_WKG_MAX")),
    "run_speed_x100": (extract_int(domain_src, "RUN_SPEED_X100_MIN"), extract_int(domain_src, "RUN_SPEED_X100_MAX")),
    "run_pace_float_sec": (extract_int(domain_src, "RUN_PACE_FLOAT_SEC_MIN"), extract_int(domain_src, "RUN_PACE_FLOAT_SEC_MAX")),
    "run_cadence": (extract_int(domain_src, "RUN_CADENCE_MIN"), extract_int(domain_src, "RUN_CADENCE_MAX")),
    "run_cadence_float": (extract_int(domain_src, "RUN_CADENCE_FLOAT_MIN"), extract_int(domain_src, "RUN_CADENCE_FLOAT_MAX")),
}

for key, expected in range_expectations.items():
    actual = extract_mock_limit(mock_src, key)
    check(actual == expected, f"tests/mock_server.py LIMITS[{key}] == {expected} (实际={actual})")

js_ranges = {
    "power": "power_float",
    "cadence": "cadence_float",
    "hr": "hr_float",
    "runpace": "run_pace_float_sec",
    "runcadence": "run_cadence_float",
    "pstep": "power_step",
}
for js_key, domain_key in js_ranges.items():
    actual = extract_js_range(app_js_src, js_key)
    expected = range_expectations[domain_key]
    check(actual == expected, f"main/web/app.js FLOAT_RANGE.{js_key} == {expected} (实际={actual})")

config_version = extract_int(domain_src, "CONFIG_VERSION")
check(f"`CONFIG_VERSION = {config_version}`" in tech_ref_src,
      f"技术参考 schema 版本与 CONFIG_VERSION({config_version}) 一致")


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
check("阶段 0-13 完成情况" in doc_index_src,
      "docs/INDEX.md 含阶段 0-13 项目进度描述")
if "| 13 |" in progress_src:
    check("阶段 0-13" in doc_index_src,
          "PROGRESS.md 含阶段 13 时 docs/INDEX.md 同步到阶段 0-13")


# ============================ 5. BLE 通知间隔一致性 ============================
print(f"\n{TAG} [S5] BLE 通知间隔一致性")

notify_ms = extract_int(config_src, "BLE_NOTIFY_INTERVAL_MS")
check(notify_ms == 1000, f"BLE_NOTIFY_INTERVAL_MS = 1000 (实际={notify_ms})")

advert_ms = extract_int(config_src, "BLE_ADVERTISING_INTERVAL_MS")
check(advert_ms == 500, f"BLE_ADVERTISING_INTERVAL_MS = 500 (实际={advert_ms})")


# ============================ 7. 内存阈值一致性 ============================
print(f"\n{TAG} [S6] 内存阈值一致性")

heap_warn = extract_int(config_src, "HEAP_WARN_BYTES")
heap_crit = extract_int(config_src, "HEAP_CRIT_BYTES")
check(heap_warn == 40960, f"HEAP_WARN_BYTES = 40960 (实际={heap_warn})")
check(heap_crit == 20480, f"HEAP_CRIT_BYTES = 20480 (实际={heap_crit})")

# hardware-constraints.md 应提到堆告警阈值（40KB/20KB 与 HEAP_WARN/CRIT 对齐）
if hw_src:
    check("40KB" in hw_src or "40 KB" in hw_src or "20480" in hw_src or "20KB" in hw_src,
          "hardware-constraints.md 提到堆告警阈值 40KB/20KB")


# ============================ 7. 任务栈大小合理性 ============================
print(f"\n{TAG} [S7] 任务栈大小合理性")

for stk_name in ["TASK_STACK_BTN", "TASK_STACK_LED", "TASK_STACK_HTTPD"]:
    v = extract_int(config_src, stk_name)
    check(v is not None and v >= 1024, f"{stk_name} >= 1024 (实际={v})")


# ============================ 8. FTMS OpCode 唯一性 ============================
print(f"\n{TAG} [S8] FTMS OpCode 值不冲突")

ftms_ops = {
    "FTMS_OP_REQUEST_CONTROL": extract_int(config_src, "FTMS_OP_REQUEST_CONTROL"),
    "FTMS_OP_RESET": extract_int(config_src, "FTMS_OP_RESET"),
    "FTMS_OP_SET_TARGET_POWER": extract_int(config_src, "FTMS_OP_SET_TARGET_POWER"),
    "FTMS_OP_START_RESUME": extract_int(config_src, "FTMS_OP_START_RESUME"),
    "FTMS_OP_STOP_PAUSE": extract_int(config_src, "FTMS_OP_STOP_PAUSE"),
}
values = [v for v in ftms_ops.values() if v is not None]
check(len(values) == len(set(values)), f"FTMS OpCode 值唯一 (实际={values})")


# ============================ 9. 板型枚举完整性 ============================
print(f"\n{TAG} [S9] 板型枚举完整性")

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


# ============================ 10. NVS key 长度限制 ============================
print(f"\n{TAG} [S10] NVS key 长度 <= 15（ESP-IDF 限制）")

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
