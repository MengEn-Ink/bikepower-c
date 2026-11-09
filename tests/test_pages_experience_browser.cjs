/* 通过真实页面操作验收手机教学、时间信息、主题和工具导航。 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('./web-flash/node_modules/playwright');
const root = path.join(__dirname, '../tools/web');
const screenshots = path.join(__dirname, 'reports/pages-experience');
fs.mkdirSync(screenshots, { recursive: true });
(async () => {
  const browser = await chromium.launch({ headless: true });
  try {
    for (const theme of ['light', 'dark']) {
      for (const width of [360, 390, 768, 900, 1280, 1600]) {
        const page = await browser.newPage({ viewport: { width, height: 900 }, colorScheme: theme });
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.route('**/*', route => {
          const url = new URL(route.request().url());
          if (url.hostname !== 'example.test') return route.abort();
          if (url.pathname.endsWith('site-info.json')) return route.fulfill({
            json: { built_at: '2026-11-04T10:56:24Z', commit: 'a'.repeat(40), dirty: false }
          });
          if (url.pathname.endsWith('.json')) return route.fulfill({ json: { available: false } });
          const file = url.pathname === '/' ? 'ble-monitor.html' : path.basename(url.pathname);
          if (!['ble-monitor.html', 'operation-demos.js', 'operation-demos.css', 'phone-operation-demos.js', 'phone-operation-demos.css', 'phone-portal-source.js', 'pages-experience.js', 'pages-experience.css', 'flash-session.js', 'flash-dialog.js'].includes(file)) return route.fulfill({ status: 404 });
          return route.fulfill({ path: path.join(root, file), contentType: file.endsWith('.css') ? 'text/css' : file.endsWith('.js') ? 'text/javascript' : 'text/html' });
        });
        await page.addInitScript(theme => {
          if (window !== window.top) return;
          localStorage.setItem('bikepower.theme.v1', theme);
          sessionStorage.setItem('bikepower.linkgate.skip.v1', '1');
        }, theme);
        await page.goto('https://example.test/');
        const release = page.locator(width < 900 ? '.mg-foot [data-page-release]' : '.page-release [data-page-release]');
        await release.getByText(/18:56/).waitFor();
        assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), '页面不得横向溢出 ' + width);
        if (width < 900) {
          await page.getByRole('link', { name: '按钮演示', exact: true }).click();
          const demo = page.locator('.mobile-guide [data-button-demo]');
          await demo.getByRole('button', { name: '下一步', exact: true }).click();
          assert.equal(await demo.locator('[data-demo-time]').textContent(), '0.5 s');
          await demo.getByRole('button', { name: '下一步', exact: true }).click();
          await demo.getByRole('button', { name: '下一步', exact: true }).click();
          assert.equal(await demo.getAttribute('data-state'), 'confirm');
          assert.equal(await demo.locator('[data-demo-power]').textContent(), '200');
          await demo.getByRole('button', { name: '下一步', exact: true }).click();
          assert.equal(await demo.getAttribute('data-state'), 'wifi');
          await demo.getByRole('button', { name: '不确认，自动取消', exact: true }).click();
          for (let i = 0; i < 4; i++) await demo.getByRole('button', { name: '下一步', exact: true }).click();
          assert.equal(await demo.getAttribute('data-state'), 'ble');
          await demo.getByRole('button', { name: '短按减功率', exact: true }).click();
          await demo.getByRole('button', { name: '播放演示', exact: true }).click();
          await page.waitForFunction(() => document.querySelector('[data-demo-power]').textContent === '180');
          await demo.getByRole('button', { name: '暂停', exact: true }).click();
          const time = await demo.locator('[data-demo-time]').textContent();
          assert.equal(await demo.getAttribute('data-playing'), 'false');
          assert.equal(await demo.locator('[data-demo-time]').textContent(), time);
          await page.emulateMedia({ reducedMotion: 'reduce' });
          await demo.getByRole('button', { name: '重置', exact: true }).click();
          await demo.getByRole('button', { name: '逐步演示', exact: true }).click();
          assert.equal(await demo.getAttribute('data-playing'), 'false');
          assert.equal(await demo.locator('[data-demo-power]').textContent(), '180');
          await page.getByRole('link', { name: '项目介绍', exact: true }).click();
          assert.ok(await page.locator('#mg-about-title').isVisible());
          await page.getByRole('link', { name: '使用场景', exact: true }).click();
          assert.equal(await page.locator('.mg-scene-list article').count(), 4);
          assert.equal(await page.locator('.mobile-guide .guide-faq, .mobile-guide [data-operation-guide]').count(), 0);
          assert.equal(await page.locator('#mg-steps-title, #mg-check-title, #mg-faq-title').count(), 0);
          assert.ok(await page.locator('.mg-about').getByText(/不是实际运动测量/).count());
          const mobileContract = await page.locator('.mobile-guide').evaluate(root => {
            const sections = ['.mg-hero', '.mg-about', '[aria-labelledby="mg-button-title"]', '.mg-scenes', '.mg-desk'];
            const tops = sections.map(selector => root.querySelector(selector).getBoundingClientRect().top);
            return {
              ordered: tops.every((top, i) => i === 0 || top > tops[i - 1]),
              links: [...root.querySelectorAll('.guide-index a')].every(link => root.querySelector(link.getAttribute('href'))),
              oldSections: root.querySelectorAll('#mg-steps-title, #mg-wifi-title, #mg-bridge-title, #mg-run-title, #mg-compat-title, #mg-check-title').length,
              demos: root.querySelectorAll('[data-operation-demos]').length
            };
          });
          assert.deepEqual(mobileContract, { ordered: true, links: true, oldSections: 0, demos: 1 });
          await page.locator('#mg-theme-btn').click();
          assert.equal(await page.locator('html').getAttribute('data-theme'), theme === 'light' ? 'dark' : 'light');
          await page.locator('#mg-theme-btn').click();
          if (width === 390) {
            await page.getByRole('link', { name: '按钮演示', exact: true }).click();
            await page.screenshot({ path: path.join(screenshots, 'mobile-' + theme + '.png'), animations: 'disabled' });
          }
        } else {
          const gate = page.locator('#connect-gate');
          if (await gate.isVisible()) await gate.getByRole('button', { name: '稍后连接', exact: true }).click();
          await page.locator('[data-nav-cluster="device"] > summary').click();
          await page.locator('.mode-btn[data-mode="flash"]').click();
          assert.equal(await page.locator('#mode-flash').isVisible(), true);
          assert.equal(await page.locator('.mode-btn[data-mode="flash"]').getAttribute('aria-current'), 'page');
          await page.locator('[data-nav-cluster="diagnostics"] > summary').click();
          await page.locator('.mode-btn[data-mode="health"]').click();
          assert.equal(await page.locator('#mode-health').isVisible(), true);
          assert.equal(await page.locator('.tool-cluster').count(), 3);
          if (width === 1280) await page.screenshot({ path: path.join(screenshots, 'desktop-' + theme + '.png'), animations: 'disabled' });
        }
        assert.deepEqual(errors, []);
        console.log('PASS: ' + theme + ' ' + width + 'px, 更新时间、布局、操作、主题');
        await page.close();
      }
    }
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
