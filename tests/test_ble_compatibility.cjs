/* 对生产快照与诊断规则做纯函数回归，真值手工维护。 */
const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const modulePath = path.join(__dirname, '../tools/web/ble-compatibility.js');
const api = fs.existsSync(modulePath) ? require(modulePath) : {};
const uuid = short => `0000${short}-0000-1000-8000-00805f9b34fb`;
const copy = value => JSON.parse(JSON.stringify(value));
const context = { target: '骑鬥', version: '测试版本', platform: 'Android',
  role: 'power', stage: 'connect', referenceWorks: true };
function snapshot(id = 'board') {
  return { id, name: id, capturedAt: '2026-11-05T12:00:00.000Z', info: { firmware: '候选' },
    coverage: { scope: 'authorized', enumeration: 'complete', allowedServices: [uuid('1818')] },
    services: [{ uuid: uuid('1818'), enumeration: 'complete', characteristics: [
      { uuid: uuid('2a63'), properties: 'notify', readStatus: 'not-readable', raw: null },
      { uuid: uuid('2a65'), properties: 'read', readStatus: 'ok', raw: '08 00 00 00' },
      { uuid: uuid('2a5d'), properties: 'read', readStatus: 'ok', raw: '05' },
    ] }], samples: [
      { uuid: uuid('2a63'), at: 1000, raw: '20 00 fa 00 01 00 00 04' },
      { uuid: uuid('2a63'), at: 2000, raw: '20 00 fb 00 02 00 00 08' },
    ] };
}

