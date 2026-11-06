/* Pages 三分区导航和统一版式回归，不触发真实设备请求。 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('./web-flash/node_modules/playwright');
const web = path.join(__dirname, '../tools/web');
const screenshots = path.join(__dirname, '../design-demos/bikepower-ui/screenshots');
const routes = [['manual', 'guide'], ['flash', 'device'], ['connect', 'device'],
  ['compare', 'device'], ['device-status', 'diagnostics'], ['inspector', 'diagnostics'], ['health', 'diagnostics']];
(async () => {
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BIKEPOWER_CHROMIUM_PATH || undefined });
  try {
    for (const theme of ['light', 'dark']) {
      for (const width of [900, 1280, 1600]) {
        const page = await browser.newPage({ viewport: { width, height: 1000 }, colorScheme: theme });
        const errors = [];
        page.on('pageerror', e => errors.push(e.message));
        await page.route('**/*', route => {
          const url = new URL(route.request().url());
          if (url.hostname !== 'layout.test') return route.abort();
          if (url.pathname.endsWith('.json')) return route.fulfill({ json: { available: false } });
          const name = url.pathname === '/' ? 'ble-monitor.html' : path.basename(url.pathname);
          const source = path.join(web, name);
          if (!fs.existsSync(source) || !/\.(html|css|js)$/.test(name)) return route.fulfill({ status: 404 });
          return route.fulfill({ path: source, contentType: name.endsWith('.css') ? 'text/css' : name.endsWith('.js') ? 'text/javascript' : 'text/html' });
        });
        await page.addInitScript(theme => {
          localStorage.setItem('bikepower.theme.v1', theme);
          Object.defineProperty(navigator, 'bluetooth', { value: { getDevices: async () => [], requestDevice: async () => { throw Error('禁止设备请求'); } } });
        }, theme);
        await page.goto('https://layout.test/');
        await page.locator('#operations [data-button-demo]').waitFor();
        const order = await page.evaluate(() => {
          const get = s => document.querySelector(s).getBoundingClientRect().top;
          return [get('.manual-hero'), get('#operations'), get('.home-tasks'), get('.home-directory')];
        });
        assert.ok(order.every((n, i) => i === 0 || n > order[i - 1]), '首页先介绍和演示，后任务与目录');
        assert.equal(await page.locator('.home-directory').getAttribute('open'), null);
        const overlap = await page.evaluate(() => {
          const a = document.querySelector('.mode-select').getBoundingClientRect();
          const b = document.querySelector('.masthead-right').getBoundingClientRect();
          return a.right > b.left + 1 && a.top < b.bottom && b.top < a.bottom;
        });
        assert.equal(overlap, false, '导航不遮挡常驻指标与连接入口');
        assert.ok(await page.locator('.help-label').evaluate(n => n.getBoundingClientRect().height < 30), '帮助文字不可挤成两行');
        for (const [mode, cluster] of routes) {
          await page.locator(`[data-nav-cluster="${cluster}"] > summary`).click();
          await page.locator(`.mode-btn[data-mode="${mode}"]`).click();
          assert.equal(await page.locator(`#mode-${mode}`).isVisible(), true);
          assert.equal(await page.locator(`.mode-btn[data-mode="${mode}"]`).getAttribute('aria-current'), 'page');
          assert.equal(await page.locator(`[data-nav-cluster="${cluster}"]`).evaluate(n => n.classList.contains('is-active')), true);
          assert.equal(await page.locator('.tool-cluster[open]').count(), 0, '选中后菜单关闭');
          assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), '全站无横向溢出 ' + width + ' ' + mode);
          if (width === 1280) await page.screenshot({ path: path.join(screenshots, `pages-layout-${mode}-${theme}.png`), animations: 'disabled' });
        }
        await page.locator('[data-nav-cluster="device"] > summary').click();
        await page.keyboard.press('Escape');
        assert.equal(await page.locator('.tool-cluster[open]').count(), 0);
        assert.equal(await page.locator('[data-nav-cluster="device"] > summary').evaluate(n => n === document.activeElement), true);
        await page.locator('[data-nav-cluster="diagnostics"] > summary').click();
        await page.locator('#mode-health h2').click();
        assert.equal(await page.locator('.tool-cluster[open]').count(), 0);
        assert.deepEqual(errors, []);
        console.log(`PASS layout: ${theme} ${width}px, 七路由、菜单、首屏顺序、无溢出`);
        await page.close();
      }
    }
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
