#!/usr/bin/env python3
"""核心验收的可复用证据、设备独占及运动样本断言。"""
import hashlib
import json
import os
from pathlib import Path

class DeviceLease:
    def __init__(self, directory, target):
        self.path = Path(directory) / (hashlib.sha256(target.lower().encode()).hexdigest()[:20] + '.lock')
        self.file = None

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.file = self.path.open('a+b')
        try:
            if self.path.stat().st_size == 0:
                self.file.write(b'0')
                self.file.flush()
            self.file.seek(0)
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            self.file.close()
            self.file = None
            raise RuntimeError('目标设备已有 harness 执行者；禁止串口/BLE并行写入') from exc
        return self

    def __exit__(self, *unused):
        if self.file is not None:
            if os.name == 'nt':
                import msvcrt
                self.file.seek(0)
                msvcrt.locking(self.file.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file.fileno(), fcntl.LOCK_UN)
            self.file.close()
            self.file = None


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def validate_samples(frames, seconds, minimum, erg=None):
    errors = []
    for label in ('CPS', 'HRS'):
        values = frames.get(label, [])
        if len(values) < minimum:
            errors.append(f'{label}帧数{len(values)}<{minimum}')
        if len(values) >= 2:
            times = [v['time'] for v in values]
            if times[-1] - times[0] < max(0, seconds - 3):
                errors.append(label + '仅有突发帧，未覆盖采样时长')
            if any(b - a > 3 for a, b in zip(times, times[1:])):
                errors.append(label + '通知停更超过3秒')
    cps = frames.get('CPS', [])
    powers = [v['value']['power_w'] for v in cps]
    if not powers or not any(v > 0 for v in powers):
        errors.append('功率没有非零样本')
    if any(not 0 <= v <= 2000 for v in powers):
        errors.append('功率超出0..2000W')
    cadence = []
    for a, b in zip(cps, cps[1:]):
        av, bv = a['value'], b['value']
        if av['crank_revolutions'] is None or bv['crank_revolutions'] is None:
            errors.append('CPS没有曲柄字段')
            break
        rev = (bv['crank_revolutions'] - av['crank_revolutions']) & 0xffff
        dt = (bv['crank_event_time_1024'] - av['crank_event_time_1024']) & 0xffff
        if dt and rev:
            rpm = rev * 60 * 1024 / dt
            cadence.append(rpm)
            if not 1 <= rpm <= 220:
                errors.append('曲柄解码踏频超出1..220RPM')
    if not cadence:
        errors.append('踏频累计没有推进')
    hr = [v['value']['heart_rate_bpm'] for v in frames.get('HRS', [])]
    if not hr or any(not 30 <= v <= 240 for v in hr):
        errors.append('心率无有效30..240bpm样本')
    if erg is not None:
        stable = powers[-5:]
        if len(stable) < 5 or any(not erg[0] <= v <= erg[1] for v in stable):
            errors.append(f'ERG最后5帧未收敛到{erg}: {stable}')
    return {'errors': sorted(set(errors)), 'power_w': [min(powers), max(powers)] if powers else None,
            'cadence_rpm': [round(min(cadence), 2), round(max(cadence), 2)] if cadence else None,
            'heart_rate_bpm': [min(hr), max(hr)] if hr else None,
            'counts': {k: len(v) for k, v in frames.items()}}


def aggregate(rounds, main_errors, restore_errors, required_rounds):
    return 'PASS' if (len(rounds) == required_rounds and not main_errors and not restore_errors
                      and all(r['status'] == 'PASS' for r in rounds)) else 'FAIL'
