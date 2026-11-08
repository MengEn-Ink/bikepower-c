#!/usr/bin/env python3
"""串行运行三链路相关离线回归，失败保留完整输出，不访问硬件。"""
import os, subprocess, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
commands=[['make','-C',str(ROOT/'test'),'run']]+[[sys.executable,str(ROOT/'tests'/name)] for name in (
 'test_ble_multi_client.py','test_ble_standard_contract.py','test_control_delivery.py','test_ble_config_service.py',
 'test_device_telemetry.py','test_ble_runtime.py','test_bridge_session.py','test_ble_cycling_services.py',
 'test_ble_session_security.py','test_ble_bond_cache.py','test_audit_regressions.py','check_config_consistency.py')]
folder=ROOT/'tests/reports/multi-client-offline';folder.mkdir(parents=True,exist_ok=True)
failed=[]
for n,command in enumerate(commands):
    print('RUN',command,flush=True)
    env=os.environ.copy();env['IDF_PATH']=str(ROOT/'build/toolchain/esp-idf-v5.5.4')
    result=subprocess.run(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=180)
    (folder/f'{n:02d}.log').write_text(result.stdout,encoding='utf-8')
    print(result.stdout[-3000:],flush=True)
    if result.returncode:failed.append(command[-1])
print('FAILED',failed,flush=True)
raise SystemExit(bool(failed))
