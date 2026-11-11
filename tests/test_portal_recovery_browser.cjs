/* 配置草稿撤销、隐藏错误定位与设备写入隔离浏览器回归。 */
const assert = require('node:assert/strict');
const {spawn} = require('node:child_process');
const net = require('node:net');
const path = require('node:path');
const fs = require('node:fs');
const {chromium} = require('./web-flash/node_modules/playwright');
const root = path.resolve(__dirname,'..');

(async () => {
  const socket = net.createServer();
  await new Promise(resolve => socket.listen(0,'127.0.0.1',resolve));
  const port = socket.address().port;
  await new Promise(resolve => socket.close(resolve));
  const server = spawn('python3',['tests/mock_server.py','--port',String(port)],{cwd:root,stdio:'ignore'});
  const url = `http://127.0.0.1:${port}`;
  let browser;
  try {
    let ready = false;
    for (let n=0;n<100;n++) {
      try {await fetch(url+'/api/state');ready=true;break;}
      catch (_) {await new Promise(resolve => setTimeout(resolve,50));}
    }
    assert(ready,'mock 未就绪');
    browser = await chromium.launch();
    const page = await browser.newPage({viewport:{width:390,height:844},reducedMotion:'reduce'});
    const errors=[], writes=[];
    page.on('pageerror',error => errors.push(error.message));
    page.on('request',request => {
      if (request.url().endsWith('/api/config')) writes.push(request.postDataJSON());
    });
    await page.goto(url);
    await page.waitForFunction(() => document.querySelector('#heroMetricValue').textContent==='220');
    assert.equal(await page.locator('#restoreConfigBtn').count(),1,'应提供恢复已应用配置入口');
    assert(await page.locator('#restoreConfigBtn').isDisabled(),'没有草稿时不能恢复');
    assert(await page.locator('#restoreConfigBtn').isHidden(),'没有草稿时恢复入口不占首屏高度');
    async function restore() {
      await page.locator('#restoreConfigBtn').click();
      await page.waitForFunction(() => document.querySelector('#restoreConfigBtn').disabled
        && document.querySelector('#saveState').textContent.includes('已恢复设备已应用配置')
        && document.querySelector('.editor-pane').getAttribute('aria-busy')==='false');
    }
    async function locate(id,label) {
      assert(await page.locator('#saveBtn').isDisabled(),'非法草稿禁止保存');
      assert.match(await page.locator('#saveState').textContent(),new RegExp(label),'首错误应具体说明');
      assert(await page.locator('#locateErrorBtn').isVisible(),'隐藏错误也有定位入口');
      await page.locator('#locateErrorBtn').click();
      assert(await page.locator('#'+id).isVisible(),'定位展开目标字段');
      assert(await page.locator('#'+id).evaluate(node => node===document.activeElement),'定位聚焦目标');
      assert(await page.locator('#'+id).evaluate(node => {
        const box=node.getBoundingClientRect(), top=document.querySelector('.topbar').getBoundingClientRect().bottom;
        return box.top>=top && box.bottom<=document.querySelector('.action-bar').getBoundingClientRect().top;
      }),'定位字段不能被固定头部或保存栏遮挡');
    }
    await page.locator('[data-sport="running"]').click();
    await page.locator('#paceMinutes').fill('');
    await page.locator('[data-sport="cycling"]').click();
    await locate('paceMinutes','配速');
    assert.equal(await page.locator('#paceMinutes').inputValue(),'','定位保留非法草稿');
    assert.equal(await page.locator('[data-sport="cycling"]').getAttribute('aria-pressed'),'true','定位不修改运动选择');
    await page.locator('#paceMinutes').fill('5');
    await page.locator('#paceSeconds').fill('30');
    await page.locator('#simPower').fill('270');
    await page.locator('#saveBtn').click();
    await page.locator('#toast').filter({hasText:'配置已保存并应用'}).waitFor();
    assert.equal(writes.at(-1).sport_profile,0,'修正隐藏跑步草稿后仍保存骑行');
    assert.equal(writes.at(-1).run_speed_x100,1091);
    assert(await page.locator('#runningEditor').isHidden(),'保存后恢复正常展示');

    await page.locator('#heartOptions summary').click();
    await page.locator('[data-heart-mode="fixed"]').click();
    await page.locator('#fixedHeart').fill('');
    await page.locator('[data-heart-mode="linked"]').click();
    await page.locator('#heartOptions summary').click();
    await locate('fixedHeart','心率');
    assert.equal(await page.locator('[data-heart-mode="linked"]').getAttribute('aria-pressed'),'true','定位不改变心率模式');
    await page.locator('#fixedHeart').fill('145');
    await page.locator('#sourceOptions summary').click();
    await page.locator('#powerFloat').fill('101');
    await page.locator('[data-sim-mode="pacer"]').click();
    await page.locator('#sourceOptions summary').click();
    await locate('powerFloat','功率浮动');
    assert.equal(await page.locator('[data-sim-mode="pacer"]').getAttribute('aria-pressed'),'true','定位不改变机器人模式');
    await restore();
    assert.equal(await page.locator('#simPower').inputValue(),'270','恢复最近设备已应用值');
    assert.equal(await page.locator('#paceSeconds').inputValue(),'30');
    assert.equal(await page.locator('[data-sim-mode="fixed"]').getAttribute('aria-pressed'),'true');
    assert(await page.locator('#restoreConfigBtn').isDisabled(),await page.locator('#saveState').textContent());
    assert(await page.locator('#locateErrorBtn').isHidden());
    assert.equal(writes.length,1,'恢复不保存配置');

    const setting=page.locator('details').filter({has:page.locator('#deviceNameInput')});
    await setting.locator('summary').click();
    await page.locator('#deviceNameInput').fill('中文名称中文名称');
    await setting.locator('summary').click();
    await locate('deviceNameInput','名称');
    await page.locator('#deviceNameInput').fill('BikePower');
    await page.locator('#powerStepInput').fill('');
    await setting.locator('summary').click();
    await locate('powerStepInput','步进');
    await restore();
    await page.locator('[data-sport="running"]').click();
    await page.locator('[data-run-mode="pacer"]').click();
    await page.locator('#runPreset').selectOption('custom');
    await page.locator('#runPaceLow').fill('700');
    await page.locator('#runPaceHigh').fill('600');
    await page.locator('[data-run-mode="fixed"]').click();
    await page.locator('[data-sport="cycling"]').click();
    await locate('runPaceLow','配速');
    await restore();

    await page.locator('#simPower').fill('280');
    await page.route('**/api/config',route => route.fulfill({status:400,contentType:'application/json',
      body:JSON.stringify({ok:false,error:'恢复专项故障注入'})}));
    await page.locator('#saveBtn').click();
    await page.waitForFunction(() => document.querySelector('#saveState').textContent.includes('设备请求失败'));
    assert.equal(await page.locator('#simPower').inputValue(),'280','保存失败保留草稿');
    await restore();
    assert.equal(await page.locator('#simPower').inputValue(),'270');
    await page.unroute('**/api/config');
    assert.equal(writes.length,2,'恢复失败保存后的草稿不发新保存');

    let release;
    const held=new Promise(resolve => {release=resolve;});
    await page.route('**/api/config',async route => {await held;await route.continue();});
    await page.locator('#simPower').fill('290');
    await page.locator('#saveBtn').click();
    assert(await page.locator('#restoreConfigBtn').isDisabled(),'保存等待禁止恢复');
    release();
    await page.locator('#toast').filter({hasText:'配置已保存并应用'}).waitFor();
    await page.unroute('**/api/config');
    await page.locator('#simPower').fill('299');
    const snapshot=await (await fetch(url+'/api/state')).json();
    const external=await fetch(url+'/api/config',{method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({...snapshot.config,revision:snapshot.revision,power:295})});
    assert.equal((await external.json()).ok,true);
    await page.waitForFunction(() => document.querySelector('#heroMetricValue').textContent==='295');
    assert.equal(await page.locator('#simPower').inputValue(),'299','外部配置变化保留草稿');
    await restore();
    assert.equal(await page.locator('#simPower').inputValue(),'295','恢复跟随最新设备确认值');
    await page.locator('#sourceOptions summary').click();
    await page.locator('[data-source="bridge"]').click();
    await page.locator('#bridgeStartScanBtn').click();
    await page.locator('#scanResults button').first().click();
    await page.locator('#scanModal').waitFor({state:'hidden'});
    assert.equal(await page.locator('#bridgeDeviceList [data-device]').count(),1);
    await page.locator('#copyIdentity').check();
    await page.locator('#identityNameSource').selectOption('custom');
    await page.locator('#customIdentityName').fill('临时名称');
    const beforeRestore=writes.length;
    await page.route('**/api/bridge/scan',route => route.fulfill({status:503,body:'恢复取消失败'}));
    await page.locator('#restoreConfigBtn').click();
    await page.waitForFunction(() => document.querySelector('#saveState').textContent.includes('503'));
    assert.equal(await page.locator('#bridgeDeviceList [data-device]').count(),1,'取消失败保留草稿设备');
    assert(await page.locator('#copyIdentity').isChecked(),'取消失败保留身份复制草稿');
    await page.unroute('**/api/bridge/scan');
    await restore();
    assert.equal(await page.locator('#bridgeDeviceList [data-device]').count(),0,'恢复清除临时设备草稿');
    assert.equal(await page.locator('[data-source="simulation"]').getAttribute('aria-pressed'),'true');
    assert.equal(await page.locator('#customIdentityName').inputValue(),'');
    assert(!(await page.locator('#copyIdentity').isChecked()));
    assert.equal(writes.length,beforeRestore,'取消测试与恢复不保存配置');
    assert.equal((await (await fetch(url+'/api/state')).json()).bridge.temporary,false,'临时上游已停止');
    const arg=process.argv.indexOf('--screenshots');
    if (arg>=0) fs.mkdirSync(path.resolve(process.argv[arg+1]),{recursive:true});
    for (const width of [320,390,768,1440]) {
      await page.setViewportSize({width,height:844});
      for (const theme of ['light','dark']) {
        if (await page.locator('html').getAttribute('data-theme')!==theme) await page.locator('#themeToggle').click();
        await page.locator('#simPower').fill('-1');
        await locate('simPower','功率');
        assert(await page.evaluate(() => document.documentElement.scrollWidth<=innerWidth),'错误状态无横向溢出');
        if (arg>=0) await page.screenshot({path:path.join(path.resolve(process.argv[arg+1]),`${theme}-${width}-error.png`)});
        await restore();
        assert.equal(await page.locator('#simPower').inputValue(),'295');
      }
    }
    const invalidPage=await browser.newPage({viewport:{width:390,height:844}});
    const invalidState=await (await fetch(url+'/api/state')).json();
    await invalidPage.route('**/api/state',route => route.fulfill({contentType:'application/json',
      body:JSON.stringify({...invalidState,config:{...invalidState.config,broadcast_mask:8}})}));
    const invalidWrites=[];
    invalidPage.on('request',request => {
      if (request.url().endsWith('/api/config')) invalidWrites.push(request.postDataJSON());
    });
    await invalidPage.goto(url);
    await invalidPage.waitForFunction(() => document.querySelector('#saveState').textContent.includes('踏频随功率'));
    assert(await invalidPage.locator('#saveBtn').isDisabled(),'孤立兼容位必须阻止保存');
    await invalidPage.locator('#locateErrorBtn').click();
    assert(await invalidPage.locator('#broadcastOptions summary').evaluate(node => node===document.activeElement));
    assert(await invalidPage.locator('[data-cadence-bundle]').isVisible(),'兼容位错误定位到广播设置');
    assert.deepEqual(invalidWrites,[],'孤立兼容位不得静默归一化保存');
    await invalidPage.close();
    await page.locator('#closeWifiBtn').click();
    await page.locator('#confirmCloseBtn').click();
    await page.locator('#wifiClosed').waitFor();
    assert(await page.locator('#restoreConfigBtn').isDisabled(),'结束后禁止恢复');
    assert.deepEqual(errors,[]);
    console.log('PASS：隐藏错误定位保值/运动来源心率不变/名称字节与关联范围/恢复最新设备值/临时测试取消与取消失败保留/失败保存/忙态/结束禁用/双主题四宽/零额外配置写入');
  } finally {
    if (browser) await browser.close();
    server.kill();
  }
})().catch(error => {console.error(error);process.exitCode=1;});
