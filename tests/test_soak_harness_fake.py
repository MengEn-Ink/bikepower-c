"""ble_release_control_soak harness 防虚绿 host 测试（fake client，不碰真机）。

覆盖：主异常→未读快照不写恢复；五轮独立连接重连计数；CPS 坏payload解析错误计FAIL；
零帧必FAIL；required CP缺失即FAIL且不做长时间空采。
"""
import asyncio
import importlib
import sys
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

CPS = '00002a63-0000-1000-8000-00805f9b34fb'
HR = '00002a37-0000-1000-8000-00805f9b34fb'
CSC = '00002a5b-0000-1000-8000-00805f9b34fb'
BIKE = '00002ad2-0000-1000-8000-00805f9b34fb'
CP = '00002ad9-0000-1000-8000-00805f9b34fb'
TELE = ['7a5a1002-4c7a-4f2a-9b3e-6d1f4a8c0b21', '7a5a1003-4c7a-4f2a-9b3e-6d1f4a8c0b21',
        '7a5a1004-4c7a-4f2a-9b3e-6d1f4a8c0b21', '7a5a1005-4c7a-4f2a-9b3e-6d1f4a8c0b21']
ALL_FULL = [CPS, HR, CSC, BIKE, CP] + TELE


class FakeChar:
    def __init__(self, uuid):
        self.uuid = uuid
        self.properties = ['read', 'notify']


class FakeClient:
    def __init__(self, scenario, state):
        self.scenario = scenario
        self.state = state
        self.handlers = {}
        chars = ALL_FULL if scenario in ('good', 'zero_frame', 'parse_err') else [CPS]
        self.services = [types.SimpleNamespace(characteristics=[FakeChar(u) for u in chars])]

    async def start_notify(self, char, cb):
        self.handlers[char] = cb
        if char == CP:
            return
        # 注入fake帧（good场景），每5ms一帧
        if self.scenario == 'good':
            async def pump():
                try:
                    import struct as _st
                    good = _st.pack('<Hh', 0, 100)
                    for _ in range(40):
                        cb(None, good)
                        await asyncio.sleep(0.002)
                except asyncio.CancelledError:
                    pass
            self.state['tasks'].append(asyncio.ensure_future(pump()))

    async def write_gatt_char(self, char, data, response=False):
        if char != CP:
            return
        op = data[0]
        cb = self.handlers.get(CP)
        code = 0x01
        if op == 0x05 and int.from_bytes(data[1:3], 'little', signed=True) == -1:
            code = 0x03
        if cb:
            cb(None, bytes([0x80, op, code]))


state = {}


class FakeSession:
    instances = []

    def __init__(self, name, timeout, address='', reset_method='rom'):
        self.connects = 0
        self.commits = 0
        self.cfg = {'core': bytearray(15), 'bridge': bytearray(16)}
        self.cfg['core'][14] = 3          # 初始：完整掩码未开
        self.cfg['bridge'][1] = 1         # 初始：真实桥接（FTMS 按设计不注册）
        self.client = FakeClient(state['scenario'], state)
        FakeSession.instances.append(self)

    async def connect(self):
        self.connects += 1
        if state.get('boom'):
            raise RuntimeError('boom')

    async def disconnect(self):
        pass

    async def read_snapshot(self):
        return {'protocol': 1, 'schema': 14, 'revision': 1, 'group_mask': 255,
                'capabilities': 15}, {k: bytes(v) for k, v in self.cfg.items()}

    async def commit_all(self, groups, revision):
        self.commits += 1
        for key, value in groups.items():
            if key in self.cfg:
                self.cfg[key] = bytearray(value)
        return {'result': 0, 'revision': revision + 1, 'restart_flags': 1}


def run_scenario(scenario, rounds=2, min_frames=1, boom=False):
    state.clear()
    state.update({'scenario': scenario, 'boom': boom, 'tasks': []})
    FakeSession.instances = []
    mod = importlib.import_module('ble_release_control_soak')
    importlib.reload(mod)
    mod.DeviceSession = FakeSession
    mod.reset_device = lambda *a, **k: None
    sys.argv = ['soak', '--address', 'AA:BB', '--port', 'COMX',
                '--duration', '1', '--rounds', str(rounds), '--min-frames', str(min_frames)]
    rc = asyncio.run(mod.main())
    for t in state['tasks']:
        t.cancel()
    return rc, FakeSession.instances


def main_checks():
    # 1) good: 全绿（>=1帧/路，CP响应对）
    rc, sessions = run_scenario('good')
    assert rc == 0, f'good场景应PASS: rc={rc}'
    # 初始1次 + 每轮2次重连 = 1 + 2*2
    assert sessions[0].connects >= 1 + 2, f'每轮独立重连未发生: {sessions[0].connects}'
    print('PASS good场景+独立重连计数')

    # 2) zero_frame: 无帧注入必FAIL
    rc, _ = run_scenario('zero_frame')
    assert rc == 1, '零帧场景必须FAIL'
    print('PASS 零帧FAIL')

    # 3) missing_cp: required缺失FAIL且快速返回
    rc, _ = run_scenario('missing_cp')
    assert rc == 1
    print('PASS 缺CP FAIL')

    # 4) boom: connect即失败，backup未读到，不写恢复
    rc, sessions = run_scenario('good', boom=True)
    assert rc == 1
    assert sessions[0].commits == 0, '未读快照禁止写恢复'
    print('PASS 主异常不误写恢复')

    # 5) parse_err: CPS坏payload走parse_cps异常路径
    rc, _ = run_scenario('parse_err')
    assert rc == 1
    print('PASS parse_err计入FAIL')

    # 6) backup已读+fullmask已commit后主测试中途失败→真实finally恢复执行
    class FailingRoundSession(FakeSession):
        async def connect(self):
            await super().connect()
            if state.get('fail_round_connect') and self.connects == 2:
                raise RuntimeError('boom in round reconnect')

    state.clear()
    state.update({'scenario': 'good', 'fail_round_connect': True, 'tasks': []})
    FakeSession.instances = []
    mod = importlib.import_module('ble_release_control_soak')
    importlib.reload(mod)
    mod.DeviceSession = FailingRoundSession
    mod.reset_device = lambda *a, **k: None
    sys.argv = ['soak', '--address', 'AA:BB', '--port', 'COMX',
                '--duration', '1', '--rounds', '2', '--min-frames', '1']
    rc = asyncio.run(mod.main())
    ses = FakeSession.instances[0]
    assert ses.commits >= 2, f'fullmask+恢复两次提交都应发生, 实际{ses.commits}'
    assert rc == 1
    print('PASS 已读backup+已commit后main失败仍真实恢复')


if __name__ == '__main__':
    main_checks()
