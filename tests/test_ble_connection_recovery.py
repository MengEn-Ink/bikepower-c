#!/usr/bin/env python3
"""连接恢复覆盖迁入实际双客户端GAP/角色/早到订阅/状态26/复位回归。"""
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(ROOT/'tests/test_ble_multi_client.py')],check=True)
