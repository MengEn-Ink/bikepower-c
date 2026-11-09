/* 真实配置 DOM 的档位、整数计算、FTP 草稿反馈及保存事务回归。 */
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
    const page = await browser.newPage({viewport:{width:390,height:844}, reducedMotion:'reduce'});
    const errors = [], configWrites = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('request', request => {
      if (request.url().endsWith('/api/config')) configWrites.push(request.postDataJSON());
    });
    await page.goto(url);
    await page.waitForFunction(() => document.querySelector('#heroMetricValue').textContent === '220');
    assert.equal(await page.locator('#profileWeight').evaluate(node => !!node.closest('.topbar')), true, '体重应固定在页面头部');
    for (const id of ['profileWeight','ftpValue','thresholdHeart']) {
      assert.equal(await page.locator('#' + id).evaluate(node => !!node.closest('#profileBar')), true);
      assert.equal(await page.locator('#' + id).count(),1, '个人字段不重复');
    }
    assert.equal(await page.locator('select#robotPreset').count(),0, '删除骑行强度下拉框');
    assert.equal(await page.locator('#robotPreset').getAttribute('type'),'range');
    await page.locator('#sourceOptions summary').click();
    await page.locator('#heartOptions summary').click();
    await page.locator('[data-sim-mode="pacer"]').click();
    assert.match(await page.locator('#robotPresetLabel').textContent(), /Coco.*2\.4–2\.8 W\/kg/);
    assert.equal(await page.locator('[data-ride]').count(),6);
    assert.equal(await page.locator('#robotPowerRange').textContent(), '168–196 W');
    assert.match(await page.locator('#robotFtpRange').textContent(), /56–65% FTP/);
    assert.match(await page.locator('#robotFtpReference').textContent(), /300 W/);
    assert.match(await page.locator('#robotFtpRelation').textContent(), /低于 FTP/);
    assert(await page.locator('#powerFloat').isHidden());
    assert(await page.locator('#cadenceFloat').isHidden());
    assert.equal(await page.locator('#heroMetricValue').textContent(), '220');
    await page.locator('#robotPreset').focus();
    await page.keyboard.press('Home');
    await page.keyboard.press('ArrowRight');
    assert.match(await page.locator('#robotPreset').getAttribute('aria-valuetext'), /Yumi.*2\.7–3\.1 W\/kg/);
    await page.locator('#profileWeight').fill('75');
    assert.equal(await page.locator('#robotPowerRange').textContent(), '202–232 W', '75 kg Yumi 使用固件整数截断');
    await page.locator('#ftpValue').fill('220');
    assert.match(await page.locator('#robotFtpRange').textContent(), /92–105% FTP/);
    assert.match(await page.locator('#robotFtpRelation').textContent(), /跨越 FTP/);
    await page.locator('#ftpValue').fill('200');
    assert.match(await page.locator('#robotFtpRelation').textContent(), /高于 FTP/);
    await page.locator('#robotPreset').focus();
    await page.keyboard.press('End');
    assert(await page.locator('#customRobotFields').isVisible());
    assert.equal(await page.locator('[data-ride="custom"]').getAttribute('aria-pressed'),'true');
    await page.locator('#wkgMin').fill('2.3');
    await page.locator('#wkgMax').fill('2.3');
    await page.locator('#profileWeight').fill('70');
    assert.equal(await page.locator('#robotPowerRange').textContent(), '161–161 W', '十倍整数运算避免浮点截断少一瓦');
    await page.locator('#ftpValue').fill('161');
    assert.match(await page.locator('#robotFtpRelation').textContent(), /达到 FTP/);
    await page.locator('#wkgMin').fill('1');
    await page.locator('#wkgMax').fill('7');
    await page.locator('#profileWeight').fill('150');
    await page.locator('#ftpValue').fill('50');
    assert.equal(await page.locator('#robotPowerRange').textContent(), '150–1050 W');
    assert.match(await page.locator('#robotFtpRange').textContent(), /300–2100% FTP/);
    assert.match(await page.locator('#robotFtpRelation').textContent(), /图示.*150%/);
    assert(await page.locator('#robotFtpTrack').isVisible());
    for (const [id,value] of [['profileWeight',''],['profileWeight','70.5'],['profileWeight','151'],
      ['wkgMin','0.9'],['wkgMin','2.35'],['wkgMin','7']]) {
      await page.locator('#profileWeight').fill('70');
      await page.locator('#wkgMin').fill('2');
      await page.locator('#wkgMax').fill('3');
      await page.locator('#' + id).fill(value);
      assert.equal(await page.locator('#robotPowerRange').textContent(), '—', `${id} ${value} 无效预览撤销`);
      assert(await page.locator('#robotFtpTrack').isHidden());
      assert(await page.locator('#robotRangeError').isVisible());
      assert(await page.locator('#saveBtn').isDisabled());
      assert.equal(await page.locator('#' + id).getAttribute('aria-invalid'), 'true');
    }
    await page.locator('#profileWeight').fill('70');
    await page.locator('#wkgMin').fill('2');
    for (const value of ['', '0','501','200.5']) {
      await page.locator('#ftpValue').fill(value);
      assert.equal(await page.locator('#robotPowerRange').textContent(), '140–210 W');
      assert.equal(await page.locator('#robotFtpRange').textContent(), '—');
      assert(await page.locator('#robotFtpTrack').isHidden());
      assert.match(await page.locator('#robotFtpRelation').textContent(), /50–500 W/);
      assert(await page.locator('#saveBtn').isDisabled());
      assert(!/NaN|Infinity/.test(await page.locator('#robotFields').textContent()));
    }
    await page.locator('#ftpValue').fill('300');
    assert.equal(await page.locator('#ftpValue').getAttribute('aria-invalid'), 'false');
    await page.locator('[data-sim-mode="fixed"]').click();
    assert.equal(await page.locator('#powerFloat').inputValue(), '50');
    assert.equal(await page.locator('#cadenceFloat').inputValue(), '8');
    await page.locator('#powerFloat').fill('35');
    await page.locator('#cadenceFloat').fill('6');
    await page.locator('[data-sim-mode="pacer"]').click();
    await page.locator('[data-sim-mode="fixed"]').click();
    assert.equal(await page.locator('#powerFloat').inputValue(), '35');
    assert.equal(await page.locator('#cadenceFloat').inputValue(), '6');
    await page.locator('[data-sim-mode="pacer"]').click();
    await page.locator('[data-ride="0"]').click();
    await page.locator('#robotPreset').focus();
    await page.keyboard.press('Home');
    const drag = await page.locator('#robotPreset').boundingBox();
    await page.mouse.move(drag.x + 8,drag.y + drag.height / 2);
    await page.mouse.down();
    await page.mouse.move(drag.x + (drag.width - 16) * .8 + 8,drag.y + drag.height / 2,{steps:10});
    await page.mouse.up();
    assert.equal(await page.locator('#robotPreset').inputValue(),'4', '实际拖动选择 Constance');
    assert.match(await page.locator('#robotPresetLabel').textContent(),/Constance.*4\.0–4\.4 W\/kg/);
    await page.locator('[data-ride="0"]').click();
    for (const mode of ['fixed','linked']) {
      await page.locator(`[data-heart-mode="${mode}"]`).click();
      assert(await page.locator('#ftpValue').isVisible());
      assert(await page.locator('#thresholdHeart').isVisible());
    }
    await page.locator('#heartToggle').click();
    assert(await page.locator('#ftpValue').isVisible());
    assert(await page.locator('#thresholdHeart').isVisible());
    await page.locator('#heartToggle').click();
    await page.locator('[data-sport="running"]').click();
    assert(await page.locator('#profileWeight').isVisible());
    await page.locator('[data-sport="cycling"]').click();
    for (const value of ['', '99','221','180.5']) {
      await page.locator('#thresholdHeart').fill(value);
      assert(await page.locator('#saveBtn').isDisabled());
      assert.equal(await page.locator('#thresholdHeart').getAttribute('aria-invalid'),'true');
      assert.match(await page.locator('#saveState').textContent(),/阈值心率/);
    }
    await page.locator('#thresholdHeart').fill('180');
    const screenshotArg = process.argv.indexOf('--screenshots');
    const directory = screenshotArg >= 0 ? path.resolve(process.argv[screenshotArg + 1]) : null;
    if (directory) fs.mkdirSync(directory, {recursive:true});
    for (const width of [320,390,768,1440]) {
      await page.setViewportSize({width,height:844});
      for (const theme of ['light','dark']) {
        if (await page.locator('html').getAttribute('data-theme') !== theme) await page.locator('#themeToggle').click();
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), `${width} ${theme} 横向溢出`);
        await page.evaluate(() => scrollTo(0,document.body.scrollHeight));
        for (const id of ['profileWeight','ftpValue','thresholdHeart']) {
          const box = await page.locator('#' + id).boundingBox();
          const header = await page.locator('.topbar').boundingBox();
          assert(box.y >= 0 && box.y + box.height <= header.y + header.height, `${id} 滚动后仍在顶部`);
          assert(box.height >= 44);
        }
        const track = await page.locator('#robotFtpTrack').boundingBox();
        const band = await page.locator('#robotRangeBand').boundingBox();
        assert(band.x >= track.x - 1 && band.x + band.width <= track.x + track.width + 1);
        const tick = await page.locator('#robotFtpScale span').nth(1).boundingBox();
        assert(Math.abs(tick.x + tick.width / 2 - (track.x + track.width * 2 / 3)) < 1, '100% 标签对齐刻度');
        const contrastErrors = await page.locator('#robotFields').evaluate(root => {
          const luminance = value => value.match(/[\d.]+/g).slice(0,3).map(Number).map(n => {
            n /= 255; return n <= .04045 ? n / 12.92 : ((n + .055) / 1.055) ** 2.4;
          }).reduce((sum,n,i) => sum + n * [.2126,.7152,.0722][i],0);
          return [...root.querySelectorAll('*')].filter(node => node.getClientRects().length &&
            [...node.childNodes].some(child => child.nodeType === 3 && child.textContent.trim())).flatMap(node => {
            let parent = node;
            while (parent && getComputedStyle(parent).backgroundColor === 'rgba(0, 0, 0, 0)') parent = parent.parentElement;
            if (!parent) return [];
            const foreground = luminance(getComputedStyle(node).color), background = luminance(getComputedStyle(parent).backgroundColor);
            const ratio = (Math.max(foreground,background) + .05) / (Math.min(foreground,background) + .05);
            return ratio >= 4.5 ? [] : [[node.id,node.textContent,ratio]];
          });
        });
        assert.deepEqual(contrastErrors, [], `${width} ${theme} 强度反馈文字对比度`);
        const typeScale = await page.evaluate(() => ({
          body:parseFloat(getComputedStyle(document.body).fontSize),
          heading:parseFloat(getComputedStyle(document.querySelector('#editorTitle')).fontSize)
        }));
        assert(typeScale.heading / typeScale.body >= 1.25, '内联后的真实页面标题层级明确');
        assert.equal(await page.locator('#robotRangeBand').evaluate(node => getComputedStyle(node).transitionDuration), '0s');
        if (width <= 920) {
          await page.locator('.diagnostics summary').click();
          const details = await page.locator('.diagnostics').boundingBox();
          const pane = await page.locator('.live-pane').boundingBox();
          const content = await page.locator('.diagnostics dl').boundingBox();
          const editor = await page.locator('.editor-pane').boundingBox();
          assert(details.width >= pane.width - 36, '手机运行详情展开占满摘要宽度');
          assert(content.height > 0 && content.y + content.height <= editor.y, '展开详情与配置区不重叠');
          await page.locator('.diagnostics summary').click();
        }
        if (directory) {
          await page.evaluate(() => scrollTo(0,0));
          await page.screenshot({path:path.join(directory, `${theme}-${width}.png`),fullPage:true});
          if ([390,1440].includes(width)) {
            await page.locator('#robotFields').evaluate(node => scrollTo(0,scrollY + node.getBoundingClientRect().top - document.querySelector('.topbar').getBoundingClientRect().height - 12));
            await page.locator('#robotFields').screenshot({path:path.join(directory, `${theme}-${width}-intensity.png`)});
          }
        }
      }
    }
    assert.deepEqual(configWrites, [], '草稿和强度预览不提交配置');
    let release;
    const hold = new Promise(resolve => { release = resolve; });
    await page.route('**/api/config', async route => { await hold; await route.continue(); });
    await page.locator('#saveBtn').click();
    assert(await page.locator('#saveBtn').isDisabled());
    assert.match(await page.locator('#saveBtn').textContent(), /正在保存/);
    assert.equal(await page.locator('.editor-pane').getAttribute('aria-busy'), 'true');
    for (const id of ['profileWeight','ftpValue','thresholdHeart']) assert(await page.locator('#' + id).isDisabled(), '保存等待中头部输入禁用');
    assert.equal(await page.locator('#heroMetricValue').textContent(), '220');
    release();
    await page.waitForFunction(() => document.querySelector('#toast').textContent === '配置已保存并应用');
    assert.equal(configWrites.length, 1);
    assert.equal(configWrites[0].weight,70);
    assert.equal(configWrites[0].power_float,35);
    assert.equal(configWrites[0].cadence_float,6);
    assert.equal(configWrites[0].thr_hr,180);
    assert.equal(await page.locator('.editor-pane').getAttribute('aria-busy'), 'false');
    const applied = await (await fetch(url + '/api/state')).json();
    assert.equal(applied.config.mode,1);
    await page.unroute('**/api/config');
    await page.locator('#profileWeight').fill('80');
    await page.route('**/api/config', route => route.fulfill({status:400,contentType:'application/json',
      body:JSON.stringify({ok:false,error:'故障注入'})}));
    await page.locator('#saveBtn').click();
    await page.waitForFunction(() => document.querySelector('#saveState').textContent.includes('设备请求失败'));
    assert.equal(await page.locator('#profileWeight').inputValue(), '80');
    assert.equal(await page.locator('#robotPowerRange').textContent(), '192–224 W');
    assert(await page.locator('#saveBtn').isEnabled());
    assert.equal((await (await fetch(url + '/api/state')).json()).config.weight,70);
    await page.emulateMedia({reducedMotion:'no-preference'});
    assert.notEqual(await page.locator('#robotRangeBand').evaluate(node => getComputedStyle(node).transitionDuration), '0s');
    let expired = false;
    await page.route('**/api/runtime', async route => {
      const response = await route.fetch();
      const output = await response.json();
      if (expired) output.remaining_sec = 0;
      await route.fulfill({response,json:output});
    });
    await page.route('**/api/state', route => expired ? route.abort() : route.continue());
    expired = true;
    await page.locator('#wifiClosed').waitFor();
    assert.equal(await page.locator('#wifiClosed').getAttribute('data-confirmed'),'false');
    assert.equal(await page.locator('#profileBar').evaluate(node => node.inert),true, '会话到期顶部输入禁止交互');
    expired = false;
    await page.locator('#sessionRetryBtn').click();
    await page.locator('#wifiClosed').waitFor({state:'hidden'});
    assert.equal(await page.locator('#profileBar').evaluate(node => node.inert),false, '会话恢复重新开放顶部输入');
    assert.equal(await page.locator('#profileWeight').inputValue(),'80', '恢复保留顶部参数草稿');
    for (const id of ['profileWeight','ftpValue','thresholdHeart']) assert(await page.locator('#' + id).isEnabled());
    await page.locator('#closeWifiBtn').click();
    await page.locator('#confirmCloseBtn').click();
    await page.locator('#wifiClosed').waitFor();
    assert.equal(await page.locator('#wifiClosed').getAttribute('data-confirmed'),'true');
    assert.equal(await page.locator('#profileBar').evaluate(node => node.inert),true, '确认关闭后顶部输入禁止交互');
    for (const id of ['profileWeight','ftpValue','thresholdHeart']) assert(await page.locator('#' + id).isDisabled());
    assert.deepEqual(errors, []);
    const offline = await browser.newPage({viewport:{width:390,height:844}, reducedMotion:'reduce',isMobile:true,hasTouch:true});
    const external = [];
    offline.on('pageerror', error => errors.push(error.message));
    offline.on('request', request => {
      if (!request.url().startsWith('file:')) external.push(request.url());
    });
    await offline.goto('file://' + path.join(root,'design-demos/bikepower-ui/configuration-preview/configuration.html'));
    await offline.waitForFunction(() => document.querySelector('#heroMetricValue').textContent === '200');
    await offline.locator('#sourceOptions summary').tap();
    await offline.locator('[data-sim-mode="pacer"]').tap();
    const touchRange = await offline.locator('#robotPreset').boundingBox();
    await offline.locator('#robotPreset').tap({position:{x:8 + (touchRange.width - 16) * .8,y:touchRange.height / 2}});
    assert.equal(await offline.locator('#robotPreset').inputValue(),'4', '触屏点击滑杆选择 Constance');
    await offline.locator('[data-ride="1"]').tap();
    await offline.locator('#profileWeight').fill('75');
    assert.equal(await offline.locator('#robotPowerRange').textContent(),'202–232 W');
    await offline.locator('#ftpValue').fill('220');
    assert.match(await offline.locator('#robotFtpRange').textContent(),/92–105% FTP/);
    await offline.locator('#saveBtn').click();
    await offline.waitForFunction(() => document.querySelector('#toast').textContent === '配置已保存并应用');
    const simulated = await offline.evaluate(async () => (await (await fetch('/api/state')).json()).config);
    assert.equal(simulated.weight,75);
    assert.equal(simulated.ftp,220);
    assert.deepEqual(external, [], '离线反馈与保存不产生外部请求');
    assert.deepEqual(errors, []);
    await offline.close();
    console.log('PASS：顶部三参数/滑杆拖动与键盘触屏/真实档位与整数功率/FTP联动/无效撤销/草稿隔离/双主题四宽/摘要展开/对比度/减少动画/保存忙态与失败保留/关闭及恢复/离线隔离');
  } finally {
    if (browser) await browser.close();
    server.kill();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
