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
    let binaryRequests = 0;
    await page.route('**/*', async route => {
      const url = new URL(route.request().url());
      if (url.pathname.includes('/vendor/esp-web-tools-10.4.0/')) {
        const name = path.basename(url.pathname);
        const file = path.join(root, 'tools/web/vendor/esp-web-tools-10.4.0', name);
        return route.fulfill({ path: file, contentType: 'text/javascript', headers: { 'Access-Control-Allow-Origin': '*' } });
      }
      if (url.hostname !== 'example.test') return route.abort();
      if (url.pathname.endsWith('pages-experience.js') || url.pathname.endsWith('pages-experience.css')) {
        return route.fulfill({ path: path.join(root, 'tools/web', path.basename(url.pathname)),
          contentType: url.pathname.endsWith('.css') ? 'text/css' : 'text/javascript' });
      }
      if (url.pathname.endsWith('/ble-connection-help.js')) return route.fulfill({ path: path.join(root, 'tools/web/ble-connection-help.js'), contentType: 'text/javascript' });
      if (url.pathname.endsWith('/flash-dialog.js')) return route.fulfill({ path: path.join(root, 'tools/web/flash-dialog.js'), contentType: 'text/javascript' });
      if (url.pathname.endsWith('/flash-session.js')) return route.fulfill({ path: path.join(root, 'tools/web/flash-session.js'), contentType: 'text/javascript' });
      if (url.pathname.endsWith('/firmware/latest.json')) return route.fulfill({json:{schema_version:1,available:true,tag:'v4.2.1',version:'4.2.1',boards:{'generic-c3':item}}});
      if (url.pathname.endsWith('/firmware/releases.json')) return route.fulfill({json:{schema_version:1,releases:[{tag:'v4.2.1',version:'4.2.1',latest:true,boards:{'generic-c3':item}}]}});
      if (url.pathname.endsWith('/manifest.json')) return route.fulfill({ json: manifest });
      if (url.pathname.endsWith('/bike.bin')) { binaryRequests++; return route.fulfill({ body: bytes }); }
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
    await page.waitForFunction(() => document.getElementById('firmware-status').textContent.includes('可刷写'));
    await page.waitForFunction(() => selectedPreparationStatus().name === 'ready');
    assert.equal(binaryRequests, 1, 'page-open prefetch deduplicates catalog reloads');
    assert.equal(await page.evaluate(() => serialCounts.picks), 0, 'prefetch never requests serial');
    await page.evaluate(async () => { await loadFirmwareCatalog(); });
    assert.equal(await page.locator('#install-unavailable').isVisible(),false,'catalog reload must keep safe launcher available without upstream button');
    assert.equal(await page.locator('#install-button [slot="unsupported"]').isVisible(),false);
    assert.equal(await page.locator('#install-button [slot="not-allowed"]').isVisible(),false);
    await page.evaluate(async ({ item }) => {
      await BikePowerFlash.loadDialog();
      firmwareCatalog = { schema_version: 1, available: true };
      firmwareReleases = [{ tag: 'v4.2.1', version: '4.2.1', latest: true, boards: { 'generic-c3': item } }];
      webInstallerReady = true;
      selectFirmwareRelease(null);
      location.hash = '#flash';
    }, { item });
    await page.setViewportSize({width:704,height:874});
    await page.locator('#mg-flash').click();
    assert.equal(await page.locator('#flash-activate').isVisible(),true);
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),true,'compact flasher must not overflow');
    // Actual page capture handler: chooser cancellation must release busy without touching BLE/device.
    await page.evaluate(() => { window.cancelChooser = true; document.getElementById('flash-activate').click(); });
    await page.waitForFunction(() => !flashSession.isBusy());
    assert.match(await page.locator('#flash-message').textContent(), /已取消/);
    assert.equal(await page.evaluate(() => serialCounts.opens), 0);
    let releaseDownload;
    await page.route('**/firmware/bike.bin', route => new Promise(resolve => { releaseDownload = async () => { await route.fulfill({body:bytes}); resolve(); }; }));
    await page.evaluate(() => { window.cancelChooser = false; firmwarePreparation.clear(); openFlashGuide(); fgGo(3); });
    await page.locator('#flash-activate').click();
    await page.waitForFunction(() => document.getElementById('flash-message').dataset.stage === 'download');
    assert.match(await page.locator('#flash-message').textContent(), /正在下载固件/);
    await page.evaluate(() => { fgGo(3); closeFlashGuide(true); openFlashGuide(); });
    assert.equal(await page.evaluate(() => fgStep), 4, 'reopen and back navigation cannot regress active state');
    assert.equal(await page.locator('#fg-root').getByText('连接串口并继续', { exact: false }).count(), 0);
    await page.evaluate(() => closeFlashGuide(true));
    assert.equal(await page.locator('#flash-cancel').isVisible(), true);
    assert.equal(await page.evaluate(() => serialCounts.opens), 0);
    await page.reload();
    await page.waitForFunction(() => flashSession !== null);
    assert.match(await page.evaluate(() => flashSession.recoveryNotice()), /上次.*中断.*不能确认/);
    assert.equal(await page.evaluate(() => serialCounts.picks), 0);
    await page.evaluate(item => { firmwareCatalog={available:true};firmwareReleases=[{tag:'v4.2.1',version:'4.2.1',latest:true,boards:{'generic-c3':item}}];webInstallerReady=true;selectFirmwareRelease(null);switchMode('flash'); }, item);
    assert.match(await page.locator('#flash-message').textContent(), /上次.*中断/);
    await page.unroute('**/firmware/bike.bin');
    if (releaseDownload) await releaseDownload().catch(() => {});
    // Actual page + real upstream dashboard/erase/confirmation; no device writes before confirmation.
    await page.waitForFunction(() => selectedPreparationStatus().name === 'ready');
    const downloadsBeforeInstall = binaryRequests;
    await page.evaluate(() => { openFlashGuide(); window.cancelChooser = false; window.failInitialize = true; document.getElementById('flash-activate').click(); });
    const dialog = page.locator('ewt-install-dialog');
    await dialog.getByText('安装 BikePower 固件', { exact: true }).waitFor();
    assert.equal(binaryRequests, downloadsBeforeInstall, 'verified prefetch is reused at install');
    await page.evaluate(() => { closeFlashGuide(true); openFlashGuide(); fgGo(3); });
    assert.equal(await page.evaluate(() => fgStep), 4);
    await page.evaluate(() => closeFlashGuide(true));
    await page.setViewportSize({width:704,height:874});
    assert.equal(await dialog.locator('ew-dialog').isVisible(), true, 'active serial dialog cannot be hidden by sidebar resize');
    assert.equal(await page.locator('#mode-flash').isVisible(), true);
    assert.equal(await page.locator('.mobile-guide').isVisible(), false);
    await page.setViewportSize({width:1280,height:900});
    await dialog.getByText('安装 BikePower 固件', { exact: true }).click();
    await dialog.getByText('下一步', { exact: true }).click();
    await dialog.locator('div[slot="headline"]').filter({ hasText: /^确认刷写$/ }).waitFor();
    await page.waitForFunction(() => flashSession.getState().name === 'confirm');
    assert.match(await page.locator('#flash-message').textContent(), /无需|尚未写入|确认刷写/);
    assert.equal(await page.evaluate(() => serialCounts.writes), 0);
    assert.equal(await page.evaluate(() => serialCounts.picks), 1); // after reload: no upstream duplicate chooser
    await dialog.locator('ew-text-button').filter({ hasText: /^\s*确认刷写\s*$/ }).click();
    await dialog.getByText('刷写失败', { exact: true }).waitFor();
    assert.match(await dialog.locator('ewt-page-message').evaluate(el => String(el.label)), /无法连接芯片/);
    assert.equal(await dialog.getAttribute('state'), 'INSTALL');
    assert.equal(await dialog.getByText('正在把 BikePower 固件写入开发板', { exact: true }).count(), 0);
    await dialog.getByText('安装结果以弹窗上方状态为准', { exact: true }).waitFor();
    assert.equal(await dialog.locator('ew-text-button').filter({ hasText: /^\s*确认刷写\s*$/ }).count(), 0); // no automatic retry on ERROR
    assert.equal(await page.evaluate(() => flashSession.isBusy()), true);
    assert.equal(await page.evaluate(() => serialCounts.writes), 0);
    assert.equal(await page.locator('#flash-activate').isEnabled(), false);
    await dialog.getByText('关闭并排障', { exact: true }).click();
    await page.waitForFunction(() => !flashSession.isBusy());
    assert.equal(await page.locator('#ob-success').count(), 0, 'failed session cannot report success on close');
    assert.match(await page.locator('#flash-message').textContent(), /刷写失败.*串口已释放/);
    await page.evaluate(() => openFlashGuide());
    assert.equal(await page.evaluate(() => fgStep), 4, 'failed outcome stays visible rather than resetting to chooser');
    assert.match(await page.locator('#fg-body').textContent(), /刷写失败.*串口已释放/);
    await page.evaluate(() => closeFlashGuide(true));
    assert.deepEqual(errors, []);
    // Recovery fallback: leaving this page destroys the owner; reload must not auto-select/reflash.
    await page.reload();
    await page.waitForFunction(() => flashSession !== null);
    assert.equal(await page.evaluate(() => flashSession.isBusy()), false);
    assert.equal(await page.evaluate(() => serialCounts.picks), 0);
    console.log('PASS: Chromium + real ESP Web Tools 10.4.0: cancellation, explicit erase/install confirmation, initialization ERROR on INSTALL host, neutral helper, fail-closed retry guard, reload without automatic reflash; no hardware');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
