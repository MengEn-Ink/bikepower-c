#!/usr/bin/env python3
"""桥接主备恢复静态契约：生命周期、输出选择与扫描边界。"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP_MAIN = ROOT / "main" / "app_main.c"
NVS_STORE = ROOT / "main" / "nvs_store.c"
RUNTIME = ROOT / "main" / "bridge_runtime.c"
PORTAL = ROOT / "main" / "wifi_portal.c"
QUALITY_GATE = ROOT / "scripts" / "tools" / "quality_gate.py"


def main():
    sources = {
        path: path.read_text(encoding="utf-8")
        for path in (APP_MAIN, NVS_STORE, RUNTIME, PORTAL, QUALITY_GATE)
    }
    app_main = sources[APP_MAIN]
    nvs_store = sources[NVS_STORE]
    runtime = sources[RUNTIME]
    portal = sources[PORTAL]
    quality_gate = sources[QUALITY_GATE]
    checks = [
        ('#include "bridge_runtime.h"' in app_main,
         "app_main 引入主备运行时"),
        ("bridge_runtime_init(" in app_main,
         "冷启动初始化主备运行时"),
        ("bridge_runtime_tick(" in app_main,
         "主循环驱动运行时"),
        ("bridge_runtime_uses_simulation()" in app_main,
         "实际来源选择不等同于持久来源"),
        ("bridge_runtime_apply_config(" in app_main,
         "配置提交刷新运行时"),
        ("ble_scanner_start(" not in app_main,
         "冷启动不得后台扫描"),
        ("NVS_KEY_BRIDGE_RECENT" in nvs_store,
         "最近成功提示由 NVS 模块管理"),
        ("BRIDGE_RUNTIME_SCAN_START" in runtime
         and "ble_scanner_start(" in runtime,
         "扫描只由运行时手动命令启动"),
        ("wifi_portal_set_bridge_callback(" in app_main
         and "wifi_portal_sync_bridge(" in app_main,
         "Portal 仅消费主任务发布快照并投递命令"),
        ("s_bridge_cb(" in portal
         and "ble_power_bridge_request(" not in portal
         and "ble_power_bridge_request_stop(" not in portal
         and "ble_scanner_start(" not in portal
         and "ble_scanner_stop(" not in portal,
         "HTTP 任务不直接调用 NimBLE 会话或扫描启停"),
        ("tests/check_bridge_failover_contract.py" in quality_gate,
         "主备恢复静态契约已接入总质量门禁"),
    ]
    failed = 0
    for passed, message in checks:
        print(f"[bridge-contract] {'PASS' if passed else 'FAIL'} {message}")
        failed += not passed
    print(f"[bridge-contract] 结果: {len(checks) - failed} 通过, {failed} 失败")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
