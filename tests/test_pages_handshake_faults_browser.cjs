/* Actual Pages handshake functions with deterministic BLE transport faults; no hardware. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('./web-flash/node_modules/playwright');
const root = path.join(__dirname, '..', 'tools/web');
(async () => {
  const browser = await chromium.launch({ headless: true });
  try {
    for (const theme of ['light', 'dark']) {
      const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.route('**/*', route => {
        const u = new URL(route.request().url());
        if (u.hostname !== 'example.test') return route.abort();
        if (u.pathname.endsWith('.json')) return route.fulfill({ json: { available: false } });
        const name = u.pathname === '/' ? 'ble-monitor.html' : path.basename(u.pathname);
        const file = path.join(root, name);
        return fs.existsSync(file) ? route.fulfill({ path: file, contentType: name.endsWith('.js') ? 'text/javascript' : name.endsWith('.css') ? 'text/css' : 'text/html' }) : route.fulfill({ status: 404 });
      });
      await page.addInitScript(theme => {
        localStorage.setItem('bikepower.theme.v1', theme);
        localStorage.setItem('bikepower.autolink.enabled', '0');
        Object.defineProperty(navigator, 'bluetooth', { value: { requestDevice() {} } });
      }, theme);
      await page.goto('https://example.test/#/monitor');
      await page.evaluate(() => {
        closeGate('skip'); autoLinkEnabled = false;
        window.makeTransport = scenario => {
          const listeners = new Set(), device = new EventTarget();
          const add = device.addEventListener.bind(device), remove = device.removeEventListener.bind(device);
          device.addEventListener = (type, fn, options) => { if (type === 'gattserverdisconnected') listeners.add(fn); add(type, fn, options); };
          device.removeEventListener = (type, fn, options) => { if (type === 'gattserverdisconnected') listeners.delete(fn); remove(type, fn, options); };
          device.name = 'BikePower'; device.id = 'fixture-' + scenario;
          const counters = { connects: 0, disconnects: 0, discoveries: 0 };
          const characteristic = new EventTarget();
          Object.assign(characteristic, { uuid: UUIDS.cpsMeasurement, properties: { notify: true }, startNotifications: async () => characteristic });
          const service = { uuid: UUIDS.cpsService, getCharacteristics: async () => [characteristic], getCharacteristic: async () => characteristic };
          device.gatt = {
            connected: false,
            async connect() { counters.connects++; this.connected = true; if (scenario === 'slow') await new Promise(resolve => { window.finishTransportConnect = () => { this.connected = true; resolve(); }; }); return this; },
            disconnect() { counters.disconnects++; this.connected = false; device.dispatchEvent(new Event('gattserverdisconnected')); },
            async getPrimaryService(uuid) {
              if (uuid === UUIDS.disService && scenario === 'disconnected') {
                device.gatt.connected = false; device.dispatchEvent(new Event('gattserverdisconnected'));
                throw new DOMException('GATT Server is disconnected. Cannot retrieve services.', 'NetworkError');
              }
              if (!this.connected) throw new DOMException('GATT Server is disconnected.', 'NetworkError');
              if (uuid !== UUIDS.cpsService) throw new DOMException('service not found', 'NotFoundError');
              return service;
            },
            async getPrimaryServices() {
              counters.discoveries++;
              if (!this.connected) throw new DOMException('GATT Server is disconnected.', 'NetworkError');
              if (scenario === 'discovery-error') throw new DOMException('Service discovery failed', 'NetworkError');
              if (scenario === 'unsupported') return [{ uuid: '00001800-0000-1000-8000-00805f9b34fb', getCharacteristics: async () => [] }];
              return [service];
            }
          };
          pageGattPermissions.set(device, ALL_OPTIONAL_SERVICES);
          return { device, counters, listeners };
        };
      });
      const disconnected = await page.evaluate(async () => {
        const t = makeTransport('disconnected');
        let message = ''; try { await connectSingleWith(t.device); } catch (e) { message = e.message; }
        return { message, counters: t.counters, listeners: t.listeners.size, log: document.getElementById('log').textContent };
      });
      assert.doesNotMatch(disconnected.message, /未提供运动服务|已过滤/, 'disconnect must not masquerade as unsupported device');
      assert.match(disconnected.message, /断开.*读取设备信息/);
      assert.equal(disconnected.counters.discoveries, 0, 'stop discovering after disconnect');
      assert.equal(disconnected.listeners, 0, 'failed connection listener released');
      assert.doesNotMatch(disconnected.log, /DIS 信息读取完成/);
      const rejected = await page.evaluate(async () => {
        const t = makeTransport('discovery-error');
        let message = ''; try { await connectSingleWith(t.device); } catch (e) { message = e.message; }
        return { message, listeners: t.listeners.size };
      });
      assert.match(rejected.message, /服务发现失败.*Service discovery failed/);
      assert.doesNotMatch(rejected.message, /未提供运动服务|已过滤/);
      assert.equal(rejected.listeners, 0);
      const unsupported = await page.evaluate(async () => {
        const t = makeTransport('unsupported');
        let message = ''; try { await connectSingleWith(t.device); } catch (e) { message = e.message; }
        return message;
      });
      assert.match(unsupported, /未提供运动服务|已过滤/, 'successful empty capability discovery still filters irrelevant devices');
      const shared = await page.evaluate(async () => {
        const t = makeTransport('slow');
        const a = connectSingleWith(t.device); const b = connectSingleWith(t.device);
        await new Promise(resolve => setTimeout(resolve, 0));
        finishTransportConnect();
        const records = await Promise.all([a, b]);
        const c = await connectSingleWith(t.device);
        const before = { ...t.counters, listeners: t.listeners.size, same: records[0] === records[1] && c === records[0] };
        disconnectSingle();
        const logs = document.getElementById('log').textContent;
        return { before, disconnectLogs: (logs.match(/设备 'BikePower' 已断开/g) || []).length, listeners: t.listeners.size };
      });
      assert.equal(shared.before.connects, 1, 'concurrent manual/auto handshake uses one connection');
      assert.equal(shared.before.disconnects, 0, 'reusing same connected device must not disconnect');
      assert.equal(shared.before.listeners, 1);
      assert.equal(shared.before.same, true);
      assert.equal(shared.disconnectLogs, 2, 'one failed disconnect and one valid disconnect, no historical listener duplicates');
      assert.equal(shared.listeners, 0);
      const timeout = await page.evaluate(async () => {
        const t = makeTransport('slow');
        let message = '', retry = '';
        try { await connectSingleWith(t.device, 'picker', 30); } catch (e) { message = e.message; }
        const held = !!singleConnectionAttempt;
        try { await connectSingleWith(t.device); } catch (e) { retry = e.message; }
        finishTransportConnect();
        await new Promise(resolve => setTimeout(resolve, 20));
        return { message, retry, held, unlocked: !singleConnectionAttempt, counters: t.counters, listeners: t.listeners.size, connected: isSingleConnected() };
      });
      assert.match(timeout.message, /超时/);
      assert.equal(timeout.held, true, 'timeout must not release lock while connect is unresolved');
      assert.match(timeout.retry, /仍在建立或收尾/);
      assert.equal(timeout.counters.connects, 1);
      assert.equal(timeout.counters.discoveries, 0, 'late connection cannot read services or publish success');
      assert.equal(timeout.unlocked, true);
      assert.equal(timeout.listeners, 0);
      assert.equal(timeout.connected, false);
      const handoff = await page.evaluate(async () => {
        const t = makeTransport('slow');
        const pending = connectSingleWith(t.device).catch(e => e.message);
        await new Promise(resolve => setTimeout(resolve, 0));
        setCompatibilityHandoff(true, false);
        const message = await pending;
        finishTransportConnect();
        await new Promise(resolve => setTimeout(resolve, 20));
        setCompatibilityHandoff(false, false);
        return { message, discoveries: t.counters.discoveries, unlocked: !singleConnectionAttempt, connected: isSingleConnected() };
      });
      assert.match(handoff.message, /暂停/);
      assert.equal(handoff.discoveries, 0);
      assert.equal(handoff.unlocked, true);
      assert.equal(handoff.connected, false);
      assert.deepEqual(errors, []);
      console.log(`PASS ${theme}: disconnect != unsupported, discovery error retained, listener cleanup, irrelevant-device filter, concurrent/same-device connection sharing; no hardware`);
      await page.close();
    }
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
