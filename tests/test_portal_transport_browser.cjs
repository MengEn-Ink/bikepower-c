/* 配网页网络异常、固定入口、有限只读恢复与丢失保存确认回归。 */
const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const net = require('node:net');
const path = require('node:path');
const { chromium } = require('./web-flash/node_modules/playwright');
const root = path.resolve(__dirname, '..');
(async () => {
  const socket = net.createServer();
  await new Promise(resolve => socket.listen(0, '127.0.0.1', resolve));
  const port = socket.address().port;
  await new Promise(resolve => socket.close(resolve));
  const server = spawn('python3', ['tests/mock_server.py', '--port', String(port)], { cwd: root, stdio: 'ignore' });
  const base = `http://127.0.0.1:${port}`;
  let browser;
  try {
    for (let i = 0; i < 100; i++) {
      try { await fetch(base + '/api/state'); break; } catch { await new Promise(resolve => setTimeout(resolve, 50)); }
    }
    browser = await chromium.launch({ headless: true });
    for (const host of ['connectivitycheck.vivo.com.cn', 'connectivitycheck.gstatic.com', 'captive.apple.com']) {
      const page = await browser.newPage();
      const urls = [];
      await page.route('**/*', async route => {
        const url = new URL(route.request().url());
        urls.push(route.request().url());
        if (url.pathname.startsWith('/api/')) return route.abort();
        if (url.hostname === '192.168.4.1') return route.fulfill({ body: 'canonical device entry', contentType: 'text/html' });
        const response = await route.fetch({ url: base + '/' });
        await route.fulfill({ response });
      });
      await page.goto(`http://${host}/?next=https://invalid.example/`);
      await page.waitForURL('http://192.168.4.1/');
      assert.equal(urls.filter(url => url.includes('/api/')).length, 0, '检测域名不得初始化设备接口');
      await page.close();
      console.log('PASS canonical', host);
    }
    for (const theme of ['light', 'dark']) {
      const context = await browser.newContext({ colorScheme: theme, viewport: { width: 390, height: 844 }, reducedMotion: 'reduce' });
      const page = await context.newPage();
      let fault = '', reads = [], writes = [], lost = false;
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.route('**/api/**', async route => {
        const request = route.request(), name = new URL(request.url()).pathname;
        if (request.method() === 'GET') reads.push({ name, at: Date.now() });
        else writes.push(name);
        if (lost && name === '/api/config' && request.method() === 'POST') {
          await route.fetch(); lost = false; fault = 'network'; return route.abort();
        }
        if (fault === 'network') return route.abort();
        if (fault === 'html') return route.fulfill({ contentType: 'text/html', body: '<h1>other network</h1>' });
        if (fault === '503') return route.fulfill({ status: 503, body: 'unavailable' });
        await route.continue();
      });
      await page.goto(base);
      await page.waitForFunction(() => document.getElementById('heroMetricValue').textContent === '220' || document.getElementById('heroMetricValue').textContent === '315');
      await page.locator('#simPower').fill('315');
      await page.waitForTimeout(1200);
      fault = 'network'; reads = []; const beforeWrites = writes.length;
      await page.waitForFunction(() => !document.getElementById('connectionRecovery').hidden);
      await page.waitForFunction(() => document.getElementById('connectionRecoveryText').textContent.includes('自动重试已暂停'), { timeout: 25000 });
      assert.match(await page.locator('#saveState').textContent(), /连接中断/);
      assert.doesNotMatch(await page.locator('#saveState').textContent(), /Failed to fetch/);
      assert.equal(await page.locator('#heroMetricValue').textContent(), '—');
      assert.match(await page.locator('#liveStatus').textContent(), /连接中断/);
      assert.equal(await page.locator('#simPower').inputValue(), '315');
      assert.equal(await page.locator('#saveBtn').isDisabled(), true);
      await page.locator('#connectionRetryBtn').click();
      await page.waitForFunction(() => !document.getElementById('connectionRetryBtn').disabled);
      const failedManual = reads.length;
      assert.ok(failedManual > 0, '手动失败后按钮可再次操作');
      const beforeWritesAgain = writes.length;
      assert.equal(beforeWritesAgain, beforeWrites);
      fault = '';
      await page.locator('#connectionRetryBtn').click();
      await page.waitForFunction(() => document.getElementById('connectionRecovery').hidden);
      assert.equal(writes.length, beforeWrites);
      fault = 'network'; reads = [];
      await page.waitForFunction(() => !document.getElementById('connectionRecovery').hidden);
      await page.waitForFunction(() => document.getElementById('connectionRecoveryText').textContent.includes('自动重试已暂停'), { timeout: 25000 });
      assert.equal(writes.length, beforeWrites, '自动恢复不得续期或提交');
      const attempts = reads.length;
      assert.ok(attempts >= 4 && attempts <= 6, `探测次数必须有界，实际 ${attempts}`);
      await page.waitForTimeout(2200);
      assert.equal(reads.length, attempts, '停止后不得继续请求');
      fault = '';
      await page.locator('#connectionRetryBtn').click();
      await page.waitForFunction(() => document.getElementById('connectionRecovery').hidden);
      assert.equal(await page.locator('#simPower').inputValue(), '315');
      assert.equal(writes.length, beforeWrites, '手动重新连接也只读');
      assert.equal(await page.locator('#devicePortalLink').getAttribute('href'), 'http://192.168.4.1/');
      assert.equal(await page.locator('#devicePortalLink').getAttribute('rel'), 'noopener noreferrer');
      for (const next of ['503', 'html']) {
        fault = next;
        await page.waitForFunction(() => !document.getElementById('connectionRecovery').hidden);
        assert.doesNotMatch(await page.locator('#saveState').textContent(), /Unexpected token|Failed to fetch/);
        fault = '';
        await page.locator('#connectionRetryBtn').click();
        await page.waitForFunction(() => document.getElementById('connectionRecovery').hidden);
      }
      await page.locator('#simPower').fill('315');
      await page.waitForTimeout(1200);
      lost = true; const savedPosts = writes.filter(name => name === '/api/config').length;
      await page.locator('#saveBtn').click();
      await page.waitForFunction(() => !document.getElementById('connectionRecovery').hidden);
      assert.equal(await page.locator('#simPower').inputValue(), '315');
      fault = '';
      await page.locator('#connectionRetryBtn').click();
      await page.waitForFunction(() => document.getElementById('connectionRecovery').hidden && document.getElementById('heroMetricValue').textContent === '315');
      assert.equal(writes.filter(name => name === '/api/config').length, savedPosts + 1);
      assert.match(await page.locator('#saveState').textContent(), /核实/);
      assert.doesNotMatch(await page.locator('#toast').textContent(), /保存并应用/);
      for (const width of [320, 390, 768, 1440]) {
        await page.setViewportSize({ width, height: 844 });
        fault = 'network';
        await page.waitForFunction(() => !document.getElementById('connectionRecovery').hidden);
        assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth));
        const button = await page.locator('#connectionRetryBtn').boundingBox();
        assert.ok(button.height >= 44 && button.width >= 44);
        if (process.env.BIKEPOWER_RECOVERY_SCREENSHOTS && width === 390) {
          await page.evaluate(() => scrollTo(0, 0));
          await page.screenshot({ path: path.join(process.env.BIKEPOWER_RECOVERY_SCREENSHOTS, `recovery-${theme}.png`) });
        }
        fault = '';
        await page.locator('#connectionRetryBtn').click();
        await page.waitForFunction(() => document.getElementById('connectionRecovery').hidden);
      }
      assert.deepEqual(errors, []);
      await context.close();
      console.log('PASS transport', theme, '有限退避/只读恢复/草稿/失确认不重发/错误分类/四宽');
      // 下一轮保持相同初始功率，隔离主题用例。
      const current = await (await fetch(base + '/api/state')).json();
      await fetch(base + '/api/config', { method: 'POST', body: JSON.stringify({ ...current.config, revision: current.revision, power: 220 }), headers: { 'Content-Type': 'application/json' } });
      await new Promise(resolve => setTimeout(resolve, 150));
    }
    const page = await browser.newPage();
    let offline = true;
    await page.route('**/api/**', route => offline ? route.abort() : route.continue());
    await page.goto(base);
    await page.waitForFunction(() => !document.getElementById('connectionRecovery').hidden);
    assert.equal(await page.locator('#saveBtn').isDisabled(), true);
    offline = false;
    await page.locator('#connectionRetryBtn').click();
    await page.waitForFunction(() => document.getElementById('heroMetricValue').textContent === '220');
    assert.equal(await page.locator('#profileWeight').inputValue(), '70');
    assert.equal(await page.locator('#profileWeight').isDisabled(), false);
    assert.equal(await page.locator('#simPower').inputValue(), '220');
    await page.close();
    console.log('PASS first load failure recover without reload');
  } finally { if (browser) await browser.close(); server.kill(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
