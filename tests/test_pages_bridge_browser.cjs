/* Pages 桥接编辑器真实 DOM 与事务回归；BLE 响应使用替身，不连接硬件。 */
const assert = require('node:assert/strict');
const path = require('node:path');
const { chromium } = require('./web-flash/node_modules/playwright');
const root = path.join(__dirname, '..');
const fixture = {
  schema_ver: 13, device_name: 'BikePower-C3', sport_profile: 0, mode: 1,
  power: 260, cadence: 85, has_user_hr: 0, heartrate: 145, power_step: 20,
  power_float: 50, cadence_float: 8, hr_float: 7, broadcast_mask: 7,
  weight: 70, ftp: 250, thr_hr: 175, ride_type: 2,
  custom_wkg_low: 20, custom_wkg_high: 28, custom_pacer_name: 'Custom Ride',
  run_speed_x100: 1000, run_pace_float_sec: 12, run_cadence: 170,
  run_cadence_float: 5, run_mode: 1, run_pacer_type: 1,
  run_pace_low_sec: 300, run_pace_high_sec: 420, run_custom_name: 'Custom Run',
  led_silent: 0, led_scenes: [
    { mode: 2, color: 2 }, { mode: 1, color: 1 }, { mode: 3, color: 3 },
    { mode: 3, color: 3 }, { mode: 4, color: 0 },
  ],
  bridge_wire: [1, 0, 1, 1, 1, 2, 3, 4, 5, 6, 1, 12, 0, 232, 3, 2, 80, 77],
};
(async () => {
  const browser = await chromium.launch({ headless: true });
  try {
    for (const theme of ['light', 'dark']) {
      const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.route('**/*', route => {
        const url = new URL(route.request().url());
        if (url.hostname !== 'example.test') return route.abort();
        if (url.pathname.endsWith('pages-experience.js') || url.pathname.endsWith('pages-experience.css')) {
          return route.fulfill({ path: path.join(root, 'tools/web', path.basename(url.pathname)),
            contentType: url.pathname.endsWith('.css') ? 'text/css' : 'text/javascript' });
        }
        if (url.pathname.endsWith('flash-session.js')) {
          return route.fulfill({ path: path.join(root, 'tools/web/flash-session.js'), contentType: 'text/javascript' });
        }
        if (url.pathname.endsWith('.json')) return route.fulfill({ json: { schema_version: 1, available: false } });
        return route.fulfill({ path: path.join(root, 'tools/web/ble-monitor.html'), contentType: 'text/html' });
      });
      await page.addInitScript(theme => {
        localStorage.setItem('bikepower.theme.v1', theme);
        sessionStorage.setItem('bikepower.linkgate.skip.v1', '1');
      }, theme);
      await page.goto('https://example.test/');
      await page.evaluate(fixture => {
        document.getElementById('connect-gate').classList.add('hidden');
        document.body.classList.remove('gate-lock');
        const current = { ...fixture, ...parseConfigGroup('bridge', configDataView(new Uint8Array(fixture.bridge_wire))) };
        singleDevice = {
          device: { name: 'BikePower-C3', id: 'fixture' }, server: { connected: true },
          configState: {
            supported: true, saving: false, current,
            meta: { protocol: 1, schema: 13, revision: 1 },
            characters: Object.fromEntries(CONFIG_GROUPS.map(g => [g.key, { key: g.key }])),
          },
        };
        singleDevice.configState.characters.control = { key: 'control', async startNotifications() {} };
        window.writes = [];
        waitForConfigResponse = async () => ({ result: 0, revision: 2, restartFlags: 0 });
        writeConfigValue = async (characteristic, bytes) => window.writes.push({ key: characteristic.key, bytes: Array.from(bytes) });
        const host = document.createElement('div');
        host.id = 'pages-test-host';
        host.style.cssText = 'position:relative;padding:24px;background:var(--bg);z-index:70';
        host.innerHTML = renderConfigEditor(singleDevice);
        document.body.prepend(host);
        bindConfigForm(singleDevice);
      }, fixture);
      assert.equal(await page.locator('#config-dirty').textContent(), '未修改');
      assert.equal(await page.locator('#config-save').isDisabled(), true);
      await page.selectOption('#cfg-cycling-source', '1');
      assert.equal(await page.locator('#cfg-broadcast-hr').isChecked(), false);
      await page.selectOption('#cfg-bridge-calibration', '2');
      await page.fill('#cfg-bridge-percent', '112.3');
      await page.fill('#cfg-bridge-watts', '-25');
      assert.equal(await page.locator('#config-save').isDisabled(), false);
      await page.click('#config-save');
      await page.waitForFunction(() => document.getElementById('config-status').textContent.includes('全部配置已写入'));
      const saved = await page.evaluate(() => ({ writes, current: singleDevice.configState.current }));
      const wire = saved.writes.find(w => w.key === 'bridge').bytes;
      assert.deepEqual(wire.slice(2, 10), fixture.bridge_wire.slice(2, 10));
      assert.deepEqual(wire.slice(15), fixture.bridge_wire.slice(15));
      assert.equal(wire[1], 1);
      assert.equal(wire[10], 2);
      assert.equal(Buffer.from(wire).readInt16LE(11), -25);
      assert.equal(Buffer.from(wire).readInt16LE(13), 1123);
      assert.equal(saved.writes.at(-1).bytes[5], 63);
      assert.equal(await page.locator('#config-dirty').textContent(), '未修改');
      await page.evaluate(() => restoreDeviceConfig());
      assert.equal(await page.inputValue('#cfg-bridge-percent'), '112.3');
      const hintContrast = await page.locator('#cfg-bridge-percent-hint').evaluate(el => {
        const luminance = color => {
          const [r, g, b] = color.match(/[\d.]+/g).slice(0, 3).map(v => {
            const n = Number(v) / 255;
            return n <= .04045 ? n / 12.92 : ((n + .055) / 1.055) ** 2.4;
          });
          return .2126 * r + .7152 * g + .0722 * b;
        };
        const front = luminance(getComputedStyle(el).color);
        const back = luminance(getComputedStyle(el.closest('details')).backgroundColor);
        return (Math.max(front, back) + .05) / (Math.min(front, back) + .05);
      });
      assert.ok(hintContrast >= 4.5, `${theme} 桥接提示对比度 ${hintContrast}`);
      await page.locator('#pages-test-host').screenshot({ path: path.join(root, 'tests/reports/pages-bridge-' + theme + '.png') });
      await page.fill('#cfg-bridge-percent', '150.1');
      await page.click('#config-save');
      assert.equal(await page.locator('#cfg-bridge-percent').getAttribute('aria-invalid'), 'true');
      assert.equal(await page.locator('#config-error-summary').isVisible(), true);
      await page.evaluate(async () => {
        const config = { ...singleDevice.configState.current };
        for (const key of Object.keys(config)) if (key.startsWith('bridge_') || key === 'cycling_source') delete config[key];
        writes.length = 0;
        await runConfigTransaction(singleDevice, config);
      });
      assert.deepEqual(await page.evaluate(() => writes.find(w => w.key === 'bridge').bytes), wire);
      await page.evaluate(() => {
        singleDevice.configState.current.bridge_wire[2] = 0;
        singleDevice.configState.current.bridge_wire[1] = 0;
        Object.assign(singleDevice.configState.current, parseConfigGroup('bridge', configDataView(new Uint8Array(singleDevice.configState.current.bridge_wire))));
        refreshConfigEditor(singleDevice);
      });
      assert.equal(await page.locator('#cfg-cycling-source option[value="1"]').isDisabled(), true);
      await page.locator('#cfg-bridge-percent').focus();
      assert.notEqual(await page.locator('#cfg-bridge-percent').evaluate(el => getComputedStyle(el).outlineStyle), 'none');
      const colors = await page.locator('#cfg-bridge-percent').evaluate(el => {
        const s = getComputedStyle(el);
        return { foreground: s.color, background: s.backgroundColor };
      });
      assert.notEqual(colors.foreground, colors.background);
      await page.evaluate(() => {
        const c = singleDevice.configState.current;
        for (const key of Object.keys(c)) if (key.startsWith('bridge_') || key === 'cycling_source') delete c[key];
        delete singleDevice.configState.characters.bridge;
        singleDevice.configState.meta.schema = 12;
        refreshConfigEditor(singleDevice);
      });
      assert.equal(await page.locator('#cfg-cycling-source').count(), 0);
      await page.fill('#cfg-power', '275');
      await page.click('#config-save');
      await page.waitForFunction(() => document.getElementById('config-status').textContent.includes('全部配置已写入'));
      assert.equal(await page.evaluate(() => writes.at(-1).bytes[5]), 31);
      await page.evaluate(() => document.getElementById('pages-test-host').remove());
      await page.setViewportSize({ width: 390, height: 844 });
      assert.equal(await page.locator('#mobile-guide').isVisible(), true);
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), true);
      assert.deepEqual(errors, []);
      console.log(`[pages-bridge-browser] PASS ${theme} 编辑/六组保存/绑定保留/旧固件/移动引导/焦点`);
      await page.close();
    }
  } finally {
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
