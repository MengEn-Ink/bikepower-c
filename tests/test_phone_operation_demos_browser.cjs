/* 手机页面对齐、实际操作、生命周期及离线接口边界验收。 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { PNG } = require('./web-flash/node_modules/pngjs');
const { chromium } = require('./web-flash/node_modules/playwright');
const root = path.join(__dirname, '..');
const sandbox = { window: {} };
vm.runInNewContext(fs.readFileSync(path.join(root, 'tools/web/phone-portal-source.js'), 'utf8'), sandbox);
const embedded = sandbox.window.BikePowerPhoneSource;
const production = name => fs.readFileSync(path.join(root, 'main/web', name), 'utf8');
assert.ok(embedded.includes(production('app.css')), '屏幕必须使用当前生产 CSS');
assert.ok(embedded.includes(production('app.js')), '屏幕必须使用当前生产 JS');
const adapter = embedded.match(/<script>([\s\S]*?)<\/script>/)[1];
const reference = production('index.html').replace('<!-- INLINE_CSS -->', '<style>' + production('app.css') + '</style>')
  .replace('<!-- INLINE_JS -->', '<script>' + adapter + '</script><script>' + production('app.js') + '</script>');
(async () => {
  const { default: pixelmatch } = await import('./web-flash/node_modules/pixelmatch/index.js');
  const browser = await chromium.launch({ headless: true });
  try {
    const page = await browser.newPage({ viewport: { width: 1280, height: 1800 }, colorScheme: 'light' });
    const errors = [], network = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.route('**/*', route => {
      if (route.request().url().startsWith('file:')) return route.continue();
      network.push(route.request().url()); return route.abort();
    });
    await page.goto('file://' + path.join(root, 'docs/user/pages-mobile-guide.html'));
    const suite = page.locator('[data-operation-demos]');
    await suite.locator('[data-operation-tab="phone"]').click();
    await suite.locator('.phone-demo[data-ready="true"]').waitFor();
    const iframe = suite.locator('iframe');
    const screen = suite.frameLocator('iframe');
    assert.equal(await iframe.getAttribute('sandbox'), 'allow-scripts');
    assert.equal(await screen.locator('body').evaluate(() => innerWidth), 390);
    const original = await browser.newPage({ viewport: { width: 390, height: 782 }, colorScheme: 'light' });
    await original.setContent(reference);
    await original.waitForFunction(() => !document.querySelector('.editor-pane').classList.contains('is-busy'));
    await original.waitForFunction(() => document.getAnimations().every(animation => animation.playState !== 'running'));
    await screen.locator('body').evaluate(async () => {
      await Promise.all(document.getAnimations().map(animation => animation.finished));
    });
    const shape = node => [...node.querySelectorAll('button,input,select,.topbar,.actionbar')].filter(n => n.getClientRects().length)
      .map(n => { const r = n.getBoundingClientRect(); return [n.id, n.textContent.trim(), r.x, r.y, r.width, r.height]; });
    assert.deepEqual(await screen.locator('body').evaluate(shape), await original.locator('body').evaluate(shape),
      '同一视口的控件位置、尺寸及标签与原页面一致');
    const box = await iframe.boundingBox();
    const actualPixels = await page.screenshot({ clip: box, animations: 'disabled', caret: 'hide' });
    const referencePixels = await original.screenshot({ animations: 'disabled', caret: 'hide' });
    fs.writeFileSync('/private/tmp/bikepower-phone-actual.png', actualPixels);
    fs.writeFileSync('/private/tmp/bikepower-phone-reference.png', referencePixels);
    const actualImage = PNG.sync.read(actualPixels), referenceImage = PNG.sync.read(referencePixels);
    assert.equal(actualImage.width, referenceImage.width);
    assert.equal(actualImage.height, referenceImage.height);
    let changed = 0, largest = 0;
    for (let i = 0; i < actualImage.data.length; i++) {
      const difference = Math.abs(actualImage.data[i] - referenceImage.data[i]);
      if (difference) changed++;
      largest = Math.max(largest, difference);
    }
    const differences = pixelmatch(actualImage.data, referenceImage.data, null,
      actualImage.width, actualImage.height, { threshold: .05, includeAA: false });
    assert.ok(differences <= 10, `忽略抗锯齿后仅容许10个像素差异，实际${differences}`);
    await original.close();
    async function next(expected) {
      await suite.locator('[data-phone-next]').click();
      await suite.locator('[data-phone-next]:not(:disabled)').waitFor();
      assert.equal(await suite.locator('[data-phone-count]').textContent(), expected);
    }
    await next('1 / 6');
    const target = await screen.locator('#simPower').evaluate(node => {
      const rect = node.getBoundingClientRect(); return { x: rect.x + rect.width / 2, y: rect.y + rect.height / 2 };
    });
    const tap = await suite.locator('.phone-tap').evaluate(node => ({ x: parseFloat(node.style.left), y: parseFloat(node.style.top) }));
    assert.ok(Math.abs(tap.x - target.x) < .01 && Math.abs(tap.y - target.y) < .01,
      '点击标记与实际输入框中心误差小于 .01 CSS px');
    await next('2 / 6'); await next('3 / 6');
    assert.match(await screen.locator('#toast').textContent(), /草稿已保留/);
    assert.equal(await screen.locator('#heroMetricValue').textContent(), '200');
    await next('4 / 6');
    assert.equal(await screen.locator('#heroMetricValue').textContent(), '260');
    await next('5 / 6'); await next('6 / 6');
    assert.ok(await screen.locator('#wifiClosed').isVisible());
    for (const [name, count] of [['bridge', 8], ['running', 5], ['device', 3], ['cycling', 4]]) {
      await suite.locator(`[data-phone-select="${name}"]`).click();
      await suite.locator('.phone-demo[data-ready="true"]').waitFor();
      for (let step = 1; step <= count; step++) await next(`${step} / ${count}`);
      if (name === 'bridge') {
        assert.equal(await screen.locator('#heroMetricValue').textContent(), '0');
        assert.equal(await screen.locator('#calibrationValue').inputValue(), '20');
      }
      if (name === 'running') assert.match(await screen.locator('#liveTitle').textContent(), /跑步/);
      if (name === 'device') assert.equal(await screen.locator('#headerDeviceName').textContent(), '我的训练伙伴');
      assert.equal(await page.locator('iframe').count(), 1, '整个手册只保留一个手机页面');
    }
    await suite.locator('[data-phone-select="save"]').click();
    await suite.locator('.phone-demo[data-ready="true"]').waitFor();
    await suite.locator('[data-phone-play]').click();
    await suite.locator('[data-phone-count]').filter({ hasText: '1 / 6' }).waitFor();
    await suite.locator('[data-phone-play]').click();
    assert.equal(await suite.locator('.phone-demo').getAttribute('data-playing'), 'false');
    await page.emulateMedia({ reducedMotion: 'reduce' });
    await suite.locator('[data-phone-play]').click();
    await suite.locator('[data-phone-next]:not(:disabled)').waitFor();
    assert.equal(await suite.locator('.phone-demo').getAttribute('data-playing'), 'false');
    await screen.locator('body').evaluate(async () => {
      let blocked = false;
      try { await fetch('https://192.168.4.1/api/config', { method: 'POST' }); } catch (_) { blocked = true; }
      if (!blocked) throw Error('演示未隔离外部请求');
    });
    for (const width of [320, 390, 900, 1280]) {
      await page.setViewportSize({ width, height: 1300 });
      await page.waitForFunction(() => document.documentElement.scrollWidth <= innerWidth + 1);
      assert.equal(await screen.locator('body').evaluate(() => innerWidth), 390, '外壳缩放不重新排列内页');
    }
    assert.deepEqual(errors, []);
    assert.deepEqual(network, [], '离线演示不产生任何网络请求');
    const touch = await browser.newPage({ viewport: { width: 390, height: 844 },
      deviceScaleFactor: 3, isMobile: true, hasTouch: true });
    await touch.goto('file://' + path.join(root, 'design-demos/bikepower-ui/operation-demos/index.html'));
    const touchSuite = touch.locator('[data-operation-demos]');
    await touchSuite.locator('[data-operation-tab="phone"]').tap();
    await touchSuite.locator('.phone-demo[data-ready="true"]').waitFor();
    const touchScreen = touchSuite.frameLocator('iframe');
    await touchScreen.locator('#simPower').fill('280');
    const touchFrame = touchSuite.locator('iframe');
    await touchFrame.scrollIntoViewIfNeeded();
    const physical = await touchFrame.boundingBox();
    const control = await touchScreen.locator('#saveBtn').evaluate(node => {
      const rect = node.getBoundingClientRect();
      return { x: rect.x + rect.width / 2, y: rect.y + rect.height / 2 };
    });
    const scale = physical.width / 390;
    await touch.touchscreen.tap(physical.x + control.x * scale, physical.y + control.y * scale);
    await touchScreen.locator('#toast').filter({ hasText: '配置已保存并应用' }).waitFor();
    await touchScreen.locator('#heroMetricValue').filter({ hasText: '280' }).waitFor();
    assert.equal(await touchScreen.locator('body').evaluate(() => innerWidth), 390);
    await touch.close();
    console.log(`PASS 手机：生产页控件完全对齐，排除抗锯齿后${differences}像素差异（原始${changed}通道/最大${largest}）；真实点击、失败重试、关闭、扫描、0W、跑步、名称、骑行、单实例、暂停、减少动画、离线边界、触屏模拟`);
    await page.close();
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
