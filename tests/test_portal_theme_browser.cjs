/* 生产配置页主题、持久化与双主题布局浏览器回归。 */
const assert = require('node:assert/strict');
const {spawn} = require('node:child_process');
const net = require('node:net');
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require('./web-flash/node_modules/playwright');
const root = path.resolve(__dirname, '..');

(async () => {
  const socket = net.createServer();
  await new Promise(resolve => socket.listen(0, '127.0.0.1', resolve));
  const port = socket.address().port;
  await new Promise(resolve => socket.close(resolve));
  const server = spawn('python3', ['tests/mock_server.py', '--port', String(port)], {cwd:root, stdio:'ignore'});
  const url = `http://127.0.0.1:${port}`;
  let browser;
  try {
    let ready = false;
    for (let n = 0; n < 100; n++) {
      try { await fetch(url + '/api/state'); ready = true; break; }
      catch (_) { await new Promise(resolve => setTimeout(resolve, 50)); }
    }
    assert(ready, 'mock 接口未就绪');
    browser = await chromium.launch();
    const context = await browser.newContext({colorScheme:'dark', reducedMotion:'reduce', viewport:{width:390, height:844}});
    const page = await context.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    const writes = [];
    page.on('request', request => {
      if (request.method() !== 'GET') writes.push(request.url());
    });
    await page.goto(url);
    await page.waitForFunction(() => document.querySelector('#heroMetricValue').textContent === '220');
    assert.equal(await page.locator('html').getAttribute('data-theme'), 'dark');
    assert(await page.locator('#saveBtn').isDisabled());
    await page.locator('#themeToggle').click();
    assert.equal(await page.locator('html').getAttribute('data-theme'), 'light');
    assert(await page.locator('#saveBtn').isDisabled());
    await page.waitForTimeout(1200);
    assert.deepEqual(writes, [], '主题切换不应写设备或续期');
    await page.reload();
    assert.equal(await page.locator('html').getAttribute('data-theme'), 'light');
    await page.emulateMedia({colorScheme:'light'});
    await page.locator('#themeToggle').focus();
    await page.keyboard.press('Enter');
    assert.equal(await page.locator('html').getAttribute('data-theme'), 'dark');
    await page.reload();
    assert.equal(await page.locator('html').getAttribute('data-theme'), 'dark');
    await page.waitForFunction(() => document.querySelector('#heroMetricValue').textContent === '220');
    await page.locator('#simPower').fill('310');
    await page.locator('#themeToggle').click();
    assert.equal(await page.locator('#simPower').inputValue(), '310');
    assert.equal(await page.locator('#heroMetricValue').textContent(), '220');
    assert(await page.locator('#saveBtn').isEnabled());
    const screenshotArg = process.argv.indexOf('--screenshots');
    const directory = screenshotArg >= 0 ? path.resolve(process.argv[screenshotArg + 1]) : null;
    if (directory) fs.mkdirSync(directory, {recursive:true});
    for (const width of [320,390,768,1440]) {
      await page.setViewportSize({width, height:844});
      for (const theme of ['light','dark']) {
        if (await page.locator('html').getAttribute('data-theme') !== theme) await page.locator('#themeToggle').click();
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${width} ${theme} 横向溢出`);
        assert.equal(await page.locator('#liveStatus').evaluate(node => getComputedStyle(node).color),
          theme === 'dark' ? 'rgb(130, 216, 219)' : 'rgb(0, 106, 117)', '压缩后的状态类保留语义颜色');
        assert.equal(await page.locator('.source-segment button b').first().evaluate(node => getComputedStyle(node).display),
          'block', '压缩后的后代伪类选择器保留分行');
        const contrastErrors = await page.evaluate(() => {
          const rgb = value => value.match(/[\d.]+/g).map(Number);
          const luminance = color => color.slice(0,3).map(value => {
            value /= 255; return value <= .04045 ? value / 12.92 : ((value + .055) / 1.055) ** 2.4;
          }).reduce((sum, value, index) => sum + value * [.2126,.7152,.0722][index], 0);
          return [...document.querySelectorAll('body *')].filter(node =>
            node.getClientRects().length && [...node.childNodes].some(child =>
              child.nodeType === 3 && child.textContent.trim())).flatMap(node => {
            const style = getComputedStyle(node);
            let parent = node, background;
            while (parent) {
              const color = rgb(getComputedStyle(parent).backgroundColor);
              if (color.length === 3 || color[3] === 1) { background = color; break; }
              parent = parent.parentElement;
            }
            if (!background) return [];
            const foreground = luminance(rgb(style.color)), back = luminance(background);
            const ratio = (Math.max(foreground,back) + .05) / (Math.min(foreground,back) + .05);
            const large = parseFloat(style.fontSize) >= 24 ||
              (parseFloat(style.fontSize) >= 18.66 && Number(style.fontWeight) >= 700);
            return ratio + .01 >= (large ? 3 : 4.5) ? [] : [[node.id || node.className, node.textContent.trim(), ratio]];
          });
        });
        assert.deepEqual(contrastErrors, [], `${width} ${theme} 文字对比度`);
        assert(await page.locator('#simPower').evaluate(node => {
          const luminance = value => value.match(/[\d.]+/g).slice(0,3).map(Number).map(n => {
            n /= 255; return n <= .04045 ? n / 12.92 : ((n + .055) / 1.055) ** 2.4;
          }).reduce((sum,n,i) => sum + n * [.2126,.7152,.0722][i],0);
          const style = getComputedStyle(node), border = luminance(style.borderTopColor), back = luminance(style.backgroundColor);
          return (Math.max(border,back) + .05) / (Math.min(border,back) + .05) >= 3;
        }), `${theme} 输入框边界应可辨认`);
        if (width === 390) {
          await page.evaluate(() => scrollTo(0, 0));
          const input = await page.locator('#simPower').boundingBox();
          const footer = await page.locator('.action-bar').boundingBox();
          assert(input.y + input.height <= footer.y, '390 首屏目标功率被保存栏遮挡');
        }
        if (directory) await page.screenshot({path:path.join(directory, `${theme}-${width}.png`), fullPage:true});
      }
    }
    assert.deepEqual(errors, []);
    await context.close();
    for (const blocked of [false,true]) {
      const isolated = await browser.newContext({colorScheme:'dark'});
      await isolated.addInitScript(blocked => {
        if (blocked) Object.defineProperty(window, 'localStorage', {get() { throw new DOMException('blocked', 'SecurityError'); }});
        else localStorage.setItem('bikepower.portal.theme.v1', 'invalid');
      }, blocked);
      const check = await isolated.newPage();
      await check.goto(url);
      assert.equal(await check.locator('html').getAttribute('data-theme'), 'dark');
      await check.emulateMedia({colorScheme:'light'});
      await check.waitForFunction(() => document.documentElement.dataset.theme === 'light');
      await check.locator('#themeToggle').click();
      assert.equal(await check.locator('html').getAttribute('data-theme'), 'dark');
      await check.emulateMedia({colorScheme:'dark'});
      await check.emulateMedia({colorScheme:'light'});
      assert.equal(await check.locator('html').getAttribute('data-theme'), 'dark');
      await isolated.close();
    }
    console.log('PASS：主题初始/系统跟随/持久化/键盘/存储拒绝/草稿隔离/双主题四宽布局');
  } finally {
    if (browser) await browser.close();
    server.kill();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