test('诊断生产模块可用', () => assert.equal(typeof api.analyze, 'function'));
test('标准完整不产生缺失问题，通知解析单位和节拍来自原始数据', () => {
  const result = api.analyze(snapshot(), snapshot('reference'), context);
  assert.equal(result.findings.filter(f => f.kind === 'standard').length, 0);
  assert.equal(result.signals.board[0].medianIntervalMs, 1000);
  assert.equal(result.signals.board[0].last.powerW, 251);
  assert.match(result.signals.board[0].summary, /W/);
});
test('确认完整枚举后才能认定特征缺失', () => {
  const board = snapshot();
  board.services[0].characteristics.splice(1, 1);
  assert.ok(api.analyze(board, snapshot('reference'), context).findings
    .some(f => f.kind === 'standard' && f.code === 'missing-2a65'));
  board.services[0].enumeration = 'partial';
  const partial = api.analyze(board, snapshot('reference'), context);
  assert.ok(partial.findings.some(f => f.kind === 'unknown' && f.code === 'missing-2a65'));
  assert.ok(!partial.findings.some(f => f.kind === 'standard' && f.code === 'missing-2a65'));
});
test('没有服务授权不能判定整个服务缺失', () => {
  const board = snapshot();
  board.services = [];
  board.coverage.allowedServices = [];
  assert.ok(api.analyze(board, snapshot('reference'), context).findings
    .some(f => f.kind === 'unknown' && f.code === 'service-1818'));
});
test('历史授权范围不明确时服务差异仍属未知', () => {
  const board = snapshot();
  board.services = [];
  board.coverage.scope = 'unknown';
  assert.ok(api.analyze(board, snapshot('reference'), context).findings
    .some(f => f.kind === 'unknown' && f.code === 'service-1818'));
});
test('读取失败保留为未采集，不误报为空能力', () => {
  const board = snapshot();
  Object.assign(board.services[0].characteristics[1], { raw: null, readStatus: 'failed', readError: '权限不足' });
  const result = api.analyze(board, snapshot('reference'), context);
  assert.ok(result.findings.some(f => f.code === 'value-2a65' && f.kind === 'unknown'));
  assert.ok(!result.findings.some(f => f.code === 'feature-length-2a65'));
});
test('CPS 可选字段按 flags 解析，截断与多余字节不能假绿', () => {
  assert.equal(api.decodePacket(uuid('2a63'), '00 00 fa 00').powerW, 250);
  assert.throws(() => api.decodePacket(uuid('2a63'), '20 00 fa 00'), /长度/);
  assert.throws(() => api.decodePacket(uuid('2a63'), '00 00 fa 00 01'), /长度/);
  assert.equal(api.decodePacket(uuid('2a53'), '07 00 02 b4 78 00 e8 03 00 00').distanceM, 100);
});
test('FTMS 变长包与 HRS 16-bit 心率单位正确', () => {
  assert.equal(api.decodePacket(uuid('2ad2'), '41 00 fa 00').powerW, 250);
  const bike = api.decodePacket(uuid('2ad2'), '44 00 b8 0b b4 00 fa 00');
  assert.equal(bike.cadenceRpm, 90);
  assert.equal(bike.speedKmh, 30);
  assert.equal(api.decodePacket(uuid('2a37'), '01 2c 01').heartRateBpm, 300);
});
test('FTMS 目标能力要求范围，数据属性与控制点属性收敛', () => {
  const board=snapshot();
  board.services.push({uuid:uuid('1826'),enumeration:'complete',characteristics:[
    {uuid:uuid('2acc'),properties:'read',readStatus:'ok',raw:'04 14 00 00 01 00 00 00'},
    {uuid:uuid('2acd'),properties:'read|notify',readStatus:'ok',raw:'00'},
    {uuid:uuid('2ad9'),properties:'write|write-wr|indicate',readStatus:'not-readable',raw:null},
  ]});
  let findings=api.analyze(board,snapshot('reference'),context).findings;
  for(const code of ['missing-2ad4','property-2acd','property-2ad9'])
    assert.ok(findings.some(f=>f.code===code&&f.kind==='standard'));
  board.services[1].characteristics.push(
    {uuid:uuid('2ad4'),properties:'read',readStatus:'ok',raw:'2c 01 c4 09 01 00'},
    {uuid:uuid('2ada'),properties:'notify',readStatus:'not-readable',raw:null});
  board.services[1].characteristics[1].properties='notify';
  board.services[1].characteristics[2].properties='write|indicate';
  findings=api.analyze(board,snapshot('reference'),context).findings;
  assert.ok(!findings.some(f=>f.kind==='standard'));
  assert.match(api.featureSummary('2a65','08 00 10 00'),/非分布式总功率/);
});
test('通知异常生成有原始证据的标准问题', () => {
  const board = snapshot();
  board.samples[0].raw = '20 00 fa 00';
  assert.ok(api.analyze(board, snapshot('reference'), context).findings
    .some(f => f.code === 'payload-2a63' && f.kind === 'standard' && f.evidence.includes('20 00')));
});
function ftmsSnapshot() {
  const board = snapshot();
  board.services.push({ uuid: uuid('1826'), enumeration: 'complete', characteristics: [
    { uuid: uuid('2acc'), properties: 'read', readStatus: 'ok', raw: '02 40 00 00 08 00 00 00' },
    { uuid: uuid('2ad8'), properties: 'read', readStatus: 'ok', raw: '00 00 d0 07 01 00' },
    { uuid: uuid('2ad2'), properties: 'notify', readStatus: 'not-readable', raw: null },
    { uuid: uuid('2ad9'), properties: 'write|indicate', readStatus: 'not-readable', raw: null },
    { uuid: uuid('2ada'), properties: 'notify', readStatus: 'not-readable', raw: null },
  ] });
  return board;
}
test('目标控制要求控制点，控制点要求状态，部分枚举保持未知', () => {
  const board = ftmsSnapshot(), service = board.services[1];
  service.characteristics = service.characteristics.filter(ch => ch.uuid !== uuid('2ad9'));
  assert.ok(api.analyze(board, snapshot('reference'), context).findings
    .some(f => f.code === 'missing-2ad9' && f.kind === 'standard'));
  service.enumeration = 'partial';
  assert.ok(api.analyze(board, snapshot('reference'), context).findings
    .some(f => f.code === 'missing-2ad9' && f.kind === 'unknown'));
  const noStatus = ftmsSnapshot();
  noStatus.services[1].characteristics = noStatus.services[1].characteristics.filter(ch => ch.uuid !== uuid('2ada'));
  assert.ok(api.analyze(noStatus, snapshot('reference'), context).findings
    .some(f => f.code === 'missing-2ada' && f.kind === 'standard'));
});
test('FTMS 成功读取的范围校验长度和值域，读取失败仍为未知', () => {
  for (const raw of ['ff', '00 00 d0 07 01', '00 00 d0 07 01 00 00', 'd0 07 00 00 01 00']) {
    const board = ftmsSnapshot();
    board.services[1].characteristics[1].raw = raw;
    assert.ok(api.analyze(board, snapshot('reference'), context).findings
      .some(f => f.code === 'range-value-2ad8' && f.kind === 'standard'), raw);
  }
  const board = ftmsSnapshot();
  Object.assign(board.services[1].characteristics[1], { raw: null, readStatus: 'failed' });
  const findings = api.analyze(board, snapshot('reference'), context).findings;
  assert.ok(findings.some(f => f.code === 'value-2ad8' && f.kind === 'unknown'));
  assert.ok(!findings.some(f => f.code === 'range-value-2ad8'));
});
test('跑步机通知按 flags 解析，截断样本不能记为已校验', () => {
  const packet = api.decodePacket(uuid('2acd'), '04 05 b0 04 64 00 00 96 3c 00');
  assert.equal(packet.speedKmh, 12);
  assert.equal(packet.distanceM, 100);
  assert.equal(packet.heartRateBpm, 150);
  assert.equal(packet.elapsedTimeS, 60);
  assert.throws(() => api.decodePacket(uuid('2acd'), '04 05'), /长度/);
  const board = snapshot();
  board.samples = [{ uuid: uuid('2acd'), at: 1000, raw: '04 05' }];
  assert.ok(api.analyze(board, snapshot('reference'), context).findings
    .some(f => f.code === 'payload-2acd' && f.kind === 'standard'));
});
test('未知协议通知保持未解析，不提供已校验零异常的证据', () => {
  const board = snapshot();
  board.samples = [{ uuid: uuid('fff1'), at: 1000, raw: '00' }];
  const result = api.analyze(board, snapshot('reference'), context);
  assert.equal(result.signals.board[0].unparsed, 1);
  assert.match(result.signals.board[0].summary, /未解析/);
  assert.ok(result.findings.some(f => f.code === 'payload-unknown-fff1' && f.kind === 'unknown'));
});
test('旧连接晚到不得断开已成功的新代次连接', async () => {
  const html = fs.readFileSync(path.join(__dirname, '../tools/web/ble-monitor.html'), 'utf8');
  const source = html.slice(html.indexOf('const COMPATIBILITY_HANDOFF_KEY='), html.indexOf('const DIS_CHARS ='));
  const sandbox = vm.createContext({ localStorage: { getItem: () => null } });
  vm.runInContext(source, sandbox);
  const pending = [];
  const device = { gatt: { connected: false, disconnect() { this.connected = false; },
    connect() { return new Promise(resolve => pending.push(() => { this.connected = true; resolve(this); })); } } };
  sandbox.device = device;
  const old = vm.runInContext('connectPageGatt(device,bleConnectionEpoch)', sandbox);
  vm.runInContext('bleCompatibilityPaused=true;bleConnectionEpoch++;', sandbox);
  device.gatt.disconnect();
  vm.runInContext('bleCompatibilityPaused=false;bleConnectionEpoch++;', sandbox);
  const current = vm.runInContext('connectPageGatt(device,bleConnectionEpoch)', sandbox);
  pending[1]();
  await current;
  pending[0]();
  await assert.rejects(old, /暂停|代次|失效/);
  assert.equal(device.gatt.connected, true);
});
test('跑步机采样入口订阅 Treadmill Data 原始通知', async () => {
  const html = fs.readFileSync(path.join(__dirname, '../tools/web/ble-monitor.html'), 'utf8');
  const dictionaryStart = html.indexOf('const UUIDS =');
  const dictionary = html.slice(dictionaryStart, html.indexOf('\n};', dictionaryStart) + 3);
  const source = html.slice(html.indexOf('async function subscribeNotifications('), html.indexOf('async function connectAuthorized('));
  let received = false;
  const ch = { async startNotifications() { received = true; }, addEventListener() {} };
  const rec = { subscriptions: {}, server: { async getPrimaryService(service) {
    if(service !== uuid('1826'))throw new Error('not found');
    return { async getCharacteristic(char) { if(char !== uuid('2acd'))throw new Error('not found');return ch; } };
  } } };
  const sandbox = vm.createContext({ rec, log() {}, handleNotify() {} });
  vm.runInContext(dictionary + source, sandbox);
  await vm.runInContext('subscribeNotifications(rec)', sandbox);
  assert.equal(received, true);
  assert.equal(rec.subscriptions.treadmill, true);
});
test('身份差异只建议现有身份写入，不宣称完整克隆或目标兼容', () => {
  const reference = snapshot('reference');
  reference.info.manufacturer = '示例品牌';
  const result = api.analyze(snapshot(), reference, context);
  assert.ok(result.findings.some(f => f.code === 'identity' && f.kind === 'candidate'));
  assert.equal(result.verdict, '待目标端验证');
});
test('参考设备正常连接必须用户确认', () => {
  const result = api.analyze(snapshot(), snapshot('reference'), { ...context, referenceWorks: false });
  assert.ok(result.findings.some(f => f.code === 'reference-unverified' && f.kind === 'unknown'));
});
test('报告往返保留快照和版本，导入不授予实测通过', () => {
  const report = api.makeReport(context, snapshot(), snapshot('reference'), null);
  const imported = api.importReport(JSON.stringify(report));
  assert.deepEqual(imported.snapshots.board, report.snapshots.board);
  assert.equal(imported.schema, 'bikepower.compatibility.v1');
  assert.equal(api.analyze(imported.snapshots.board, imported.snapshots.reference, imported.context).verdict,
    '待目标端验证');
  const manual = api.makeReport(context, snapshot(), snapshot('reference'), {
    recordedAt: '2026-11-05T12:00:00Z', notes: '人工测试',
    outcomes: { discovery: 'pass', connection: 'pass', data: 'not-run', reconnect: 'not-run', control: 'not-applicable' },
  });
  const importedManual = api.importReport(JSON.stringify(manual));
  assert.equal(importedManual.test.source, 'imported-user-record');
  assert.equal(api.makeReport(importedManual.context, importedManual.snapshots.board,
    importedManual.snapshots.reference, importedManual.test).test.source, 'imported-user-record');
});
test('快照复制与会话数据隔离', () => {
  const record = { device: { id: 'one', name: '开发板' }, info: { firmware: '候选' },
    services: snapshot().services, compatibilitySamples: snapshot().samples,
    gattCoverage: snapshot().coverage };
  const frozen = api.capture(record);
  record.services[0].characteristics[1].raw = '00 00 00 00';
  assert.equal(frozen.services[0].characteristics[1].raw, '08 00 00 00');
});
test('输入限制拒绝未知版本、畸形 UUID、无穷时间、巨大文件和重复特征', () => {
  const report = api.makeReport(context, snapshot(), snapshot('reference'), null);
  const invalid = copy(report);
  invalid.schema = 'unknown';
  assert.throws(() => api.importReport(JSON.stringify(invalid)), /版本/);
  invalid.schema = report.schema;
  invalid.snapshots.board.services[0].uuid = '<img>';
  assert.throws(() => api.importReport(JSON.stringify(invalid)), /UUID/);
  const badTime = snapshot();
  badTime.samples[0].at = Infinity;
  assert.throws(() => api.validateSnapshot(badTime), /时间/);
  assert.throws(() => api.importReport(' '.repeat(1024 * 1024 + 1)), /大小/);
  const duplicate = snapshot();
  duplicate.services[0].characteristics.push(copy(duplicate.services[0].characteristics[0]));
  assert.throws(() => api.validateSnapshot(duplicate), /重复/);
});

module.exports = { snapshot, context };
