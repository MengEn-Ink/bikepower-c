/* 官方平台 Logo、移动首屏与离线资源回归，不连接真实设备。 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('./web-flash/node_modules/playwright');
const root = path.resolve(__dirname, '..');
const screenshots = process.env.BIKEPOWER_PLATFORM_SCREENSHOTS || path.join(__dirname, 'reports/pages-platforms');
const brands = [
  ['Zwift', 'https://www.zwift.com/', 'zwift.svg'],
  ['MyWhoosh', 'https://www.mywhoosh.com/', 'mywhoosh.png'],
  ['顽鹿', 'https://www.onelap.cn/index.html', 'onelap.png'],
  ['骑鬥', 'https://www.weqidou.com/', 'qidou.png']
];
const mime = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.svg': 'image/svg+xml', '.png': 'image/png' };
(async () => {
  fs.mkdirSync(screenshots, { recursive: true });
  const browser = await chromium.launch({ headless: true, executablePath: process.env.BIKEPOWER_CHROMIUM_PATH || undefined });
  try {
    for (const offline of [false, true]) {
      for (const theme of ['light', 'dark']) {
        for (const width of offline ? [320, 390, 768] : [320, 360, 375, 390, 600, 768, 899, 900, 1280]) {
          const page = await browser.newPage({ viewport: { width, height: 844 }, colorScheme: theme, reducedMotion: 'reduce' });
          const errors = [];
          page.on('pageerror', error => errors.push(error.message));
          await page.route('**/*', route => {
            const url = new URL(route.request().url());
            if (url.hostname !== 'platform.test') return route.abort();
            if (url.pathname.endsWith('.json')) return route.fulfill({ json: { available: false } });
            const file = url.pathname === '/' ? path.join(root, 'tools/web/ble-monitor.html') : path.resolve(root, '.' + url.pathname);
            if (!file.startsWith(root + path.sep) || !fs.existsSync(file) || !mime[path.extname(file)]) return route.fulfill({ status: 404 });
            return route.fulfill({ path: file, contentType: mime[path.extname(file)] });
          });
          await page.addInitScript(theme => {
            localStorage.setItem('bikepower.theme.v1', theme);
            sessionStorage.setItem('bikepower.linkgate.skip.v1', '1');
            if (window === window.top) Object.defineProperty(navigator, 'bluetooth', { value: {
              getDevices: async () => [], requestDevice: async () => { throw Error('禁止设备请求'); }
            } });
          }, theme);
          await page.goto(offline ? 'https://platform.test/docs/user/pages-mobile-guide.html' : 'https://platform.test/tools/web/ble-monitor.html');
          if (!offline && width >= 900) await page.evaluate(() => switchMode('manual'));
          const hero = page.locator(offline || width < 900 ? '.mg-hero' : '.manual-hero');
          const support = hero.locator('.platform-support');
          await support.waitFor();
          assert.equal(await support.getByRole('heading', { name: '支持骑行平台' }).count(), 1);
          assert.equal(await support.locator('li').count(), 4);
          for (const [name, href, file] of brands) {
            const link = support.getByRole('link', { name: `${name} 官方网站（新窗口）`, exact: true });
            assert.equal(await link.getAttribute('href'), href);
            assert.equal(await link.getAttribute('rel'), 'noopener noreferrer');
            assert.equal(await link.getAttribute('target'), '_blank');
            assert.ok((await link.locator('img').getAttribute('src')).endsWith('/' + file));
          }
          await page.waitForFunction(selector => [...document.querySelectorAll(selector + ' .platform-list img')].every(img => img.complete && img.naturalWidth > 0), offline || width < 900 ? '.mg-hero' : '.manual-hero');
          const layout = await support.evaluate(node => {
            const links = [...node.querySelectorAll('a')];
            const rects = links.map(link => link.getBoundingClientRect());
            const notes = node.querySelector('.platform-note');
            return {
              firstRow: rects.filter(r => Math.abs(r.top - rects[0].top) < 1).length,
              touch: rects.every(r => r.width >= 44 && r.height >= 44),
              contained: rects.every(r => r.left >= 0 && r.right <= innerWidth),
              text: links.every(link => { const n = link.lastElementChild; return n.scrollWidth <= n.clientWidth + 1; }),
              noteSize: parseFloat(getComputedStyle(notes).fontSize),
              note: notes.textContent,
              bottom: node.getBoundingClientRect().bottom,
              filter: [...node.querySelectorAll('img')].every(img => getComputedStyle(img).filter === 'none')
            };
          });
          assert.equal(layout.firstRow, width < 600 ? 2 : 4);
          assert.equal(layout.touch && layout.contained && layout.text && layout.filter, true);
          assert.ok(layout.noteSize >= 14);
          assert.match(layout.note, /骑鬥同屏心率需独立心率带/);
          assert.match(layout.note, /非官方认证或合作/);
          if (width <= 390) assert.ok(layout.bottom < 700, '平台说明在手机首屏内');
          assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), '页面无横向溢出');
          const first = support.locator('a').first();
          await first.focus();
          assert.equal(await first.evaluate(n => getComputedStyle(n).outlineStyle), 'solid');
          if (!offline && width < 900) {
            await page.locator('#mg-theme-btn').click();
            assert.equal(await page.locator('html').getAttribute('data-theme'), theme === 'dark' ? 'light' : 'dark');
            await page.locator('#mg-theme-btn').click();
            assert.equal(await page.locator('.mobile-guide .guide-faq, .mobile-guide [data-operation-guide]').count(), 0);
          }
          await page.evaluate(() => { document.activeElement.blur(); scrollTo(0, 0); });
          if (!offline && [390, 1280].includes(width)) await page.screenshot({ path: path.join(screenshots, `platforms-${width}-${theme}.png`), animations: 'disabled' });
          assert.deepEqual(errors, []);
          console.log(`PASS platforms: ${offline ? 'offline' : 'online'} ${theme} ${width}px`);
          await page.close();
        }
      }
    }
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
