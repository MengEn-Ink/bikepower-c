const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {chromium}=require('./web-flash/node_modules/playwright');
const root=path.join(__dirname,'..','tools','web');
(async()=>{
  const browser=await chromium.launch({headless:true,...(process.env.BP_BROWSER_CHANNEL?{channel:process.env.BP_BROWSER_CHANNEL}:{})});
  try{
    for(const theme of ['light','dark'])for(const width of [900,1440]){
      const page=await browser.newPage({viewport:{width,height:1000}}),errors=[];
      page.on('pageerror',e=>errors.push(e.message));
      await page.route('**/*',route=>{
        const u=new URL(route.request().url());
        if(u.hostname!=='example.test')return route.abort();
        if(u.pathname.endsWith('.json'))return route.fulfill({json:{available:false}});
        const name=u.pathname==='/'?'ble-monitor.html':path.basename(u.pathname),file=path.join(root,name);
        return fs.existsSync(file)?route.fulfill({path:file,contentType:name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html'}):route.fulfill({status:404});
      });
      await page.addInitScript(theme=>{
        localStorage.setItem('bikepower.theme.v1',theme);localStorage.setItem('bikepower.autolink.enabled','0');
        Object.defineProperty(navigator,'bluetooth',{value:{requestDevice:()=>Promise.reject(new DOMException('User cancelled','NotFoundError'))}});
      },theme);
      await page.goto('https://example.test/#/monitor');
      await page.evaluate(async()=>{await openGate({force:true});showBleFailure(new Error('Connection Error: Connection attempt failed.'),{phase:'connect'});});
      await page.locator('#gate-recovery').waitFor({state:'visible'});
      assert.match(await page.locator('#gate-title').textContent(),/原因未确定/);
      const steps=await page.locator('#gate-recovery').textContent();
      for(const token of ['Mac','Windows','忽略此设备','删除设备','全量合并','未刚刷机'])assert.ok(steps.includes(token),token);
      await page.evaluate(()=>showBleFailure(new Error('Peer removed pairing information')));
      assert.equal(await page.locator('#gate-recovery').getAttribute('data-category'),'pairing');
      assert.match(await page.locator('#gate-title').textContent(),/旧配对/);
      await page.evaluate(()=>showBleFailure(new DOMException('fixture','NotAllowedError')));
      assert.equal(await page.locator('#gate-recovery').getAttribute('data-category'),'permission');
      await page.evaluate(()=>showBleFailure(new Error('<img src=x onerror=window.injected=1>')));
      assert.equal(await page.locator('#gate-recovery img').count(),0);
      assert.equal(await page.evaluate(()=>window.injected),undefined);
      await page.locator('#gate-recovery button').click();
      assert.equal(await page.locator('#connect-gate').isVisible(),false);
      assert.equal(await page.locator('#ble-troubleshooting').getAttribute('open'),'');
      assert.equal(await page.locator('#ble-troubleshooting-categories > details').count(),15);
      await page.evaluate(()=>{const node=document.getElementById('ble-troubleshooting');node.open=true;});
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),true,'no horizontal overflow');
      await page.evaluate(()=>setGate('choose'));
      assert.equal(await page.locator('#gate-recovery').isVisible(),false);
      await page.evaluate(()=>{
        closeGate('skip');
        currentMode='connect';
        singleDevice={device:{name:'fixture',id:'fixture'},server:{connected:true},info:{},services:[],history:[],subscriptions:{},connectedAt:new Date(Date.now()-30000).toISOString(),lastNotifyAt:0,staleOpen:false,notifyTimes:{}};
        checkAnomalies();
      });
      assert.equal(await page.locator('#ble-connect-recovery').getAttribute('data-category'),'notifications');
      await page.evaluate(()=>markNotify(singleDevice,'cps'));
      assert.equal(await page.locator('#ble-connect-recovery').isVisible(),false);
      await page.evaluate(()=>{singleDevice=null;});
      assert.deepEqual(errors,[]);
      console.log(`PASS ${theme} ${width}: generic/known-pairing/permission, OS steps, escaped technical, help navigation, all categories, overflow`);
      await page.close();
    }
  }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
