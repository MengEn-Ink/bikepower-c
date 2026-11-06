/* 对真实教学控件执行操作；无线接口作为拒绝副作用的边界。 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('./web-flash/node_modules/playwright');
const root = path.join(__dirname, '..');
const web = path.join(root, 'tools/web');
(async () => {
  const browser = await chromium.launch({ headless: true });
  try {
    for (const theme of ['light', 'dark']) {
      for (const width of [320, 390, 900, 1280]) {
        const page = await browser.newPage({ viewport: { width, height: 960 }, colorScheme: theme });
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.route('**/*', route => {
          const url = new URL(route.request().url());
          if (url.hostname !== 'example.test') return route.abort();
          if (url.pathname.endsWith('.json')) return route.fulfill({ json: { available: false } });
          const file = url.pathname === '/' ? 'ble-monitor.html' : path.basename(url.pathname);
          const source = path.join(web, file);
          if (!fs.existsSync(source) || !/\.(?:html|css|js)$/.test(file)) return route.fulfill({ status: 404 });
          return route.fulfill({ path: source, contentType: file.endsWith('.js') ? 'text/javascript' : file.endsWith('.css') ? 'text/css' : 'text/html' });
        });
        await page.addInitScript(theme => {
          if (window !== window.top) return;
          localStorage.setItem('bikepower.theme.v1', theme);
          window.demoDeviceRequests = 0;
          Object.defineProperty(navigator, 'bluetooth', { configurable: true, value: {
            getDevices: async () => [], requestDevice: async () => { window.demoDeviceRequests++; throw Error('教学禁止请求设备'); }
          } });
          Object.defineProperty(navigator, 'serial', { configurable: true, value: {
            requestPort: async () => { window.demoDeviceRequests++; throw Error('教学禁止请求串口'); }
          } });
        }, theme);
        await page.goto('https://example.test/');
        const scope = page.locator(width < 900 ? '.mobile-guide' : '#mode-manual');
        await scope.locator('[data-operation-tabs], .operation-tabs').waitFor();
        assert.equal(await page.locator('#connect-gate').isVisible(), false, '首次手册不弹出连接门');
        if (width >= 900) await scope.locator('.task-options a').nth(1).click();
        const suite = scope.locator('[data-operation-demos]');
        const demo = suite.locator('[data-button-demo]');
        await demo.getByRole('button', { name: '下一步', exact: true }).click();
        await demo.getByRole('button', { name: '下一步', exact: true }).click();
        await demo.getByRole('button', { name: '下一步', exact: true }).click();
        assert.equal(await demo.getAttribute('data-state'), 'confirm');
        assert.match(await demo.locator('[data-demo-countdown]').textContent(), /3\.0 s/);
        const led = await demo.getAttribute('data-led-on');
        assert.equal(await demo.getAttribute('data-playing'), 'false');
        await demo.getByRole('button', { name: '下一步', exact: true }).click();
        assert.equal(await demo.getAttribute('data-state'), 'wifi');
        assert.equal(await demo.locator('[data-demo-power]').textContent(), '200');
        if (width === 390 || width === 1280) {
          await suite.screenshot({ path: '/tmp/bikepower-operation-' + width + '-' + theme + '.png' });
        }
        await demo.getByRole('button', { name: '重置', exact: true }).click();
        await demo.getByRole('button', { name: '播放演示', exact: true }).click();
        await page.waitForFunction(selector => document.querySelector(selector).dataset.playing === 'true', (width < 900 ? '.mobile-guide' : '#mode-manual') + ' [data-button-demo]');
        await suite.locator('[data-operation-tab="phone"]').click();
        assert.equal(await demo.getAttribute('data-playing'), 'false', '切换场景暂停隐藏演示');
        await suite.locator('.phone-demo[data-ready="true"]').waitFor();
        const screen = suite.frameLocator('iframe');
        await suite.locator('[data-phone-next]').click();
        await screen.locator('#simPower').waitFor();
        await page.waitForFunction(selector => document.querySelector(selector).textContent === '1 / 6',
          (width < 900 ? '.mobile-guide' : '#mode-manual') + ' [data-phone-count]');
        assert.equal(await screen.locator('#simPower').inputValue(), '260');
        assert.equal(await screen.locator('#heroMetricValue').textContent(), '200');
        for (let i = 0; i < 2; i++) {
          await suite.locator('[data-phone-next]').click();
          await suite.locator('[data-phone-next]:not(:disabled)').waitFor();
        }
        assert.match(await screen.locator('#toast').textContent(), /草稿已保留/);
        assert.equal(await screen.locator('#simPower').inputValue(), '260');
        await suite.locator('[data-phone-next]').click();
        await suite.locator('[data-phone-next]:not(:disabled)').waitFor();
        assert.equal(await screen.locator('#heroMetricValue').textContent(), '260');
        await page.emulateMedia({ reducedMotion: 'reduce' });
        await suite.locator('[data-phone-play]').click();
        await suite.locator('[data-phone-next]:not(:disabled)').waitFor();
        assert.equal(await suite.locator('.phone-demo').getAttribute('data-playing'), 'false');
        assert.equal(await page.evaluate(() => window.demoDeviceRequests), 0, '教学无无线或串口请求');
        assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), '页面横向溢出');
        assert.deepEqual(errors, []);
        assert.doesNotMatch(await scope.innerText(), /合宙|ESP32|CORE-ESP/);
        const stepGuide = width < 900 ? scope.locator('[data-operation-guide="hotspot"]') : scope.locator('#ch04');
        if (width >= 900) await stepGuide.locator('summary').click();
        const guide = stepGuide.locator('.guide-animation');
        await guide.locator('[data-guide-next]').click();
        assert.match(await guide.locator('[data-guide-caption]').textContent(), /12345678/);
        await guide.locator('[data-guide-next]').click();
        assert.match(await guide.locator('[data-guide-stage]').textContent(), /192\.168\.4\.1/);
        assert.equal(await guide.getAttribute('data-playing'), 'false');
        await page.emulateMedia({ reducedMotion: 'no-preference' });
        await guide.locator('[data-guide-play]').click();
        assert.equal(await guide.getAttribute('data-playing'), 'true');
        if (width >= 900) await stepGuide.locator('summary').click();
        else await suite.scrollIntoViewIfNeeded();
        await page.waitForFunction(selector => document.querySelector(selector).dataset.playing === 'false',
          (width < 900 ? '.mobile-guide [data-operation-guide="hotspot"]' : '#ch04') + ' .guide-animation');
        await suite.locator('[data-operation-tab="button"]').click();
        await demo.locator('[data-demo-reset]').click();
        const glow = await demo.locator('[data-led-halo]').evaluate(node => ({
          radius: Number(node.getAttribute('r')), opacity: getComputedStyle(node).opacity
        }));
        assert.ok(glow.radius >= 70);
        assert.equal(glow.opacity, '1');
        assert.ok(await demo.locator('.demo-light-field').evaluate(node => node.getBoundingClientRect().width >= 300));
        if (width === 390 || width === 1280) {
          await suite.screenshot({ path: '/tmp/bikepower-operation-final-' + width + '-' + theme + '.png' });
        }
        console.log(`PASS 操作演示：${theme} ${width}px，确认/保存/0W/交接/减少动画/无设备请求`);
        if (width === 1280 && theme === 'light') {
          await suite.locator('[data-operation-tab="button"]').click();
          await suite.locator('.practice summary').click();
          const key = suite.locator('[data-practice-key]');
          await key.focus();
          await page.keyboard.down('Space');
          await page.waitForFunction(() => document.querySelector('#mode-manual [data-practice-key]').dataset.ready === 'true');
          await page.keyboard.up('Space');
          assert.match(await suite.locator('[data-practice-output]').textContent(), /200 W.*等待确认/);
          await page.keyboard.press('Enter');
          assert.match(await suite.locator('[data-practice-output]').textContent(), /WiFi 配网/);
          await suite.locator('[data-practice-reset]').click();
          await key.focus();
          await page.keyboard.down('Space');
          await page.waitForFunction(() => document.querySelector('#mode-manual [data-practice-key]').dataset.ready === 'true');
          await page.keyboard.up('Space');
          await page.waitForFunction(() => document.querySelector('#mode-manual [data-practice-output]').textContent.includes('蓝牙工作'));
          assert.match(await suite.locator('[data-practice-output]').textContent(), /200 W/);
          await key.focus();
          await page.keyboard.down('Space');
          await page.keyboard.press('Tab');
          await page.keyboard.up('Space');
          assert.equal(await key.getAttribute('aria-pressed'), 'false');
          console.log('PASS 键盘真实持按、二次确认、超时回滚与焦点移出取消');
        }
        assert.ok(led === 'false' || led === 'true');
        await page.close();
      }
    }
    const offline = await browser.newPage({ viewport: { width: 390, height: 900 } });
    const offlineErrors = [];
    offline.on('pageerror', error => offlineErrors.push(error.message));
    await offline.route('**/*', route => route.request().url().startsWith('file:') ? route.continue() : route.abort());
    await offline.goto('file://' + path.join(root, 'docs/user/pages-mobile-guide.html'));
    await offline.locator('[data-operation-tab="phone"]').click();
    await offline.locator('.phone-demo[data-ready="true"]').waitFor();
    await offline.locator('[data-operation-demos] [data-phone-next]').click();
    await offline.locator('[data-operation-demos] [data-phone-next]:not(:disabled)').waitFor();
    assert.equal(await offline.frameLocator('iframe').locator('#simPower').inputValue(), '260');
    assert.deepEqual(offlineErrors, []);
    console.log('PASS 离线手册 file:// 无网络演示');
    await offline.close();
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
