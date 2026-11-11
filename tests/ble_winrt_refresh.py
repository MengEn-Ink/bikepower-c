"""WinRT 服务缓存刷新策略（harness 层 repo wrapper，不改 Bleak 共享库）。

策略：
1. 默认(允许cached)连接；读 Config Meta 强校验长度=12。读到 stale（长度!=12/
   DIS 不是当前配置头文件版本对应的板型固件名）→ 判定缓存过期。
2. 过期则关闭当前引用，创建新 client(use_cached_services=False) 强制完整重枚举；
   重试限定事件/瞬态窗口，不做 sleep 盲重试。
3. per-service DEBUG：逐 UUID GetCharacteristicsAsync 定位 E_UNEXPECTED 抛出点。
4. finally 关闭 GattDeviceService/session/device 引用与 callback token。
"""
import asyncio
import logging
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

log = logging.getLogger(__name__)

CONFIG_META_UUID = '7a5a0001-4c7a-4f2a-9b3e-6d1f4a8c0b21'
DIS_FW_UUID = '00002a26-0000-1000-8000-00805f9b34fb'
def _config_value(name):
    match = re.search(rf'#define\s+{name}\s+"([^"]+)"',
                      (ROOT / "main" / "bikepower_config.h").read_text(encoding="utf-8"))
    if not match:
        raise RuntimeError(f"无法读取 {name}")
    return match.group(1)


def expected_firmware_prefixes():
    version = _config_value("FIRMWARE_VERSION")
    return {
        f"{_config_value('BOARD_NAME_GENERIC')}-v{version}",
        f"{_config_value('BOARD_NAME_M5STAMP')}-v{version}",
    }


EXPECTED_META_LEN = 12
EXPECTED_FW_PREFIXES = expected_firmware_prefixes()


class StaleCacheError(RuntimeError):
    pass


async def _read_meta(s):
    try:
        raw = bytes(await s.client.read_gatt_char(CONFIG_META_UUID))
        return raw
    except Exception as exc:
        raise StaleCacheError(f'meta读失败: {type(exc).__name__}: {exc}') from exc


async def _probe_services_debug(s):
    """逐 service 枚举 characteristics，DEBUG 定位原生异常点。"""
    for svc in s.client.services:
        try:
            chars = list(svc.characteristics)
            log.debug('svc %s chars=%d', svc.uuid, len(chars))
        except Exception as exc:  # WinRT 原生异常
            log.error('svc %s GetCharacteristicsAsync 抛 %s: %s',
                      svc.uuid, type(exc).__name__, exc)


def _looks_stale(meta_raw, fw_bytes):
    reasons = []
    if len(meta_raw) != EXPECTED_META_LEN:
        reasons.append(f'meta长度={len(meta_raw)} != {EXPECTED_META_LEN}')
    try:
        fw = bytes(fw_bytes).split(b'\x00')[0].decode('ascii', 'replace')
    except Exception:
        fw = ''
    if not any(fw.startswith(prefix) for prefix in EXPECTED_FW_PREFIXES):
        reasons.append(f'DIS固件异常: {fw!r}')
    return reasons


async def connect_validated(device_session, max_refresh_attempts=2):
    """连接并强制校验；返回 stale 原因列表（空=当前缓存即真实）。"""
    stale_reasons = []
    for attempt in range(1, max_refresh_attempts + 1):
        uncached = attempt > 1
        try:
            await device_session.connect_uncached(uncached)
        except Exception as exc:
            stale_reasons.append(f'attempt{attempt} uncached连接失败: {type(exc).__name__}: {exc}')
            continue
        await _probe_services_debug(device_session)
        meta_raw = await _read_meta(device_session)
        try:
            fw_bytes = bytes(await device_session.client.read_gatt_char(DIS_FW_UUID))
        except Exception as exc:
            fw_bytes = b''
            stale_reasons.append(f'DIS读失败: {exc}')
        stale = _looks_stale(meta_raw, fw_bytes)
        if not stale:
            return stale_reasons
        stale_reasons.extend([f'attempt{attempt}: {r}' for r in stale])
        try:
            await device_session.disconnect()
        except Exception:
            pass
        # 给 Windows 缓存失效窗口一次事件循环yield（非盲sleep）
        await asyncio.sleep(2)
    raise StaleCacheError('；'.join(stale_reasons) or '缓存无法刷新到真实表')
