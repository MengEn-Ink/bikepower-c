"""WinRT 服务缓存刷新策略（harness 层 repo wrapper，不改 Bleak 共享库）。

策略：
1. 默认允许缓存连接；校验当前 Config Meta 与源码对应的双板 DIS 身份。
2. 过期则关闭当前引用，创建新 client(use_cached_services=False) 强制完整重枚举；
   重试限定事件/瞬态窗口，不做 sleep 盲重试。
3. per-service DEBUG：逐 UUID GetCharacteristicsAsync 定位 E_UNEXPECTED 抛出点。
4. finally 关闭 GattDeviceService/session/device 引用与 callback token。
"""
import asyncio
import logging

from ble_config_e2e import expected_firmware_name, parse_meta

log = logging.getLogger(__name__)

CONFIG_META_UUID = '7a5a0001-4c7a-4f2a-9b3e-6d1f4a8c0b21'
DIS_FW_UUID = '00002a26-0000-1000-8000-00805f9b34fb'
EXPECTED_META_LEN = 12


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


def _looks_stale(meta_raw, fw_bytes, expected_firmware=""):
    reasons = []
    try:
        meta = parse_meta(meta_raw)
        if meta["schema"] != 14 or meta["max_len"] != 48:
            reasons.append(f'配置 schema/最大长度异常: {meta}')
    except RuntimeError as exc:
        reasons.append(str(exc))
    try:
        fw = bytes(fw_bytes).split(b'\x00')[0].decode('ascii', 'replace')
    except Exception:
        fw = ''
    accepted = (expected_firmware,) if expected_firmware else tuple(
        expected_firmware_name(board) for board in ("generic-c3", "m5stamp-c3")
    )
    if fw not in accepted:
        reasons.append(f'DIS固件异常: {fw!r}')
    return reasons


async def connect_validated(device_session, max_refresh_attempts=2, expected_firmware=""):
    """连接并强制校验；返回 stale 原因列表（空=当前缓存即真实）。"""
    stale_reasons = []
    for attempt in range(1, max_refresh_attempts + 1):
        uncached = attempt > 1
        try:
            await device_session.connect_uncached(uncached)
            await _probe_services_debug(device_session)
            meta_raw = await _read_meta(device_session)
            fw_bytes = bytes(await device_session.client.read_gatt_char(DIS_FW_UUID))
            stale = _looks_stale(meta_raw, fw_bytes, expected_firmware)
            if not stale:
                return stale_reasons
            stale_reasons.extend([f'attempt{attempt}: {r}' for r in stale])
        except Exception as exc:
            stale_reasons.append(f'attempt{attempt}: {type(exc).__name__}: {exc}')
        try:
            await device_session.disconnect()
        except Exception:
            pass
        await asyncio.sleep(0)
    raise StaleCacheError('；'.join(stale_reasons) or '缓存无法刷新到真实表')
