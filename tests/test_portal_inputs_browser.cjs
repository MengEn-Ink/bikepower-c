/* 首次渐进配置、已有配置恢复、五项组合数值与图标主题浏览器回归。 */
const assert = require('node:assert/strict');
const {spawn} = require('node:child_process');
const net = require('node:net');
const fs = require('node:fs');
const path = require('node:path');
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
    for (let n = 0; n < 100; n++) {
      try {await fetch(url + '/api/state');ready=true;break;}
      catch (_) {await new Promise(resolve => setTimeout(resolve,50));}
    }
    assert(ready,'mock 未就绪');
    browser = await chromium.launch();
    const page = await browser.newPage({viewport:{width:390,height:844},reducedMotion:'reduce'});
    const errors = [], writes = [];
    page.on('pageerror',error => errors.push(error.message));
    page.on('request',request => {if (request.url().endsWith('/api/config')) writes.push(request.postDataJSON());});
    await page.goto(url);
    await page.waitForFunction(() => document.querySelector('#heroMetricValue').textContent === '220');
    const firstArg=process.argv.indexOf('--screenshots');
    if (firstArg>=0) {
      const firstDirectory=path.resolve(process.argv[firstArg+1]);
      fs.mkdirSync(firstDirectory,{recursive:true});
      await page.screenshot({path:path.join(firstDirectory,'initial-390.png'),fullPage:true});
      await page.screenshot({path:path.join(firstDirectory,'initial-390-viewport.png')});
    }
    assert.equal(await page.locator('#sourceOptions').count(),1,'首次打开应有按需展开的模式与来源');
    assert.equal(await page.locator('#sourceOptions').getAttribute('open'),null,'默认折叠模式与来源');
    assert.equal(await page.locator('#broadcastOptions').getAttribute('open'),null,'默认折叠广播兼容选项');
    assert.equal(await page.locator('#heartOptions').getAttribute('open'),null,'默认心率随动，按需切换固定心率');
    assert.match(await page.locator('#sourceOptionsSummary').textContent(),/模拟.*固定功率/);
    assert.match(await page.locator('#heartOptionsSummary').textContent(),/功率随动/);
    assert.equal(await page.locator('#heartToggle').getAttribute('aria-checked'),'true');
    for (const id of ['simPower','simCadence','heartToggle']) {
      const box=await page.locator('#'+id).boundingBox(), footer=await page.locator('.action-bar').boundingBox();
      assert(box.y>=0 && box.y+box.height<=footer.y,`${id} 核心配置应在手机首屏可见`);
    }
    assert(await page.locator('[data-source="bridge"]').isHidden());
    assert(await page.locator('[data-sim-mode="pacer"]').isHidden());
    assert(await page.locator('[data-heart-mode="fixed"]').isHidden());
    assert(await page.locator('[data-broadcast="power"]').isHidden());
    await page.locator('#simPower').fill('231');
    await page.locator('#sourceOptions summary').focus();
    await page.keyboard.press('Enter');
    assert(await page.locator('[data-sim-mode="pacer"]').isVisible(),'键盘可展开模式设置');
    await page.locator('#sourceOptions summary').click();
    assert.equal(await page.locator('#simPower').inputValue(),'231','收起保留草稿');
    await page.locator('#simPower').fill('220');
    assert(await page.locator('#saveBtn').isDisabled(),'展开设置不修改配置');
    assert.deepEqual(writes,[],'渐进展开不得自动保存');
    await page.locator('#sourceOptions summary').click();
    await page.locator('#heartOptions summary').click();
    async function checkAlignment() {
      const layout = await page.evaluate(() => [...document.querySelectorAll('.sport-switch,.segmented')]
        .filter(node => node.getClientRects().length).map(node => {
          const box = node.getBoundingClientRect(), style = getComputedStyle(node);
          const buttons = [...node.children].filter(child => child.tagName==='BUTTON');
          const bounds = buttons.map(button => button.getBoundingClientRect());
          return {name:node.getAttribute('aria-label'),room:box.width-parseFloat(style.paddingLeft)
            -parseFloat(style.paddingRight)-parseFloat(style.borderLeftWidth)-parseFloat(style.borderRightWidth),
          used:bounds.at(-1).right-bounds[0].left,widths:bounds.map(bound => bound.width),
          centered:buttons.every(button => getComputedStyle(button).textAlign==='center')};
        }));
      for (const group of layout) {
        assert(Math.abs(group.room-group.used)<1,`${group.name} 按钮应占满栏宽，实际 ${group.used}/${group.room}`);
        assert(Math.max(...group.widths)-Math.min(...group.widths)<1,`${group.name} 每项应等宽`);
        assert(group.centered,`${group.name} 文字居中`);
      }
      assert(await page.evaluate(() => {
        const nav=document.querySelector('.sport-switch').getBoundingClientRect();
        const content=document.querySelector('.page').getBoundingClientRect();
        return Math.abs(nav.x+nav.width/2-content.x-content.width/2)<1;
      }),'骑行/跑步切换整体居中');
      assert(await page.evaluate(() => {
        const editor=document.querySelector('.editor-pane'), workspace=document.querySelector('.workspace');
        if (!workspace.classList.contains('is-mode-pending')) return true;
        return Math.abs(editor.getBoundingClientRect().width
          -Math.min(workspace.getBoundingClientRect().width,parseFloat(editor.style.maxWidth)))<1;
      }),'切换运动后的编辑栏应按可用宽度显示，不随内容缩窄');
    }
    await checkAlignment();
    async function checkFloat(id,max,value) {
      const number=page.locator('#'+id), slider=page.locator('#'+id+'Range');
      await number.fill(String(value));
      assert.equal(await slider.inputValue(),String(value),`${id} 数字同步滑杆`);
      await slider.focus();await page.keyboard.press('End');
      assert.equal(await number.inputValue(),String(max));
      await page.keyboard.press('Home');
      assert.equal(await number.inputValue(),'0');
      for (const invalid of ['',String(max+1),'2.5']) {
        await number.fill(invalid);
        assert.equal(await number.inputValue(),invalid,'浮动非法值保留');
        assert.equal(await number.getAttribute('aria-invalid'),'true');
        assert(await slider.isDisabled(),'非法浮动暂停滑杆');
        assert.match(await page.locator('#'+id+'Help').textContent(),/整数/);
        assert(await page.locator('#saveBtn').isDisabled());
      }
      await number.fill(String(value));
      assert(await slider.isEnabled());
      await slider.evaluate(node => node.scrollIntoView({block:'center'}));
      const box=await slider.boundingBox();
      await page.mouse.move(box.x+8,box.y+box.height/2);await page.mouse.down();
      await page.mouse.move(box.x+8+(box.width-16)*.5,box.y+box.height/2,{steps:8});await page.mouse.up();
      assert.equal(Number(await number.inputValue()),max/2,'浮动实际拖动同步');
      await number.fill(String(value));
    }
    assert.equal(await page.locator('#themeToggle svg').count(),2,'主题切换应使用月亮与太阳图标');
    assert.equal((await page.locator('#themeToggle').textContent()).trim(),'','图标按钮不显示主题文字');
    for (const theme of ['dark','light']) {
      if (await page.locator('html').getAttribute('data-theme') !== theme) await page.locator('#themeToggle').click();
      const next = theme === 'dark' ? '浅色':'深色';
      assert.equal(await page.locator('#themeToggle').getAttribute('aria-label'),`切换为${next}主题`);
      assert.equal(await page.locator('#themeToggle').getAttribute('title'),`切换为${next}主题`);
      assert.equal(await page.locator('#themeToggle svg:visible').count(),1);
    }
    for (const [target,float] of [['simPower','powerFloat'],['simCadence','cadenceFloat']]) {
      const pair = page.locator('.paired-input').filter({has:page.locator('#' + target)});
      assert.equal(await pair.locator('#' + float).count(),1,'目标与浮动同一输入栏');
      const left = await page.locator('#' + target).boundingBox(), right = await page.locator('#' + float).boundingBox();
      assert(Math.abs(left.y-right.y) < 1 && right.x > left.x,'浮动在目标输入右侧');
      assert(right.height >= 44 && right.width >= 44,'浮动数字可触摸编辑');
    }
    await page.locator('#powerFloat').fill('35');
    assert.equal(await page.locator('#powerFloatRange').inputValue(),'35');
    await page.locator('#powerFloatRange').focus();
    await page.keyboard.press('Home');
    assert.equal(await page.locator('#powerFloat').inputValue(),'0');
    await page.keyboard.press('End');
    assert.equal(await page.locator('#powerFloat').inputValue(),'100');
    await page.keyboard.press('ArrowLeft');
    assert.equal(await page.locator('#powerFloat').inputValue(),'99');
    await page.locator('#cadenceFloat').fill('6');
    assert.equal(await page.locator('#cadenceFloatRange').inputValue(),'6');
    await page.locator('#cadenceFloatRange').evaluate(node => node.scrollIntoView({block:'center'}));
    const drag = await page.locator('#cadenceFloatRange').boundingBox();
    await page.mouse.move(drag.x+8,drag.y+drag.height/2);
    await page.mouse.down();
    await page.mouse.move(drag.x+8+(drag.width-16)*.5,drag.y+drag.height/2,{steps:8});
    await page.mouse.up();
    assert.equal(await page.locator('#cadenceFloat').inputValue(),'10','实际拖动同步踏频浮动');
    const invalids = [
      ['simPower','-1','powerInputHelp','目标功率'],['simPower','2001','powerInputHelp','目标功率'],
      ['simPower','','powerInputHelp','目标功率'],['powerFloat','101','powerInputHelp','功率浮动'],
      ['powerFloat','2.5','powerInputHelp','功率浮动'],['simCadence','19','cadenceInputHelp','踏频'],
      ['cadenceFloat','21','cadenceInputHelp','踏频浮动']
    ];
    for (const [id,value,help,label] of invalids) {
      await page.locator('#simPower').fill('220');
      await page.locator('#simCadence').fill('88');
      await page.locator('#powerFloat').fill('35');
      await page.locator('#cadenceFloat').fill('6');
      await page.locator('#' + id).fill(value);
      assert.equal(await page.locator('#' + id).inputValue(),value,'非法输入不静默夹紧');
      assert.equal(await page.locator('#' + id).getAttribute('aria-invalid'),'true');
      assert.match(await page.locator('#' + help).textContent(),new RegExp(label));
      assert(await page.locator('#saveBtn').isDisabled());
    }
    await page.locator('#cadenceFloat').fill('6');
    await page.locator('#powerFloat').fill('35');
    await page.locator('#simPower').fill('260');
    await page.locator('#simCadence').fill('90');
    await page.locator('[data-sim-mode="pacer"]').click();
    assert(await page.locator('#powerFloat').isHidden());
    assert(await page.locator('#cadenceFloat').isHidden());
    assert(await page.locator('#simCadence').isVisible());
    await page.locator('[data-sim-mode="fixed"]').click();
    assert.equal(await page.locator('#powerFloat').inputValue(),'35');
    assert.equal(await page.locator('#cadenceFloat').inputValue(),'6');
    await page.locator('[data-heart-mode="fixed"]').click();
    await checkFloat('hrFloat',20,7);
    async function checkFloatRow(target,float) {
      const pair=page.locator('.paired-input').filter({has:page.locator('#'+target)});
      assert.equal(await pair.count(),1,`${float} 必须与目标共用同一个组合输入栏`);
      assert.equal(await pair.locator('#'+float).count(),1,'浮动在组合栏右侧');
      assert.equal((await pair.locator(':scope > div').last().locator('span').first().textContent()).trim(),'±');
      const left=await page.locator('#'+target).boundingBox(), right=await page.locator('#'+float).boundingBox();
      assert(Math.abs(left.y-right.y)<1 && right.x>left.x,`${float} 数字行与目标对齐`);
      assert(left.width>=44 && right.width>=44 && left.height===right.height,'组合数字均可触控且等高');
      const unit=await page.locator('#'+float).locator('..').locator('span').last().boundingBox();
      assert(unit.y>=right.y && unit.y+unit.height<=right.y+right.height,'单位保持在数字行');
      const box=await pair.boundingBox(), slider=await page.locator('#'+float+'Range').boundingBox();
      assert(Math.abs(box.x-slider.x)<1 && Math.abs(box.width-slider.width)<1,'浮动滑杆占满整个组合栏');
    }
    await checkFloatRow('fixedHeart','hrFloat');
    await page.locator('[data-heart-mode="linked"]').click();
    assert(await page.locator('#hrFloatRange').isHidden(),'功率联动收起固定心率浮动');
    await page.locator('[data-heart-mode="fixed"]').click();
    assert.equal(await page.locator('#hrFloat').inputValue(),'7');
    await page.locator('[data-sport="running"]').click();
    await checkFloat('runPaceFloat',60,15);
    await checkFloatRow('paceMinutes','runPaceFloat');
    await checkFloatRow('paceSeconds','runPaceFloat');
    await checkFloat('runCadenceFloat',20,5);
    await checkFloatRow('runCadence','runCadenceFloat');
    await page.locator('#paceMinutes').fill('5');
    await page.locator('#paceSeconds').fill('30');
    await page.locator('[data-run-mode="pacer"]').click();
    assert(await page.locator('#runPaceFloatRange').isHidden(),'配速档收起固定配速浮动');
    assert(await page.locator('#runCadenceFloatRange').isVisible(),'两种跑步模式均支持步频浮动');
    await page.locator('[data-run-mode="fixed"]').click();
    assert.equal(await page.locator('#runPaceFloat').inputValue(),'15');
    await page.locator('[data-sport="cycling"]').click();
    assert.deepEqual(writes,[],'主题与输入仅更改草稿');
    await page.route('**/api/config',route => route.fulfill({status:400,contentType:'application/json',
      body:JSON.stringify({ok:false,error:'输入专项故障注入'})}));
    await page.locator('#saveBtn').click();
    await page.waitForFunction(() => document.querySelector('#saveState').textContent.includes('设备请求失败'));
    assert.equal(await page.locator('#simPower').inputValue(),'260');
    assert.equal(await page.locator('#powerFloat').inputValue(),'35');
    await page.unroute('**/api/config');
    let release;
    const held = new Promise(resolve => {release=resolve;});
    await page.route('**/api/config',async route => {await held;await route.continue();});
    await page.locator('#saveBtn').click();
    for (const id of ['simPower','simCadence','fixedHeart','runCadence','paceMinutes','paceSeconds','powerFloat','cadenceFloat','powerFloatRange','cadenceFloatRange',
      'hrFloat','hrFloatRange','runPaceFloat','runPaceFloatRange','runCadenceFloat','runCadenceFloatRange']) {
      assert(await page.locator('#' + id).isDisabled(),'保存等待禁止编辑组合输入');
    }
    release();
    await page.locator('#toast').filter({hasText:'配置已保存并应用'}).waitFor();
    const applied = await (await fetch(url + '/api/state')).json();
    assert.equal(applied.config.power,260);
    assert.equal(applied.config.power_float,35);
    assert.equal(applied.config.cadence,90);
    assert.equal(applied.config.cadence_float,6);
    assert.equal(applied.config.hr_float,7);
    assert.equal(applied.config.run_pace_float_sec,15);
    assert.equal(applied.config.run_cadence_float,5);
    assert.equal(applied.config.run_speed_x100,1091,'组合配速 5:30 保留原保存换算');
    await page.unroute('**/api/config');
    for (const config of [
      {mode:1,power:321,has_user_hr:1,heartrate:157,broadcast_mask:5},
      {sport_profile:1,run_speed_x100:1091,has_user_hr:1,heartrate:157},
      {cycling_source:1,bridge_bound:1,bridge_address:'111111111111',bridge_addr_type:0,bridge_name:'已保存功率计',
        bridge_devices:[{addr_type:0,address:'111111111111',name:'已保存功率计'}]}
    ]) {
      const restored=await browser.newPage({viewport:{width:390,height:844}});
      let savedWrites=0;
      restored.on('request',request => {if (request.url().endsWith('/api/config')) savedWrites++;});
      await restored.route('**/api/state',async route => {
        const response=await route.fetch(), data=await response.json();
        Object.assign(data.config,config);
        await route.fulfill({response,json:data});
      });
      await restored.goto(url);
      await restored.waitForFunction(() => !document.querySelector('#saveState').textContent.includes('正在读取'));
      assert(await restored.locator('#saveBtn').isDisabled(),`已有配置恢复不产生草稿：${JSON.stringify(config)}`);
      if (config.mode===1) {
        assert(await restored.locator('#robotFields').isVisible(),'已有机器人模式正常恢复');
        assert.match(await restored.locator('#sourceOptionsSummary').textContent(),/配速机器人/);
        assert.equal(await restored.locator('#simPower').inputValue(),'321');
        assert(await restored.locator('[data-broadcast="power"]').isVisible(),'非默认广播自动展开');
      }
      if (config.sport_profile===1) {
        assert(await restored.locator('#runningEditor').isVisible(),'已有跑步模式正常恢复');
        assert.equal(await restored.locator('#paceSeconds').inputValue(),'30');
        assert(await restored.locator('#sourceOptions').isHidden(),'跑步收起骑行来源设置');
      }
      if (config.cycling_source===1) {
        assert(await restored.locator('#bridgeEditor').isVisible(),'已有功率计配置正常恢复');
        assert.match(await restored.locator('#sourceOptionsSummary').textContent(),/真实功率计/);
        assert.match(await restored.locator('#selectedDeviceName').textContent(),/已保存功率计/);
      }
      if (config.has_user_hr===1) {
        assert(await restored.locator('#fixedHeartFields').isVisible(),'已有固定心率正常恢复');
        assert.equal(await restored.locator('#fixedHeart').inputValue(),'157');
      }
      assert.equal(savedWrites,0,'打开已有配置不得重置或自动提交');
      await restored.close();
    }
    await page.locator('[data-source="bridge"]').click();
    await page.locator('#bridgeStartScanBtn').click();
    await page.locator('#scanResults button').first().waitFor();
    await page.locator('#scanResults button').last().focus();
    await page.keyboard.press('Tab');
    assert(await page.evaluate(() => !!document.activeElement.closest('#scanModal')),'扫描末尾Tab不得跳到背景');
    assert.equal(await page.locator('#scanModal button').first().evaluate(node => node===document.activeElement),true);
    await page.keyboard.press('Shift+Tab');
    assert.equal(await page.locator('#scanResults button').last().evaluate(node => node===document.activeElement),true);
    await page.keyboard.press('Escape');
    await page.locator('[data-source="simulation"]').click();
    await page.locator('[data-sport="running"]').click();
    await page.locator('#paceMinutes').fill('');
    assert(!/NaN|Infinity/.test(await page.locator('#paceSpeed').textContent()),'无效配速不得出现NaN');
    await page.locator('#paceMinutes').fill('5');
    await page.locator('[data-sport="cycling"]').click();
    assert(await page.locator('#powerFloat').isVisible(),'多次切换来源和运动后功率浮动仍显示');
    await page.waitForFunction(() => !document.querySelector('#toast').classList.contains('is-visible'));
    const arg = process.argv.indexOf('--screenshots');
    const directory = arg >= 0 ? path.resolve(process.argv[arg+1]) : null;
    if (directory) fs.mkdirSync(directory,{recursive:true});
    const componentStyle = '.action-bar{visibility:hidden}';
    for (const width of [320,390,768,1440]) {
      await page.setViewportSize({width,height:844});
      for (const theme of ['light','dark']) {
        if (await page.locator('html').getAttribute('data-theme') !== theme) await page.locator('#themeToggle').click();
        assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth),'组合栏无横向溢出');
        await checkAlignment();
        if (directory) {
          await page.evaluate(() => scrollTo(0,0));
          await page.screenshot({path:path.join(directory,`${theme}-${width}.png`),fullPage:true,style:componentStyle});
          await page.screenshot({path:path.join(directory,`${theme}-${width}-viewport.png`)});
          await page.locator('#simulationEditor .simulation-fields').evaluate(node => node.scrollIntoView({block:'center'}));
          await page.locator('#simulationEditor .simulation-fields').screenshot({path:path.join(directory,`${theme}-${width}-inputs.png`),style:componentStyle});
          await page.locator('#heartSection').screenshot({path:path.join(directory,`${theme}-${width}-heart.png`),style:componentStyle});
        }
        await checkFloatRow('fixedHeart','hrFloat');
        await page.locator('[data-sport="running"]').click();
        await checkAlignment();
        await checkFloatRow('runCadence','runCadenceFloat');
        await checkFloatRow('paceMinutes','runPaceFloat');
        await checkFloatRow('paceSeconds','runPaceFloat');
        if (directory) {
          await page.locator('#runningEditor').screenshot({path:path.join(directory,`${theme}-${width}-running.png`),style:componentStyle});
        }
        await page.locator('[data-run-mode="pacer"]').click();
        await checkAlignment();
        await page.locator('[data-run-mode="fixed"]').click();
        await page.locator('[data-sport="cycling"]').click();
        await page.locator('[data-source="bridge"]').click();
        await checkAlignment();
        await page.locator('[data-source="simulation"]').click();
      }
    }
    await page.locator('#closeWifiBtn').click();
    await page.locator('#confirmCloseBtn').click();
    await page.locator('#wifiClosed').waitFor();
    for (const id of ['powerFloat','cadenceFloat','hrFloat','runPaceFloat','runCadenceFloat']) {
      assert(await page.locator('#'+id).isDisabled(),'配网结束禁止编辑浮动数字');
      assert(await page.locator('#'+id+'Range').isDisabled(),'配网结束禁止编辑浮动滑杆');
    }
    for (const id of ['paceMinutes','paceSeconds']) assert(await page.locator('#'+id).isDisabled(),'配网结束禁止编辑配速目标');
    const touch = await browser.newPage({viewport:{width:390,height:844},isMobile:true,hasTouch:true,reducedMotion:'reduce'});
    const external = [];
    touch.on('pageerror',error => errors.push(error.message));
    touch.on('request',request => {if (!request.url().startsWith('file:')) external.push(request.url());});
    await touch.goto('file://' + path.join(root,'design-demos/bikepower-ui/configuration-preview/configuration.html'));
    await touch.waitForFunction(() => document.querySelector('#heroMetricValue').textContent === '200');
    for (const [id,max] of [['powerFloatRange',100],['cadenceFloatRange',20]]) {
      const slider = await touch.locator('#' + id).boundingBox();
      await touch.locator('#' + id).tap({position:{x:8+(slider.width-16)*.5,y:slider.height/2}});
      assert.equal(Number(await touch.locator('#' + id.replace('Range','')).inputValue()),max/2,'触屏同步浮动数字');
    }
    await touch.locator('#heartOptions summary').tap();
    await touch.locator('[data-heart-mode="fixed"]').tap();
    for (const [id,max] of [['hrFloat',20],['runPaceFloat',60],['runCadenceFloat',20]]) {
      if (id==='runPaceFloat') await touch.locator('[data-sport="running"]').tap();
      const slider=touch.locator('#'+id+'Range');
      await slider.evaluate(node => node.scrollIntoView({block:'center'}));
      const box=await slider.boundingBox();
      await slider.tap({position:{x:8+(box.width-16)*.5,y:box.height/2}});
      assert.equal(Number(await touch.locator('#'+id).inputValue()),max/2,'新增浮动触屏同步');
    }
    await touch.locator('[data-sport="cycling"]').tap();
    await touch.locator('#simPower').fill('270');
    await touch.locator('#saveBtn').tap();
    await touch.locator('#toast').filter({hasText:'配置已保存并应用'}).waitFor();
    assert.deepEqual(external,[],'离线组合输入不产生外部请求');
    assert.deepEqual(errors,[]);
    await touch.close();
    console.log('PASS：首次核心配置/渐进展开与已有配置恢复/五项统一同栏与全宽滑杆/配速分秒与保存换算/模式等宽居中/键盘拖动触屏/非法保留/隐藏保值/保存忙态与失败/会话结束禁用/字段映射/主题与焦点/双主题四宽/离线隔离');
  } finally {
    if (browser) await browser.close();
    server.kill();
  }
})().catch(error => {console.error(error);process.exitCode=1;});
