#!/usr/bin/env python3
"""真实生产 HTTP handler → 应用回调 → runtime 故障注入，ASan/UBSan。"""
from test_portal_runtime import main

if __name__ == "__main__":
    main("test_portal_faults.c")
