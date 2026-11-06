/* 教学模型按真实操作顺序验证，数据与设备会话隔离。 */
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const source = path.join(__dirname, '../tools/web/operation-demos.js');
let api;
test('操作演示提供独立模型', () => {
  assert.ok(fs.existsSync(source), '缺少操作演示模块');
  api = require(source);
  assert.equal(typeof api.practice, 'function');
});
test('练习短按减少目标，按住期间临时增加', () => {
  if (!api) return;
  let state = api.practice(null, 'down', 0);
  state = api.practice(state, 'up', 300);
  assert.equal(state.power, 180);
  state = api.practice(state, 'down', 1000);
  state = api.practice(state, 'tick', 1900);
  assert.equal(state.power, 240);
  state = api.practice(state, 'up', 1900);
  assert.equal(state.power, 240);
  assert.equal(state.mode, 'ble');
});
test('满2秒仍按住不配网，松开回滚并开始3秒确认', () => {
  if (!api) return;
  let state = api.practice(null, 'down', 0);
  state = api.practice(state, 'tick', 2100);
  assert.equal(state.mode, 'ble');
  assert.ok(state.power > 200);
  state = api.practice(state, 'up', 2300);
  assert.equal(state.power, 200);
  assert.equal(state.mode, 'confirm');
  assert.equal(state.deadline, 5300);
  state = api.practice(state, 'down', 3300);
  assert.equal(state.mode, 'wifi');
  assert.equal(state.power, 200);
});
test('超时取消，窗口结束时的按下不能开启热点', () => {
  if (!api) return;
  let state = api.practice(null, 'down', 0);
  state = api.practice(state, 'up', 2000);
  state = api.practice(state, 'tick', 5000);
  assert.equal(state.mode, 'ble');
  assert.equal(state.power, 200);
  state = api.practice(state, 'down', 5000);
  assert.equal(state.mode, 'ble');
});
test('取消手势回滚，键盘与指针共用事件模型', () => {
  if (!api) return;
  let state = api.practice(null, 'down', 0);
  state = api.practice(state, 'tick', 1200);
  state = api.practice(state, 'cancel', 1300);
  assert.equal(state.pressed, false);
  assert.equal(state.power, 200);
  assert.equal(state.mode, 'ble');
});
test('草稿不改变输出，失败保留草稿，成功才提交', () => {
  if (!api) return;
  let state = api.configuration(null, 'edit', { power: 260 });
  assert.equal(state.applied, 200);
  assert.equal(state.draft, 260);
  state = api.configuration(state, 'fail');
  assert.equal(state.applied, 200);
  assert.equal(state.draft, 260);
  assert.equal(state.status, 'error');
  state = api.configuration(state, 'save');
  assert.equal(state.applied, 260);
  assert.equal(state.status, 'saved');
});
test('三种校正保持0W，固定值与比例遵守输出上限', () => {
  if (!api) return;
  assert.equal(api.correctedPower(200, 'offset', 20), 220);
  assert.equal(api.correctedPower(200, 'percent', 5), 210);
  assert.equal(api.correctedPower(100, 'percent', 200), 300);
  for (const mode of ['raw', 'offset', 'percent']) {
    assert.equal(api.correctedPower(0, mode, 200), 0);
  }
  assert.equal(api.correctedPower(1900, 'percent', 200), 2000);
  assert.equal(api.correctedPower(10, 'offset', -200), 0);
});
test('网页占用时App不能连接，释放后才能交接', () => {
  if (!api) return;
  let state = api.handoff(null, 'app');
  assert.equal(state.owner, 'web');
  assert.equal(state.status, 'occupied');
  state = api.handoff(state, 'release');
  assert.equal(state.owner, 'none');
  state = api.handoff(state, 'app');
  assert.equal(state.owner, 'app');
  state = api.handoff(state, 'web');
  assert.equal(state.owner, 'app');
  assert.equal(state.status, 'occupied');
});
test('板图保留实际位置，用户只看操作键与指示灯', () => {
  if (!api) return;
  const board = api.boardMarkup();
  assert.match(board, /BOOT/);
  assert.match(board, /RST/);
  assert.match(board, /data-board-led/);
  assert.match(board, /指示灯/);
  assert.match(board, /data-led-halo/);
  assert.doesNotMatch(board, /合宙|ESP32|CORE-/);
  assert.match(api.boardSource, /wiki\.luatos\.com\/chips\/esp32c3\/board\.html/);
});
test('每类使用步骤均有可逐步播放的视觉演示', () => {
  const required = ['install', 'connect', 'hotspot', 'cycling', 'running', 'profile',
    'device', 'step', 'lights', 'upstream', 'about', 'exit', 'troubleshoot', 'overview', 'compare'];
  for (const name of required) {
    assert.ok(api.guides[name].frames.length >= 3, name);
    assert.match(api.guideMarkup(name), /data-guide-stage/);
    assert.match(api.guideMarkup(name), /data-guide-play/);
    assert.doesNotMatch(api.guideMarkup(name), /合宙|ESP32|开发板|GATT|NVS/);
  }
});
