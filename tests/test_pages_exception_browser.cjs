/* 生产 DOM、编码器和响应监听器异常回归；只替换蓝牙设备传输与时钟。 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { chromium } = require('./web-flash/node_modules/playwright');
const root = path.join(__dirname, '..');
const fixture = vm.runInNewContext(
  '(' + fs.readFileSync(path.join(__dirname, 'test_pages_bridge_browser.cjs'), 'utf8')
    .match(/const fixture = ([\s\S]*?);\n\(async/)[1] + ')');

(async () => {
  const browser = await chromium.launch({ headless: true });
  try {
    for (const scenario of ['success', 'wrong-response', 'notify-reject', 'begin-reject', 'begin-write', 'begin-timeout',
      'group-write', 'cancel-write', 'commit-reject', 'commit-write', 'commit-timeout', 'malformed', 'disconnect']) {
      const page = await browser.newPage();
      const errors = [];
      page.on('pageerror', e => errors.push(e.message));
      await page.route('**/*', route => {
        const url = new URL(route.request().url());
        if (url.hostname !== 'example.test') return route.abort();
        if (url.pathname.endsWith('.js') || url.pathname.endsWith('.css'))
          return route.fulfill({ path: path.join(root, 'tools/web', path.basename(url.pathname)) });
        if (url.pathname.endsWith('.json')) return route.fulfill({ json: { available: false } });
        return route.fulfill({ path: path.join(root, 'tools/web/ble-monitor.html'), contentType: 'text/html' });
      });
      await page.addInitScript(() => {
        sessionStorage.setItem('bikepower.linkgate.skip.v1', '1');
        const original = window.setTimeout;
        window.setTimeout = (fn, ms, ...args) => original(fn, ms === 10000 ? 80 : ms, ...args);
      });
      await page.goto('https://example.test/');
      await page.evaluate(({ fixture, scenario }) => {
        document.getElementById('connect-gate').classList.add('hidden');
        document.body.classList.remove('gate-lock');
        const device = new EventTarget();
        const current = { ...fixture, ...parseConfigGroup('bridge', configDataView(new Uint8Array(fixture.bridge_wire))) };
        window.writes = [];
        class Characteristic extends EventTarget {
          constructor(key) { super(); this.key = key; this.listeners = new Set(); }
          addEventListener(type, fn, options) { if (type === 'characteristicvaluechanged') this.listeners.add(fn); super.addEventListener(type, fn, options); }
          removeEventListener(type, fn, options) { this.listeners.delete(fn); super.removeEventListener(type, fn, options); }
          async startNotifications() { if (scenario === 'notify-reject') throw new Error('订阅失败'); }
          emit(opcode, tx, result = 0) {
            const bytes = new Uint8Array(12), view = new DataView(bytes.buffer);
            bytes[0] = 0x80; bytes[1] = opcode; bytes[2] = result;
            view.setUint32(4, tx, true); view.setUint32(8, 2, true);
            this.value = view;
            this.dispatchEvent(new Event('characteristicvaluechanged'));
          }
          async writeValueWithResponse(bytes) {
            writes.push({ key: this.key, bytes: Array.from(bytes) });
            if (this.key !== 'control') {
              if (['group-write', 'cancel-write'].includes(scenario)) throw new Error('组写入失败');
              return;
            }
            const view = configDataView(bytes), op = bytes[0], tx = view.getUint32(op === 1 ? 2 : 1, true);
            if (op === 3) { if (scenario === 'cancel-write') throw new Error('取消失败'); return; }
            if (op === 1 && scenario === 'begin-write') throw new Error('BEGIN 传输失败');
            if (op === 1 && scenario === 'begin-timeout') return;
            if (op === 2 && scenario === 'commit-write') throw new Error('COMMIT 传输失败');
            if (op === 2 && scenario === 'disconnect') {
              singleDevice.server.connected = false;
              device.dispatchEvent(new Event('gattserverdisconnected'));
              return;
            }
            if (op === 2 && scenario === 'commit-timeout') return;
            if (op === 2 && scenario === 'malformed') {
              this.value = new DataView(new Uint8Array([0x80]).buffer);
              this.dispatchEvent(new Event('characteristicvaluechanged')); return;
            }
            if (scenario === 'wrong-response') { this.emit(op, tx ^ 1); this.emit(op === 1 ? 2 : 1, tx); }
            this.emit(op, tx, scenario === 'begin-reject' && op === 1 ? 4 : scenario === 'commit-reject' && op === 2 ? 6 : 0);
          }
        }
        const characters = Object.fromEntries([...CONFIG_GROUPS.map(g => g.key), 'control'].map(key => [key, new Characteristic(key)]));
        singleDevice = { device, server: { connected: true }, configState: {
          supported: true, saving: false, current, meta: { protocol: 1, schema: 13, revision: 1 }, characters,
        } };
        window.recover = () => { scenario = 'success'; singleDevice.server.connected = true; setConfigBusy(false); };
        const host = document.createElement('div');
        host.innerHTML = renderConfigEditor(singleDevice); document.body.prepend(host);
        bindConfigForm(singleDevice);
      }, { fixture, scenario });
      await page.fill('#cfg-power', '275');
      await page.click('#config-save');
      await page.waitForFunction(() => !singleDevice.configState.saving
        && (writes.length > 0 || document.getElementById('config-status').textContent.includes('订阅失败')));
      await page.waitForTimeout(160);
      const result = await page.evaluate(() => ({
        writes, revision: singleDevice.configState.meta.revision, power: singleDevice.configState.current.power,
        listeners: singleDevice.configState.characters.control.listeners.size,
        status: document.getElementById('config-status').textContent,
        draft: document.getElementById('cfg-power').value,
      }));
      const success = ['success', 'wrong-response'].includes(scenario);
      assert.equal(result.revision, success ? 2 : 1, scenario);
      assert.equal(result.power, success ? 275 : 260, scenario);
      assert.equal(result.draft, '275');
      assert.equal(result.listeners, 0, `${scenario} 响应监听器未释放`);
      if (['begin-timeout', 'group-write', 'cancel-write', 'commit-write'].includes(scenario))
        assert.ok(result.writes.some(w => w.key === 'control' && w.bytes[0] === 3), scenario);
      if (['commit-timeout', 'disconnect', 'commit-write'].includes(scenario))
        assert.match(result.status, /核实|不确定/, `${scenario} 必须告知保存结果未知`);
      assert.deepEqual(errors, [], `${scenario} 未处理异常`);
      if (!success) {
        await page.evaluate(() => recover());
        await page.click('#config-save');
        await page.waitForFunction(() => singleDevice.configState.meta.revision === 2);
        assert.equal(await page.evaluate(() => singleDevice.configState.current.power), 275);
        assert.equal(await page.evaluate(() => singleDevice.configState.characters.control.listeners.size), 0);
        assert.deepEqual(errors, [], `${scenario} 重试未处理异常`);
      }
      console.log(`[pages-exception] PASS ${scenario}`);
      await page.close();
    }
  } finally { await browser.close(); }
})().catch(e => { console.error(e); process.exitCode = 1; });
