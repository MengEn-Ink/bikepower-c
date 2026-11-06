/* 固定生产页面回归：无真实蓝牙/串口写入。 */
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const {chromium}=require('./web-flash/node_modules/playwright');
const root=path.join(__dirname,'..');
(async()=>{const browser=await chromium.launch({headless:true});try{
for(const theme of ['light','dark']){
 const page=await browser.newPage({viewport:{width:1280,height:900}}),errors=[];
 page.on('pageerror',e=>errors.push(e.message));
 await page.route('**/*',route=>{const u=new URL(route.request().url());if(u.hostname!=='example.test')return route.abort();if(u.pathname.endsWith('.json'))return route.fulfill({json:{available:false}});const name=u.pathname==='/'?'ble-monitor.html':path.basename(u.pathname),file=path.join(root,'tools/web',name);return fs.existsSync(file)?route.fulfill({path:file,contentType:name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html'}):route.fulfill({status:404});});
 await page.addInitScript(theme=>{localStorage.setItem('bikepower.theme.v1',theme);sessionStorage.setItem('bikepower.linkgate.skip.v1','1');},theme);
 await page.goto('https://example.test/#/monitor');
 const result=await page.evaluate(async()=>{
  document.getElementById('connect-gate').classList.add('hidden');document.body.classList.remove('gate-lock');
  const dv=bytes=>new DataView(new Uint8Array(bytes).buffer);
  const crank=(flags,wheel,revs,time)=>{const b=new Uint8Array(flags===3?11:5),v=new DataView(b.buffer);b[0]=flags;let o=1;if(flags&1){v.setUint32(o,wheel,true);v.setUint16(o+4,1000,true);o+=6;}v.setUint16(o,revs,true);v.setUint16(o+2,time,true);return v;};
  const rec0={};parseCsc(rec0,crank(3,100,10,1000));const combined=parseCsc(rec0,crank(3,101,11,2000));
  const rec1={};parseCsc(rec1,crank(2,0,65535,65500));const rollover=parseCsc(rec1,crank(2,0,0,1000));
  const rec={device:{name:'固定 FTMS 回归设备',id:'fixture'},server:{connected:true},latest:{power:null,ftmsPower:null,cadence:null,hr:null,speed:null},history:[],notifyTimes:{},subscriptions:{ftms:true},services:[],sessionStartedAt:Date.now()};singleDevice=rec;
  const bike=(speed,cadence,power)=>{const b=new Uint8Array(8),v=new DataView(b.buffer);v.setUint16(0,0x44,true);v.setUint16(2,speed*100,true);v.setUint16(4,cadence*2,true);v.setInt16(6,power,true);return v;};
  handleNotify(rec,'ftms',{target:{value:bike(10,80,200)}});handleNotify(rec,'ftms',{target:{value:bike(15,90,250)}});
  const display={speed:document.getElementById('m-speed').textContent,cadence:document.getElementById('m-cadence').textContent,power:document.getElementById('m-power').textContent};
  const before={...rec.latest},notifyBefore=rec.notifyTimes.ftms;
  handleNotify(rec,'ftms',{target:{value:dv([0x40,1,250,0])}});
  const unchanged=JSON.stringify(before)===JSON.stringify(rec.latest)&&rec.notifyTimes.ftms===notifyBefore;
  let rejected=0;for(const [parse,bytes] of [[parseIndoorBike,[0,0]],[parseRsc,[3,0,1,85]],[parseCyclingPower,[32,0,100,0]]])try{parse(dv(bytes));}catch(e){rejected++;}
  rec.notifyTimes.csc=Date.now();rec.latest.cadence=61;
  handleNotify(rec,'ftms',{target:{value:bike(16,100,260)}});const freshCsc=rec.latest.cadence;
  rec.notifyTimes.csc=Date.now()-4000;handleNotify(rec,'ftms',{target:{value:bike(17,110,270)}});const staleCsc=rec.latest.cadence;
  const fixture={power:260,cadence:85,mode:1,heartrate:145,has_user_hr:0,power_float:50,cadence_float:8,hr_float:7,power_step:20,sport_profile:0,broadcast_mask:7,weight:70,ftp:250,thr_hr:175,ride_type:2,custom_wkg_low:20,custom_wkg_high:28,custom_pacer_name:'Ride',run_speed_x100:1000,run_pace_float_sec:12,run_cadence:170,run_cadence_float:5,run_mode:1,run_pacer_type:1,run_pace_low_sec:300,run_pace_high_sec:420,run_custom_name:'Run',led_silent:0,led_scenes:Array.from({length:5},()=>({mode:1,color:1})),device_name:'BikePower',bridge_wire:[1,0,1,1,1,2,3,4,5,6,2,0,0,232,3,2,80,77],bridge_backup_1_wire:[1,1,1,7,8,9,10,11,12,2,66,49],bridge_backup_2_wire:[1,0,0,0,0,0,0,0,0,0]};
  let metaReads=0,changed=false;const requested=[];
  const service={async getCharacteristic(uuid){requested.push(uuid);if(uuid===UUIDS.configMeta)return{async readValue(){metaReads++;const b=new Uint8Array(12),v=new DataView(b.buffer);b[0]=1;b[1]=14;b[2]=255;v.setUint32(4,changed&&metaReads%2===0?2:1,true);v.setUint16(8,80,true);return v;}};if(uuid===UUIDS.configControl)return{};const g=CONFIG_GROUPS.find(x=>x.uuid===uuid);return{async readValue(){return configDataView(encodeConfigGroup(g.key,fixture));}};}};
  const cfg={server:{connected:true,async getPrimaryService(){return service;}},device:{name:'eight groups'}};
  const state=await readDeviceConfig(cfg);const complete=state.supported&&state.current.bridge_backup_1_wire.join()===fixture.bridge_backup_1_wire.join();
  const previous=state.current;changed=true;await readDeviceConfig(cfg);const fence=cfg.configState.current===previous&&cfg.configState.statusKind==='warn';
  const unknown={server:cfg.server};await readDeviceConfig(unknown);const refused=!unknown.configState.supported;
  return{combined,rollover,display,unchanged,rejected,freshCsc,staleCsc,complete,fence,refused,metaReads,backups:requested.filter(u=>/000a|000b/.test(u)).length};
 });
 assert.equal(result.combined,61);assert.equal(result.rollover,59);assert.deepEqual(result.display,{speed:'15.00',cadence:'90',power:'250'});assert.equal(result.unchanged,true);assert.equal(result.rejected,3);assert.equal(result.freshCsc,61);assert.equal(result.staleCsc,110);assert.equal(result.complete,true);assert.equal(result.fence,true);assert.equal(result.refused,true);assert.equal(result.metaReads,6);assert.equal(result.backups,6);assert.deepEqual(errors,[]);
 console.log(`[pages-audit-regression] PASS ${theme} CSC/回绕/FTMS实时/截断原子/来源新鲜度/八组/混合快照拒绝`);
 await page.close();
}}finally{await browser.close();}})().catch(e=>{console.error(e);process.exitCode=1;});
