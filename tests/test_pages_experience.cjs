/* 按钮教学按固定时刻验证状态；演示不得调用设备接口。 */
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = path.join(__dirname, '../tools/web/pages-experience.js');
let api;
test('页面教学模块提供独立时间轴', () => {
  assert.ok(fs.existsSync(source), '缺少按钮教学与页面更新模块');
  const context = { module: { exports: {} } };
  vm.runInNewContext(fs.readFileSync(source, 'utf8'), context);
  api = context.module.exports;
  assert.equal(typeof api.buttonFrame, 'function');
});
test('短按减一步；中长按累加；2秒松开回滚后等待确认', () => {
  assert.equal(api.buttonFrame('short', 400).power, 180);
  assert.ok(api.buttonFrame('adjust', 900).power > 200);
  const confirm = api.buttonFrame('wifi', 2400);
  assert.equal(confirm.power, 200);
  assert.equal(confirm.state, 'confirm');
  assert.equal(confirm.pressed, false);
});
test('二次按下才进入配网；不确认会超时恢复蓝牙', () => {
  assert.equal(api.buttonFrame('wifi', 3400).state, 'wifi');
  assert.match(api.scenes.wifi.title, /开启 WiFi 配网/);
  assert.match(api.buttonFrame('wifi', 0).text, /热点.*手机.*配置页/);
  assert.match(api.buttonFrame('wifi', 3400).text, /BPW_XXXX.*12345678.*192\.168\.4\.1/);
  assert.equal(api.buttonFrame('cancel', 5400).state, 'ble');
  assert.equal(api.buttonFrame('cancel', 5400).power, 200);
});
test('暂停和逐步显示使用相同时间轴；非法场景拒绝', () => {
  assert.equal(api.buttonFrame('wifi', 2400).text, api.buttonFrame('wifi', 2400).text);
  assert.throws(() => api.buttonFrame('invalid', 0));
});
test('页面发布时间只接受有效构建元数据，不伪造时间', () => {
  assert.equal(api.releaseLabel(null), '本地预览 · 未发布');
  assert.equal(api.releaseLabel({ built_at: 'invalid', commit: 'a'.repeat(40) }), '更新信息暂不可用');
  assert.match(api.releaseLabel({ built_at: '2026-11-04T10:56:24Z', commit: 'a'.repeat(40), dirty: false }), /2026.*11.*04.*18:56.*aaaaaaa/);
});
