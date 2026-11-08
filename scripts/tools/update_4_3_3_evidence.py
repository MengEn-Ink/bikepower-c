import json
import subprocess
from datetime import datetime, timezone, timedelta
from pathlib import Path

ROOT = Path(r'D:\workspace\bikepower-c')
OUT = ROOT / 'build' / 'release' / 'evidence' / 'device-acceptance-generic-c3.json'
CST = timezone(timedelta(hours=8))


def git_sha():
    return subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', '--short', 'HEAD'], text=True).strip()


now = datetime.now(CST).isoformat(timespec='seconds')
sha = git_sha()
rel = 'tests/reports/release-4.3.3-continuation'

record = {
    'schema_version': 2,
    'firmware_version': '4.3.3',
    'git_sha': sha,
    'board': 'generic-c3',
    'operator': 'MengEn',
    'created_at': now,
    'updated_at': now,
    'items': {
        'serial_boot': {
            'title': '串口启动日志',
            'priority': 'P0', 'kind': 'auto', 'status': 'PASS', 'checked_at': now,
            'evidence': f'{rel}/restore-original-config.log（ROM 复位启动与连接正常）',
        },
        'ble_config_rw': {
            'title': 'BLE 全量配置读写与 NVS 持久化',
            'priority': 'P0', 'kind': 'auto', 'status': 'PASS', 'checked_at': now,
            'evidence': f'{rel}/restore-original-config.json（八组与基线一致、重启独立回读一致、串号哈希一致）',
        },
        'ble_cycling': {
            'title': '骑行 BLE 自测',
            'priority': 'P0', 'kind': 'auto', 'status': 'PASS', 'checked_at': now,
            'evidence': f'{rel}/control-soak-5x60.json（五轮 CPS/HR/CSC/FTMS 每轮 60 帧、CP 响应全部正确）',
        },
        'ble_reconnect': {
            'title': 'BLE 连接与重连',
            'priority': 'P0', 'kind': 'auto', 'status': 'PASS', 'checked_at': now,
            'evidence': f'{rel}/control-soak-5x60.json（五轮独立连接全部成功）',
        },
        'soak_5min': {
            'title': '5 分钟设备耐久',
            'priority': 'P0', 'kind': 'auto', 'status': 'PASS', 'checked_at': now,
            'evidence': f'{rel}/control-soak-5x60.json（5 轮 × 60 秒控制耐久，0 失败）',
        },
        'ble_bonded_rebuild': {
            'title': '已绑定客户端 GATT 重建恢复',
            'priority': 'P0', 'kind': 'auto', 'status': 'PASS', 'checked_at': now,
            'evidence': f'{rel}/restore-original-config.json（提交触发 GATT 重建后绑定连接恢复并完成八组回读）',
        },
        'ble_running': {
            'title': '跑步 BLE 自测',
            'priority': 'P1', 'kind': 'auto', 'status': 'NOT_RUN', 'checked_at': now,
            'evidence': '本轮未重跑跑步 RSC/FTMS',
        },
        'portal_http': {
            'title': '配网门户 HTTP',
            'priority': 'P1', 'kind': 'auto', 'status': 'NOT_RUN', 'checked_at': now,
            'evidence': '本轮未连接开发板 AP 复测门户；门户逻辑由 host 契约测试覆盖',
        },
        'broadcast_mask': {
            'title': '广播项数据面',
            'priority': 'P1', 'kind': 'auto', 'status': 'NOT_RUN', 'checked_at': now,
            'evidence': '未逐字段比对广播掩码',
        },
        'button_short_long': {
            'title': '实体按钮短按与长按',
            'priority': 'P1', 'kind': 'manual', 'status': 'NOT_RUN', 'checked_at': now,
            'evidence': '需人工按键',
        },
        'wifi_confirm': {
            'title': 'WiFi 二次确认',
            'priority': 'P1', 'kind': 'manual', 'status': 'NOT_RUN', 'checked_at': now,
            'evidence': '需人工确认',
        },
        'led_patterns': {
            'title': 'LED 灯效',
            'priority': 'P1', 'kind': 'manual', 'status': 'NOT_RUN', 'checked_at': now,
            'evidence': '需人工观察',
        },
        'portal_mobile': {
            'title': '手机 Captive Portal',
            'priority': 'P1', 'kind': 'manual', 'status': 'NOT_RUN', 'checked_at': now,
            'evidence': '需人工验证',
        },
        'portal_exit': {
            'title': '配网退出与 BLE 连续性',
            'priority': 'P1', 'kind': 'manual', 'status': 'NOT_RUN', 'checked_at': now,
            'evidence': '需人工验证',
        },
        'zwift_cycling': {
            'title': 'Zwift 骑行兼容',
            'priority': 'P1', 'kind': 'manual', 'status': 'NOT_RUN', 'checked_at': now,
            'evidence': '未执行 Zwift 联调',
        },
        'running_apps': {
            'title': '跑步 App 兼容',
            'priority': 'P1', 'kind': 'manual', 'status': 'NOT_RUN', 'checked_at': now,
            'evidence': '未执行跑步 App 联调',
        },
    },
}
OUT.write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
p0 = [i for i in record['items'].values() if i['priority'] == 'P0']
print('git_sha:', sha)
print('P0 PASS:', sum(1 for i in p0 if i['status'] == 'PASS'), '/', len(p0))
print('written:', OUT)
