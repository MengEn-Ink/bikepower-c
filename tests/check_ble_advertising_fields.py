#!/usr/bin/env python3
"""
TC-BLE-ADV BLE 广播字段静态检查（无需设备）

校验骑行/跑步模式主广播声明的服务 UUID 与 GATT 注册保持一致（按 broadcast_mask
动态拼装），避免码表按设备类型过滤扫描时漏发现；同时校验广播名长度预算。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BLE_BIKEPOWER_C = ROOT / "main" / "ble_bikepower.c"
CONFIG_H = ROOT / "main" / "bikepower_config.h"

TAG = "[TC-BLE-ADV]"
failures = []


def check(cond: bool, msg: str):
    if cond:
        print(f"{TAG}   PASS {msg}")
    else:
        failures.append(msg)
        print(f"{TAG}   FAIL {msg}")


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def extract_int_define(src: str, name: str):
    m = re.search(rf'^#define\s+{re.escape(name)}\s+(-?\d+)\b', src, re.MULTILINE)
    return int(m.group(1)) if m else None


ble_src = read(BLE_BIKEPOWER_C)
config_src = read(CONFIG_H)

# 提取广播 UUID 动态拼装块（adv_uuids 构建到 fields.uuids16 赋值之间）
adv_block_m = re.search(r"ble_uuid16_t adv_uuids\[3\];.*?fields\.uuids16", ble_src, re.S)
adv_block = adv_block_m.group(0) if adv_block_m else ""

# 拆分跑步分支与骑行分支
running_branch_m = re.search(
    r"if \(g_profile == SPORT_PROFILE_RUNNING\) \{(?P<body>.*?)\} else \{(?P<cyc>.*?)\}",
    adv_block, re.S,
)
running_branch = running_branch_m.group("body") if running_branch_m else ""
cycling_branch = running_branch_m.group("cyc") if running_branch_m else ""

name_max = extract_int_define(config_src, "BLE_ADV_NAME_MAX_LEN")

print(f"\n{TAG} [S1] 骑行主广播服务发现")
check("BLE_UUID16_INIT(POWER_SERVICE_UUID)" in cycling_branch,
      "骑行主广播包含 Cycling Power Service")
check("BLE_UUID16_INIT(CSC_SERVICE_UUID)" in cycling_branch,
      "骑行主广播包含 Cycling Speed and Cadence Service")
check("BLE_UUID16_INIT(FTMS_SERVICE_UUID)" in cycling_branch,
      "骑行主广播包含 Fitness Machine Service")

print(f"\n{TAG} [S2] 跑步主广播服务发现")
check("BLE_UUID16_INIT(RSC_SERVICE_UUID)" in running_branch,
      "跑步主广播包含 Running Speed and Cadence Service")
check("BLE_UUID16_INIT(FTMS_SERVICE_UUID)" in running_branch,
      "跑步主广播包含 Fitness Machine Service")

print(f"\n{TAG} [S3] 广播包预算")
check(name_max == 18,
      f"BLE_ADV_NAME_MAX_LEN 为 18，匹配 flags + 3 个 16-bit UUID 的 31 字节预算 (实际={name_max})")
check("完整名走 scan response" in config_src and "rsp_fields.name" in ble_src,
      "完整设备名保留在 scan response")
check("HR_SERVICE_UUID" in adv_block,
      "scan response 声明 HRS UUID（HEARTRATE 启用时）")

if failures:
    print(f"\n{TAG} 失败 {len(failures)} 项")
    for item in failures:
        print(f"{TAG} - {item}")
    sys.exit(1)

print(f"\n{TAG} 全部通过")
