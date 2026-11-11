/* Production Pages controls + real ESP Web Tools renderer, fake BLE/SerialPort only. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { chromium } = require('./web-flash/node_modules/playwright');
const root = path.join(__dirname, '..');
const bytes = Buffer.from('fixture, never firmware');
const item = { manifest: 'firmware/manifest.json', download: 'firmware/bike.bin', size: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') };
const manifest = { name: 'BikePower', version: '4.3.3', new_install_prompt_erase: true, builds: [{ chipFamily: 'ESP32-C3', parts: [{ offset: 0, path: 'bike.bin' }] }] };
(async () => {
  const browser = await chromium.launch({ headless: true, ...(process.env.BP_BROWSER_CHANNEL ? { channel: process.env.BP_BROWSER_CHANNEL } : {}) });
  try {
    for (const theme of ['light', 'dark']) {
      const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.route('**/*', route => {
        const u = new URL(route.request().url());
        if (u.pathname.includes('/vendor/esp-web-tools-10.4.0/')) return route.fulfill({ path: path.join(root, 'tools/web/vendor/esp-web-tools-10.4.0', path.basename(u.pathname)), contentType: 'text/javascript', headers: { 'Access-Control-Allow-Origin': '*' } });
        if (u.hostname !== 'example.test') return route.abort();
        if (u.pathname.endsWith('manifest.json')) return route.fulfill({ json: manifest });
        if (u.pathname.endsWith('bike.bin')) return route.fulfill({ body: bytes });
        if (u.pathname.endsWith('.json')) return route.fulfill({ json: { available: false } });
        const name = u.pathname.endsWith('/') ? 'ble-monitor.html' : path.basename(u.pathname);
        const file = path.join(root, 'tools/web', name);
        return fs.existsSync(file) ? route.fulfill({ path: file, contentType: name.endsWith('.js') ? 'text/javascript' : name.endsWith('.css') ? 'text/css' : 'text/html' }) : route.fulfill({ status: 404 });
      });
      await page.addInitScript(theme => {
        localStorage.setItem('bikepower.theme.v1', theme);
        if (localStorage.getItem('bikepower.compatibility.handoff.v1') === null) localStorage.setItem('bikepower.compatibility.handoff.v1', '1');
        window.picks = 0;
        window.mode = 'cancel';
        Object.defineProperty(navigator, 'bluetooth', { value: {
          getDevices: async () => [],
          requestDevice: () => {
            window.picks++;
            window.activation = navigator.userActivation.isActive;
            if (window.mode === 'success') return Promise.resolve(window.fakeDevice);
            if (window.mode === 'pending') return new Promise((resolve, reject) => { window.cancelPick = () => reject(new DOMException('cancelled', 'NotFoundError')); });
            return Promise.reject(new DOMException('fixture', window.mode === 'denied' ? 'SecurityError' : 'NotFoundError'));
          }
        } });
        window.serialCounts = { picks: 0, opens: 0, closes: 0, writes: 0 };
        const port = {
          readable: null, writable: null,
          getInfo: () => ({ usbVendorId: 0x303a, usbProductId: 0x1001 }), async setSignals() {},
          async open() { window.serialCounts.opens++; this.readable = new ReadableStream(); this.writable = new WritableStream({ write() { window.serialCounts.writes++; } }); },
          async close() { window.serialCounts.closes++; this.readable = null; this.writable = null; }
        };
        const serial = new EventTarget();
        serial.requestPort = async () => { window.serialCounts.picks++; return port; };
        Object.defineProperty(navigator, 'serial', { value: serial });
      }, theme);
      await page.goto('https://example.test/bikepower-c/#/monitor');
      await page.locator('#ble-pause-notice').waitFor();
      const scan = page.getByRole('button', { name: '扫描并连接设备', exact: true });
      await scan.click();
      assert.equal(await page.evaluate(() => picks), 0, 'paused must not claim device');
      assert.match(await page.locator('#ble-connect-feedback').textContent(), /恢复网页连接/);
      await page.getByRole('button', { name: '结束实测，恢复网页连接', exact: true }).click();
      assert.equal(await page.evaluate(() => picks), 0, 'resume is not permission to auto-connect');
      await scan.click();
      await page.waitForFunction(() => document.getElementById('ble-connect-feedback').textContent.includes('已取消'));
      assert.equal(await page.evaluate(() => activation), true, 'requestDevice must remain within click activation');
      await page.evaluate(() => { window.mode = 'pending'; });
      await scan.click();
      assert.match(await page.locator('#ble-connect-feedback').textContent(), /正在打开/);
      await scan.click();
      assert.equal(await page.evaluate(() => picks), 2, 'repeated clicks cannot open duplicate pickers');
      await page.evaluate(() => cancelPick());
      await page.evaluate(() => { window.mode = 'denied'; });
      await scan.click();
      await page.waitForFunction(() => document.getElementById('ble-connect-feedback').textContent.includes('蓝牙权限'));
      await page.evaluate(() => openGate({ force: true }));
      await page.locator('#gate-connect').click();
      await page.waitForFunction(() => gateState() === 'error');
      assert.match(await page.locator('#gate-desc').textContent(), /蓝牙权限/);
      await page.evaluate(() => closeGate('skip'));
      await page.evaluate(() => {
        window.fakeCharacteristic = new EventTarget();
        Object.assign(fakeCharacteristic, { uuid: UUIDS.cpsMeasurement, properties: { notify: true }, startNotifications: async () => fakeCharacteristic });
        const service = { uuid: UUIDS.cpsService, getCharacteristics: async () => [fakeCharacteristic], getCharacteristic: async uuid => { if (uuid !== UUIDS.cpsMeasurement) throw Error('not found'); return fakeCharacteristic; } };
        const device = new EventTarget(); device.id = 'fixture-cps'; device.name = '固定回归功率计';
        device.gatt = { connected: false, connect: async () => { device.gatt.connected = true; return device.gatt; }, disconnect: () => { device.gatt.connected = false; device.dispatchEvent(new Event('gattserverdisconnected')); }, getPrimaryServices: async () => [service], getPrimaryService: async uuid => { if (uuid !== UUIDS.cpsService) throw Error('not found'); return service; } };
        window.fakeDevice = device; window.mode = 'success';
      });
      await scan.click();
      await page.waitForFunction(() => isSingleConnected());
      await page.evaluate(() => {
        fakeCharacteristic.value = new DataView(Uint8Array.from([0, 0, 4, 1]).buffer);
        fakeCharacteristic.dispatchEvent(new Event('characteristicvaluechanged'));
      });
      assert.equal(await page.locator('#m-power').textContent(), '260', 'production subscription must render device notification');
      await page.evaluate(() => disconnectSingle());
      assert.equal(await page.evaluate(() => isSingleConnected()), false);
      await page.reload();
      assert.equal(await page.locator('#ble-pause-notice').isVisible(), false, 'explicit resume survives reload');
      await page.evaluate(async item => {
        await BikePowerFlash.loadDialog();
        firmwareCatalog = { available: true };
        firmwareReleases = [{ tag: 'v4.3.3', version: '4.3.3', latest: true, boards: { 'generic-c3': item } }];
        webInstallerReady = true; selectFirmwareRelease(null); switchMode('flash');
      }, item);
      await page.locator('#flash-activate').click();
      let dialog = page.locator('ewt-install-dialog');
      await dialog.getByText('安装 BikePower 固件', { exact: true }).waitFor();
      await dialog.locator('ew-icon-button').click();
      await page.waitForFunction(() => !flashSession.isBusy());
      assert.equal(await page.locator('#ob-success').count(), 0, 'close without install must never show success');
      assert.match(await page.locator('#flash-message').textContent(), /未确认刷写成功/);
      await page.locator('#flash-activate').click();
      dialog = page.locator('ewt-install-dialog');
      await dialog.getByText('安装 BikePower 固件', { exact: true }).click();
      await dialog.getByText('下一步', { exact: true }).click();
      assert.equal(await dialog.locator('ew-checkbox').count(), 0);
      // Inject fixture terminal state into the REAL upstream renderer, not into the adapter.
      // This tests rendered terminal/close semantics; it does NOT prove a hardware write.
      await dialog.evaluate(async host => {
        host._installConfirmed = true;
        host._installState = { state: 'writing', chipFamily: 'ESP32-C3', details: { percentage: 100 } };
        host._client = null; host.requestUpdate(); await host.updateComplete;
      });
      await page.waitForTimeout(2200);
      assert.equal(await dialog.count(), 1, '100% writing is not a terminal success');
      assert.equal(await page.evaluate(() => flashSession.isBusy()), true);
      await dialog.evaluate(async host => {
        host._installState = { state: 'finished', chipFamily: 'ESP32-C3' }; host._client = undefined;
        host.requestUpdate(); await host.updateComplete;
      });
      await page.waitForTimeout(2200);
      assert.equal(await dialog.count(), 1, 'wrapping up must not close');
      await dialog.evaluate(async host => { host._client = null; host.requestUpdate(); await host.updateComplete; });
      await page.waitForFunction(() => !flashSession.isBusy());
      assert.equal(await dialog.count(), 0, 'successful terminal auto-closes real dialog');
      assert.match(await page.locator('#flash-message').textContent(), /刷写完成.*串口已释放/);
      assert.equal(await page.locator('#ob-success').count(), 1);
      assert.equal(await page.evaluate(() => serialCounts.writes), 0, 'no hardware and no serial writes');
      await page.waitForFunction(() => !document.getElementById('ob-success'), { timeout: 9000 });
      assert.match(await page.locator('#ho-title').textContent(), /蓝牙连接/);
      assert.deepEqual(errors, []);
      console.log(`PASS ${theme}: persisted pause, visible recovery/cancel/denied, user activation, duplicate picker guard, close!=success, 100%/wrapping!=finished, real renderer auto-close/release/handoff; no hardware write`);
      await page.close();
    }
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
