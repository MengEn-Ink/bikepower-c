/* 生产页面诊断与连接交接回归；只替换外部蓝牙传输。 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('./web-flash/node_modules/playwright');
const root = path.join(__dirname, '..');

(async () => {
  const browser = await chromium.launch({ headless: true });
  try {
    const context = await browser.newContext({ acceptDownloads: true, viewport: { width: 1440, height: 1000 } });
    await context.route('**/*', route => {
      const url = new URL(route.request().url());
      if (url.hostname !== 'compatibility.test') return route.abort();
      if (url.pathname.endsWith('.js') || url.pathname.endsWith('.css'))
        return route.fulfill({ path: path.join(root, 'tools/web', path.basename(url.pathname)) });
      if (url.pathname.endsWith('.json')) return route.fulfill({ json: { available: false } });
      return route.fulfill({ path: path.join(root, 'tools/web/ble-monitor.html'), contentType: 'text/html' });
    });
    await context.addInitScript(() => {
      sessionStorage.setItem('bikepower_gate_skipped', '1');
      if(localStorage.getItem('bikepower_autolink')===null)localStorage.setItem('bikepower_autolink', '0');
    });
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.goto('https://compatibility.test/#/compare');
    assert.equal(await page.locator('#compatibility-diagnosis').count(), 1, '生产页面缺少兼容性诊断入口');
    await page.fill('#compat-target', '骑鬥');
    await page.fill('#compat-platform', 'Android');
    await page.fill('#compat-version', '实测版本');
    await page.evaluate(() => {
      const full = short => `0000${short}-0000-1000-8000-00805f9b34fb`;
      window.makeRecord = (id, board) => {
        const device = new EventTarget();
        device.id = id; device.name = board ? '开发板' : '正常设备';
        device.gatt = { connected: true, disconnect() { this.connected = false; } };
        return { device, server: device.gatt, info: { firmware: '候选' },
          services: [
            { uuid: full('1818'), enumeration: 'complete', characteristics: [
              { uuid: full('2a63'), properties: 'notify', raw: null, readStatus: 'not-readable' },
              ...(board ? [] : [{ uuid: full('2a65'), properties: 'read', raw: '08 00 00 00', readStatus: 'ok' }]),
              { uuid: full('2a5d'), properties: 'read', raw: '05', readStatus: 'ok' },
            ] },
            ...(board ? [{ uuid: UUIDS.configService, enumeration: 'complete',
              characteristics: [{ uuid: IDENTITY_PROFILE_UUID, properties: 'read|write', readStatus: 'skipped', raw: null }] }] : []),
          ], gattCoverage: { scope: 'authorized', enumeration: 'complete', allowedServices: [full('1818'), UUIDS.configService] },
          compatibilitySamples: [{ uuid: full('2a63'), at: 1000, raw: '20 00 fa 00 01 00 00 04' },
            { uuid: full('2a63'), at: 2000, raw: '20 00 fa 00 02 00 00 08' }],
          configState: { supported: false } };
      };
      compareDevices = [makeRecord('board', true), makeRecord('reference', false)];
      compareDevices.forEach((r, i) => { r._idx = i; });
      renderCompareDevices(); renderCompareTable();
    });
    await page.selectOption('#compat-board', 'board');
    await page.selectOption('#compat-reference', 'reference');
    await page.check('#compat-reference-works');
    await page.click('#compat-capture');
    await page.click('#compat-analyze');
    assert.match(await page.locator('#compat-results').innerText(), /2a65/i);
    assert.match(await page.locator('#compat-status').innerText(), /待目标端验证/);
    const frozen = await page.evaluate(() => window.BikePowerCompatibilityUI.report().snapshots.board);
    await page.evaluate(() => compareDevices[0].services[0].characteristics.push(
      { uuid: '00002a65-0000-1000-8000-00805f9b34fb', properties: 'read', raw: '08 00 00 00', readStatus: 'ok' }));
    await page.click('#compat-analyze');
    assert.deepEqual(await page.evaluate(() => window.BikePowerCompatibilityUI.report().snapshots.board), frozen);
    const downloadPromise = page.waitForEvent('download');
    await page.click('#compat-export');
    const download = await downloadPromise;
    const report = JSON.parse(fs.readFileSync(await download.path(), 'utf8'));
    assert.equal(report.context.target, '骑鬥');
    await page.locator('#compat-import').setInputFiles({ name: 'bad.json', mimeType: 'application/json', buffer: Buffer.from('{"schema":"bad"}') });
    await page.waitForFunction(() => document.getElementById('compat-status').textContent.includes('版本'));
    assert.match(await page.locator('#compat-status').innerText(), /版本/);
    assert.deepEqual(await page.evaluate(() => window.BikePowerCompatibilityUI.report().snapshots.board), frozen);
    await page.locator('#compat-import').setInputFiles({ name: 'report.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(report)) });
    await page.waitForFunction(() => document.getElementById('compat-status').textContent.includes('已导入'));
    assert.match(await page.locator('#compat-status').innerText(), /导入/);
    await page.evaluate(async () => {
      const ch = { uuid: '00002a65-0000-1000-8000-00805f9b34fb', properties: { read: true },
        readValue: async () => { throw new Error('读取被拒绝'); } };
      const services = await enumerateServices({ getPrimaryServices: async () => [
        { uuid: UUIDS.cpsService, getCharacteristics: async () => [ch] },
        { uuid: UUIDS.cscService, getCharacteristics: async () => { throw new Error('没有授权'); } },
      ] });
      window.enumEvidence = { coverage: services.coverage, services };
    });
    const evidence = await page.evaluate(() => enumEvidence);
    assert.equal(evidence.coverage.enumeration, 'partial');
    assert.equal(evidence.services[0].characteristics[0].readStatus, 'failed');
    assert.equal(evidence.services[0].characteristics[0].readError, '读取被拒绝');
    assert.equal(evidence.services[1].enumeration, 'failed');
    const unsafe = structuredClone(report);
    unsafe.snapshots.board.name = '<img src=x onerror="window.injected=true">';
    unsafe.snapshots.board.services[0].characteristics[0].readError = '<svg onload="window.injected=true">';
    await page.locator('#compat-import').setInputFiles({ name: 'unsafe.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(unsafe)) });
    await page.waitForFunction(() => window.BikePowerCompatibilityUI.report().snapshots.board.name.includes('<img'));
    assert.equal(await page.locator('#compat-results img, #compat-results svg, #compat-snapshots img').count(), 0);
    assert.equal(await page.evaluate(() => window.injected), undefined);
    await page.locator('#compat-import').setInputFiles({ name: 'report.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(report)) });
    await page.waitForFunction(() => window.BikePowerCompatibilityUI.report().snapshots.board.name === '开发板');
    for (const theme of ['light', 'dark']) {
      await page.evaluate(theme => document.documentElement.setAttribute('data-theme', theme), theme);
      const colors = await page.locator('#compat-target').evaluate(input => {
        const css = getComputedStyle(input);
        return { text: css.color, background: css.backgroundColor, placeholder: getComputedStyle(input, '::placeholder').color };
      });
      const luminance = color => {
        const rgb = color.match(/\d+/g).slice(0, 3).map(n => Number(n) / 255)
          .map(n => n <= 0.04045 ? n / 12.92 : ((n + 0.055) / 1.055) ** 2.4);
        return rgb[0] * 0.2126 + rgb[1] * 0.7152 + rgb[2] * 0.0722;
      };
      const contrast = (a, b) => (Math.max(luminance(a), luminance(b)) + 0.05) / (Math.min(luminance(a), luminance(b)) + 0.05);
      assert.ok(contrast(colors.text, colors.background) >= 4.5, theme + ' 输入正文对比度不足');
      assert.ok(contrast(colors.placeholder, colors.background) >= 4.5, theme + ' 占位文案对比度不足');
      if (process.env.BP_COMPAT_SCREENSHOT_DIR) {
        await page.locator('#compatibility-diagnosis').scrollIntoViewIfNeeded();
        await page.evaluate(() => window.scrollTo(0, 0));
        await page.screenshot({ path: path.join(process.env.BP_COMPAT_SCREENSHOT_DIR, 'compat-' + theme + '.png') });
      }
    }
    const peer = await context.newPage();
    await peer.goto('https://compatibility.test/#/compare');
    await peer.evaluate(() => {
      const device = new EventTarget();
      device.id = 'peer'; device.name = '其他标签页';
      device.gatt = { connected: true, disconnect() { this.connected = false; } };
      compareDevices = [{ device, server: device.gatt, services: [], info: {} }];
    });
    await page.evaluate(() => {
      const pending = new EventTarget();
      pending.id = 'in-flight'; pending.name = '正在连接的设备';
      pending.gatt = { connected: false, disconnect() { this.connected = false; },
        connect() { return new Promise(resolve => { window.finishPendingConnection = () => { this.connected = true; resolve(this); }; }); } };
      window.pendingDevice = pending;
      window.pendingResult = connectAndCollect(pending).then(() => 'unexpected-success', error => error.message);
    });
    await page.click('#compat-handoff');
    await peer.waitForFunction(() => compatibilityHandoffActive() && !compareDevices[0].server.connected);
    assert.equal(await page.evaluate(() => compareDevices.every(r => !r.server.connected)), true);
    assert.equal(await page.evaluate(() => autoConnect('manual')), false);
    assert.match(await page.locator('#compat-handoff-status').innerText(), /暂停/);
    await page.evaluate(() => finishPendingConnection());
    assert.match(await page.evaluate(() => pendingResult), /暂停/);
    assert.equal(await page.evaluate(() => pendingDevice.gatt.connected), false);
    const refreshed = await context.newPage();
    await refreshed.goto('https://compatibility.test/#/compare');
    assert.equal(await refreshed.evaluate(() => compatibilityHandoffActive()), true, '刷新必须保持暂停');
    assert.equal(await refreshed.evaluate(() => autoConnect('manual')), false);
    await page.selectOption('#compat-result-connection', 'pass');
    await page.fill('#compat-test-notes', '目标端测试记录');
    await page.click('#compat-save-test');
    assert.equal(await page.evaluate(() => window.BikePowerCompatibilityUI.report().test.outcomes.connection), 'pass');
    await page.fill('#compat-version', '新版本');
    assert.equal(await page.evaluate(() => window.BikePowerCompatibilityUI.report().test), null);
    await page.click('#compat-resume');
    await peer.waitForFunction(() => !compatibilityHandoffActive());
    await refreshed.waitForFunction(() => !compatibilityHandoffActive());
    for(const outcome of ['resolve','reject']){
      const connected = await page.evaluate(async outcome => {
        const device=new EventTarget(), pending=[];
        device.id='shared-generation';device.name='代次测试';
        device.gatt={connected:false,disconnect(){this.connected=false;},
          connect(){return new Promise((resolve,reject)=>pending.push({
            resolve:()=>{this.connected=true;resolve(this);},reject:()=>reject(new Error('旧连接失败')),
          }));}};
        const old=connectAndCollect(device).then(()=>false,()=>true);
        setCompatibilityHandoff(true,false);
        setCompatibilityHandoff(false,false);
        const current=connectPageGatt(device,bleConnectionEpoch);
        pending[1].resolve();await current;
        pending[0][outcome]();await old;
        return device.gatt.connected;
      }, outcome);
      assert.equal(connected,true,'旧请求 '+outcome+' 不得清理新连接');
    }
    await page.evaluate(() => localStorage.setItem('bikepower_autolink', '1'));
    await refreshed.reload();
    assert.equal(await refreshed.evaluate(() => autoLinkUserDisconnected), true,
      '结束实测后的手动连接限制必须跨刷新保留');
    assert.equal(await refreshed.evaluate(() => autoConnect('auto')), false);
    const reopened = await context.newPage();
    await reopened.goto('https://compatibility.test/#/compare');
    assert.equal(await reopened.evaluate(() => autoLinkUserDisconnected), true,
      '新标签页也必须等待手动连接');
    await reopened.evaluate(() => allowManualPageConnection());
    await refreshed.waitForFunction(() => !autoLinkUserDisconnected);
    assert.equal(await reopened.evaluate(() => autoLinkUserDisconnected), false);
    await page.setViewportSize({ width: 390, height: 844 });
    assert.equal(await page.locator('#mobile-guide').isVisible(), true);
    assert.match(await page.locator('#mobile-guide').innerText(), /兼容性|诊断/);
    if (process.env.BP_COMPAT_SCREENSHOT_DIR)
      await page.screenshot({ path: path.join(process.env.BP_COMPAT_SCREENSHOT_DIR, 'compat-mobile.png'), fullPage: true });
    assert.deepEqual(errors, []);
    console.log('[ble-compatibility-browser] PASS 固定快照、读取失败、注入防护、双主题、在途连接、跨标签刷新与实测失效');
    await context.close();
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
