/* 独立两侧采集、双连降级与快照持久化：只替换外部BLE传输。 */
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {chromium}=require('./web-flash/node_modules/playwright');
const root=path.join(__dirname,'../tools/web');
const screenshots=path.join(__dirname,'reports/pages-capture');
fs.mkdirSync(screenshots,{recursive:true});
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:process.env.BIKEPOWER_CHROMIUM_PATH||undefined});
 try{
  for(const theme of ['light','dark']){
   const page=await browser.newPage({viewport:{width:1280,height:1000},colorScheme:theme,acceptDownloads:true});
   const errors=[];page.on('pageerror',e=>errors.push(e.message));
   await page.route('**/*',route=>{const u=new URL(route.request().url());if(u.hostname!=='capture.test')return route.abort();if(u.pathname.endsWith('.json'))return route.fulfill({json:{available:false}});const file=u.pathname==='/'?'ble-monitor.html':path.basename(u.pathname);const source=path.join(root,file);if(!fs.existsSync(source))return route.fulfill({status:404});return route.fulfill({path:source,contentType:file.endsWith('.js')?'text/javascript':file.endsWith('.css')?'text/css':'text/html'});});
   await page.addInitScript(theme=>{
    sessionStorage.setItem('bikepower.linkgate.skip.v1','1');
    localStorage.setItem('bikepower.theme.v1',theme);
    const full=s=>`0000${s}-0000-1000-8000-00805f9b34fb`;
    const config='7a5a0000-4c7a-4f2a-9b3e-6d1f4a8c0b21';
    window.queue=[];window.devices=[];window.writes=0;window.requests=[];
    function make(id,isBoard=false,fail=false){
     const device=new EventTarget();device.id=id;device.name=isBoard?'BikePower':'第三方功率计';
     const ch=(id,raw,notify=false)=>{const c=new EventTarget();c.uuid=full(id);c.properties={read:!notify,notify};c.readValue=async()=>new DataView(Uint8Array.from(raw).buffer);c.startNotifications=async()=>{setTimeout(()=>{c.value=new DataView(Uint8Array.from([0,0,250,0]).buffer);c.dispatchEvent(new Event('characteristicvaluechanged'));},30);return c;};c.writeValue=async()=>{window.writes++;throw Error('禁止写入')};return c;};
     const chars=[ch('2a63',[],true),ch('2a5d',[5])];if(!isBoard)chars.push(ch('2a65',[8,0,16,0]));
     const services=[{uuid:full('1818'),getCharacteristics:async()=>chars}];if(isBoard)services.push({uuid:config,getCharacteristics:async()=>[]});
     device.gatt={connected:false,async connect(){if(fail)throw Error('双连失败');this.connected=true;return this;},getPrimaryServices:async()=>services,disconnect(){this.connected=false;device.dispatchEvent(new Event('gattserverdisconnected'));}};
     devices.push(device);return device;
    }
    window.makeCaptureDevice=make;
    Object.defineProperty(navigator,'bluetooth',{configurable:true,value:{getDevices:async()=>[],requestDevice:async opts=>{requests.push(opts);const next=queue.shift();if(next==='cancel')throw new DOMException('取消','NotFoundError');return next;}}});
    const real=setTimeout;window.setTimeout=(fn,ms,...args)=>real(fn,ms===1000?10:ms,...args);
   },theme);
   await page.goto('https://capture.test/#/compare');await page.locator('#compat-collect-reference').waitFor();
   assert.equal(await page.locator('#connect-gate').isVisible(),false,'比对页不要求预先连接主设备');
   assert.equal(await page.locator('.compare-legacy').getAttribute('open'),null);
   assert.equal(await page.locator('.compat-context').getAttribute('open'),null);
   await page.evaluate(()=>queue.push(makeCaptureDevice('reference')));
   await page.locator('#compat-collect-reference').click();
   await page.waitForFunction(()=>document.querySelector('#compat-reference-state').textContent.includes('保存'));
   assert.equal(await page.evaluate(()=>devices[0].gatt.connected),false,'逐台采集后释放');
   const saved=await page.evaluate(()=>BikePowerCompatibilityUI.report().snapshots.reference);
   assert.ok(saved.samples.length>0);assert.equal(saved.deviceKey,'reference');
   await page.evaluate(()=>queue.push(makeCaptureDevice('board',true)));
   await page.locator('#compat-collect-board').click();
   await page.waitForFunction(()=>document.querySelector('#compat-board-state').textContent.includes('保存'));
   assert.equal(await page.evaluate(()=>devices[1].gatt.connected),false);
   await page.locator('#compat-analyze').click();
   assert.match(await page.locator('#compat-results').innerText(),/2a65/);
   assert.equal(await page.evaluate(()=>writes),0);
   assert.ok(await page.evaluate(()=>requests.every(o=>o.acceptAllDevices===true&&Array.isArray(o.optionalServices))));
   await page.reload();await page.locator('#compat-analyze:not(:disabled)').waitFor();
   assert.deepEqual(await page.evaluate(()=>BikePowerCompatibilityUI.report().snapshots.reference),saved,'刷新恢复快照');
   const downloadPromise=page.waitForEvent('download');await page.locator('#compat-export-reference').click();const download=await downloadPromise;const partial=JSON.parse(fs.readFileSync(await download.path(),'utf8'));assert.ok(partial.snapshots.reference);assert.equal(partial.snapshots.board,null);
   await page.locator('#compat-clear-snapshots').click();assert.equal(await page.locator('#compat-analyze').isDisabled(),true);
   await page.locator('#compat-import').setInputFiles({name:'reference.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(partial))});await page.waitForFunction(()=>BikePowerCompatibilityUI.report().snapshots.reference!==null);assert.equal(await page.evaluate(()=>BikePowerCompatibilityUI.report().snapshots.board),null);
   await page.locator('#compat-connection-mode').selectOption('simultaneous');
   await page.evaluate(()=>queue.push(makeCaptureDevice('reference')));await page.locator('#compat-collect-reference').click();await page.waitForFunction(()=>document.querySelector('#compat-reference-state').textContent.includes('连接中'));
   await page.evaluate(()=>queue.push(makeCaptureDevice('board-fail',true,true)));await page.locator('#compat-collect-board').click();await page.waitForFunction(()=>document.querySelector('#compat-status').textContent.includes('双连失败'));assert.equal(await page.evaluate(()=>devices[0].gatt.connected),true,'第二台失败保留第一台连接');assert.ok(await page.evaluate(()=>BikePowerCompatibilityUI.report().snapshots.reference));
   await page.evaluate(()=>queue.push(makeCaptureDevice('board',true)));await page.locator('#compat-collect-board').click();await page.waitForFunction(()=>document.querySelector('#compat-board-state').textContent.includes('连接中'));assert.equal(await page.evaluate(()=>devices.filter(d=>d.gatt.connected).length),2,'可同时连接两台');
   const before=await page.evaluate(()=>BikePowerCompatibilityUI.report().snapshots.board);
   await page.evaluate(()=>queue.push(makeCaptureDevice('reference')));await page.locator('#compat-collect-board').click();await page.waitForFunction(()=>document.querySelector('#compat-status').textContent.includes('同一台'));assert.deepEqual(await page.evaluate(()=>BikePowerCompatibilityUI.report().snapshots.board),before,'同设备拒绝不覆盖快照');
   await page.locator('#compat-release-capture').click();assert.equal(await page.evaluate(()=>devices.filter(d=>d.gatt.connected).length),0);assert.deepEqual(await page.evaluate(()=>BikePowerCompatibilityUI.report().snapshots.board),before);
   await page.evaluate(()=>queue.push('cancel'));await page.locator('#compat-collect-board').click();await page.waitForFunction(()=>document.querySelector('#compat-collection-progress').textContent.includes('取消'));assert.deepEqual(await page.evaluate(()=>BikePowerCompatibilityUI.report().snapshots.board),before);
   await page.locator('#compat-analyze').click();await page.evaluate(()=>window.scrollTo(0,0));await page.screenshot({path:path.join(screenshots,`pages-capture-${theme}.png`),animations:'disabled'});
   assert.equal(await page.evaluate(()=>writes),0);
   const invalid=structuredClone(partial);invalid.snapshots.reference.name='<img src=x onerror="window.captureInjected=true">';
   await page.locator('#compat-import').setInputFiles({name:'unsafe.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(invalid))});
   await page.waitForFunction(()=>document.querySelector('#compat-reference-summary').textContent.includes('<img'));
   assert.equal(await page.locator('#compat-reference-summary img').count(),0);assert.equal(await page.evaluate(()=>window.captureInjected),undefined);
   await page.locator('#compat-import').setInputFiles({name:'bad.json',mimeType:'application/json',buffer:Buffer.from('{"schema":"bad"}')});
   await page.waitForFunction(()=>document.querySelector('#compat-status').textContent.includes('版本'));assert.ok(await page.evaluate(()=>BikePowerCompatibilityUI.report().snapshots.reference.name.includes('<img')));
   await page.locator('#compat-import').setInputFiles({name:'reference.json',mimeType:'application/json',buffer:Buffer.from(JSON.stringify(partial))});await page.waitForFunction(()=>BikePowerCompatibilityUI.report().snapshots.reference.name==='第三方功率计');
   await page.evaluate(()=>queue.push(makeCaptureDevice('pending-new',true)));await page.locator('#compat-collect-board').click();await page.locator('#compat-release-capture').click();assert.deepEqual(await page.evaluate(()=>BikePowerCompatibilityUI.report().snapshots.board),before,'取消在途采集不覆盖快照');
   assert.deepEqual(errors,[]);console.log('PASS capture '+theme+':顺序/双连/失败保留/身份/刷新/单侧导入导出/差异/只读/注入防护/在途取消');
   await page.setViewportSize({width:390,height:844});assert.equal(await page.locator('.mobile-guide .guide-faq').count(),0);await page.getByRole('link',{name:'使用场景',exact:true}).click();assert.ok(await page.locator('#mg-scenes-title').isVisible());assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));await page.close();
  }
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
