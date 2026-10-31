/* Real 10.4.0 component + Chromium; only the SerialPort is fake. Never uses hardware. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { createHash } = require('node:crypto');
const { chromium } = require('./web-flash/node_modules/playwright');
const root = path.join(__dirname, '..');
const bytes = Buffer.from('offline browser fixture, not firmware');
const item = { label: 'ESP32-C3', manifest: 'firmware/manifest.json', download: 'firmware/bike.bin', filename: 'bike.bin', size: bytes.length, sha256: createHash('sha256').update(bytes).digest('hex') };
const manifest = { name: 'BikePower', version: '4.2.1', new_install_prompt_erase: true, new_install_improv_wait_time: 0, builds: [{ chipFamily: 'ESP32-C3', parts: [{ offset: 0, path: 'bike.bin' }] }] };
(async () => {
  const browser = await chromium.launch({ headless: true, ...(process.env.BP_BROWSER_CHANNEL ? { channel: process.env.BP_BROWSER_CHANNEL } : {}) });
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.route('**/*', async route => {
      const url = new URL(route.request().url());
      if (url.hostname === 'unpkg.com') {
        const name = path.basename(url.pathname);
        const file = path.join(__dirname, 'web-flash/node_modules/esp-web-tools/dist/web', name);
        return route.fulfill({ path: file, contentType: 'text/javascript', headers: { 'Access-Control-Allow-Origin': '*' } });
      }
      if (url.hostname !== 'example.test') return route.abort();
      if (url.pathname.endsWith('/flash-session.js')) return route.fulfill({ path: path.join(root, 'tools/web/flash-session.js'), contentType: 'text/javascript' });
      if (url.pathname.endsWith('/manifest.json')) return route.fulfill({ json: manifest });
      if (url.pathname.endsWith('/bike.bin')) return route.fulfill({ body: bytes });
      if (url.pathname.endsWith('.json')) return route.fulfill({ json: { schema_version: 1, available: false } });
      if (url.pathname === '/bikepower-c/') return route.fulfill({ path: path.join(root, 'tools/web/ble-monitor.html'), contentType: 'text/html' });
      return route.fulfill({ status: 404, body: '' });
    });
    await page.addInitScript(() => {
      window.serialCounts = { picks: 0, opens: 0, closes: 0, writes: 0 };
      const port = {
        readable: null, writable: null,
        getInfo: () => ({ usbVendorId: 0x303a, usbProductId: 0x1001 }),
        async setSignals() {},
        async open() {
          window.serialCounts.opens++;
          if (window.failInitialize && window.serialCounts.opens === 2) throw new Error('injected initialization failure');
          this.readable = new ReadableStream();
          this.writable = new WritableStream({ write() { window.serialCounts.writes++; } });
        },
        async close() { window.serialCounts.closes++; this.readable = null; this.writable = null; },
      };
      const serial = new EventTarget();
      serial.requestPort = async () => {
        window.serialCounts.picks++;
        if (window.cancelChooser) throw new DOMException('cancelled', 'NotFoundError');
        return port;
      };
      Object.defineProperty(navigator, 'serial', { value: serial });
    });
    await page.goto('https://example.test/bikepower-c/');
    await page.waitForFunction(() => document.getElementById('flash-message').textContent.includes('暂无完整板型固件'));
    await page.evaluate(async ({ item }) => {
      await customElements.whenDefined('esp-web-install-button');
      firmwareCatalog = { schema_version: 1, available: true };
      firmwareReleases = [{ tag: 'v4.2.1', version: '4.2.1', latest: true, boards: { 'generic-c3': item } }];
      webInstallerReady = true;
      selectFirmwareRelease(null);
      location.hash = '#flash';
    }, { item });
    // Actual page capture handler: chooser cancellation must release busy without touching BLE/device.
    await page.evaluate(() => { window.cancelChooser = true; document.getElementById('flash-activate').click(); });
    await page.waitForFunction(() => !flashSession.isBusy());
    assert.match(await page.locator('#flash-message').textContent(), /已取消/);
    assert.equal(await page.evaluate(() => serialCounts.opens), 0);
    // Actual page + real upstream dashboard/erase/confirmation; no device writes before confirmation.
    await page.evaluate(() => { window.cancelChooser = false; window.failInitialize = true; document.getElementById('flash-activate').click(); });
    const dialog = page.locator('ewt-install-dialog');
    await dialog.getByText('Install BikePower', { exact: true }).click();
    await dialog.getByText('Next', { exact: true }).click();
    await dialog.getByText('Confirm Installation', { exact: true }).waitFor();
    assert.equal(await page.evaluate(() => serialCounts.writes), 0);
    assert.equal(await page.evaluate(() => serialCounts.picks), 2); // no upstream duplicate chooser
    await dialog.locator('ew-text-button').filter({ hasText: /^\s*Install\s*$/ }).click();
    await dialog.getByText('Installation failed', { exact: true }).waitFor();
    assert.match(await dialog.locator('ewt-page-message').evaluate(el => String(el.label)), /Failed to initialize/);
    assert.equal(await dialog.getAttribute('state'), 'INSTALL');
    assert.equal(await dialog.getByText('正在把 BikePower 固件写入开发板', { exact: true }).count(), 0);
    await dialog.getByText('安装结果以弹窗上方状态为准', { exact: true }).waitFor();
    assert.equal(await dialog.locator('ew-text-button').filter({ hasText: /^\s*Install\s*$/ }).count(), 0); // no automatic retry on ERROR
    assert.equal(await page.evaluate(() => flashSession.isBusy()), true);
    assert.equal(await page.evaluate(() => serialCounts.writes), 0);
    assert.equal(await page.locator('#flash-activate').isEnabled(), false);
    assert.deepEqual(errors, []);
    // Recovery fallback: leaving this page destroys the owner; reload must not auto-select/reflash.
    await page.reload();
    await page.waitForFunction(() => flashSession !== null);
    assert.equal(await page.evaluate(() => flashSession.isBusy()), false);
    assert.equal(await page.evaluate(() => serialCounts.picks), 0);
    console.log('PASS: Chromium + real ESP Web Tools 10.4.0: cancellation, explicit erase/install confirmation, initialization ERROR on INSTALL host, neutral helper, fail-closed retry guard, reload without automatic reflash; no hardware');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
