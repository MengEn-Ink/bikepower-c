const assert = require('node:assert/strict');
const { test } = require('node:test');
const help = require('../tools/web/ble-connection-help.js');
const cases = [
  ['cancelled', { name: 'NotFoundError', message: 'User cancelled' }, { phase:'picker' }],
  ['permission', { name:'SecurityError', message:'fixture' }, {}],
  ['permission', { name:'NotAllowedError', message:'fixture' }, {}],
  ['environment', new Error('fixture'), {secure:false}],
  ['radio', new Error('Bluetooth adapter powered off'), {}],
  ['pairing', new Error('Peer removed pairing information'), {}],
  ['pairing', new Error('PIN/Key Missing'), {}],
  ['generic', new Error('Connection Error: Connection attempt failed.'), {}],
  ['generic', new Error('GATT operation failed for unknown reason'), {}],
  ['busy', new Error('上一连接仍在建立或收尾'), {}],
  ['timeout', new Error('连接超时（20 秒）'), {}],
  ['disconnected', new Error('GATT Server is disconnected.'), {}],
  ['discovery', new Error('蓝牙服务发现失败：Service discovery failed'), {}],
  ['unsupported', new Error('该设备未提供运动服务，已过滤'), {}],
  ['paused', new Error('网页蓝牙已暂停'), {}],
  ['serial', new Error('串口会话期间禁止新建 BLE 会话'), {}],
  ['authorized', new Error('当前浏览器不支持已授权设备列表'), {}],
  ['generic', { name:'NotFoundError',message:'unknown characteristic' }, {phase:'connect'}],
];
for (const [expected,error,context] of cases) test(expected+' '+error.message,()=>{
  const result=help.classify(error,context);
  assert.equal(result.category,expected);
  assert.ok(result.title&&result.summary&&result.steps.length);
  assert.equal(result.technical,error.message);
});
test('generic is conditional and does not claim known pairing or occupancy',()=>{
  const result=help.classify(new Error('Connection attempt failed'));
  assert.match(result.title,/原因未确定/);
  assert.match(result.steps.join(' '),/如果刚刷入/);
  assert.match(result.steps.join(' '),/仅.*BikePower/);
  assert.match(result.steps.join(' '),/未刚刷机.*不要直接清绑定/);
  assert.doesNotMatch(result.title,/被别的 App|旧配对信息已失效/);
});
test('pairing provides both OS paths and keeps scope narrow',()=>{
  const text=help.text(help.classify(new Error('Peer removed pairing information')));
  for(const token of ['Mac','Windows','忽略此设备','删除设备','普通传感器读取','不要整片擦除'])assert.ok(text.includes(token),token);
});
test('all explicit categories are accessible and do not mutate inputs',()=>{
  const error=Object.freeze({name:'Error',message:'original'});
  for(const category of Object.keys(help.catalog))assert.equal(help.classify(error,{category}).category,category);
  assert.equal(error.message,'original');
});
