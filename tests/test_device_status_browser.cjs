/* 实际 Pages 交互；受控协议输入不构成真机验收。 */
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const {chromium}=require("./web-flash/node_modules/playwright");
const root=path.join(__dirname,"../tools/web");
assert.ok(fs.existsSync(path.join(root,"device-status.js")),"缺少设备状态界面");
(async()=>{
  const browser=await chromium.launch({headless:true});
  try{
    for(const theme of ["light","dark"])for(const width of [390,1280]){
      const page=await browser.newPage({viewport:{width,height:900}});
      const errors=[];page.on("pageerror",e=>errors.push(e.message));
      await page.route("**/*",route=>{
        const url=new URL(route.request().url());
        if(url.hostname!=="example.test")return route.abort();
        if(url.pathname.endsWith(".json"))return route.fulfill({json:{available:false}});
        const name=url.pathname==="/"?"ble-monitor.html":path.basename(url.pathname);
        const file=path.join(root,name);
        if(!fs.existsSync(file))return route.fulfill({status:404});
        return route.fulfill({path:file,contentType:name.endsWith(".js")?"text/javascript":name.endsWith(".css")?"text/css":"text/html"});
      });
      await page.addInitScript(theme=>{
        localStorage.setItem("bikepower.theme.v1",theme);
        sessionStorage.setItem("bikepower.linkgate.skip.v1","1");
      },theme);
      await page.goto("https://example.test/#/device-status");
      await page.locator("#mode-device-status").waitFor({state:"visible"});
      const gate=page.locator("#connect-gate");
      if(await gate.isVisible())await gate.getByRole("button",{name:"稍后连接",exact:true}).click();
      assert.match(await page.locator("#telemetry-state").textContent(),/未连接/);
      await page.evaluate(async()=>{
        const chars=Array.from({length:5},()=>new EventTarget());
        const meta=new Uint8Array([1,15,232,3,1,0,0,0,4,2,1,0,1,97,98,99,100,101,102,103]);
        chars.forEach((ch,i)=>{
          ch.startNotifications=async()=>ch;ch.stopNotifications=async()=>{};
          ch.readValue=async()=>i===0?new DataView(meta.buffer):new DataView(new ArrayBuffer(20));
        });
        const server={connected:true,async getPrimaryService(){return {async getCharacteristic(id){return chars[parseInt(id.slice(4,8),16)-0x1001];}};}};
        const rec={server,device:{name:"协议测试设备"}};
        await window.BikePowerStatus.attach(rec);
        window.__telemetryFeed=(seq,free=81920)=>{
          for(let g=0;g<4;g++){
            const b=new Uint8Array(20),v=new DataView(b.buffer);
            b[0]=1;b[1]=g===2?3:1;v.setUint16(2,seq,true);
            if(g===0){v.setUint32(4,free,true);v.setUint32(8,40960,true);v.setUint32(12,65536,true);v.setUint32(16,327680,true);}
            if(g===1){v.setUint32(4,seq,true);v.setUint16(8,2500,true);v.setUint16(10,160,true);v.setUint16(12,100,true);v.setUint32(14,2048,true);}
            if(g===2){v.setInt8(4,-65);b[5]=1;v.setUint16(6,23,true);v.setUint16(8,24,true);v.setUint16(12,400,true);}
            chars[g+1].value=v;chars[g+1].dispatchEvent(new Event("characteristicvaluechanged"));
          }
        };
        __telemetryFeed(1);
      });
      assert.equal(await page.locator("#telemetry-memory-value").textContent(),"80.0 KiB");
      assert.equal(await page.locator("#telemetry-memory-sub").textContent(),"25.0% 空闲");
      assert.equal(await page.locator("#telemetry-cpu-value").textContent(),"25.0%");
      assert.ok(await page.evaluate(()=>{
        const bar=document.getElementById("telemetry-bar"),theme=document.getElementById("theme-btn");
        const a=bar.getBoundingClientRect(),b=theme.getBoundingClientRect();
        return bar.nextElementSibling===theme&&a.width>0&&a.right<=b.left&&Math.abs(a.y+a.height/2-b.y-b.height/2)<2;
      }),"指标应在右上角主题按钮左侧同排显示");
      await page.locator('[data-telemetry-open="cpu"]').click();
      assert.equal(await page.locator("#telemetry-window").isVisible(),true);
      const windowBox=page.locator("#telemetry-window");
      if(width===1280){
        const title=await page.locator("#telemetry-window-title").boundingBox();
        const before=await windowBox.boundingBox();
        await page.mouse.move(title.x+30,title.y+8);
        await page.mouse.down();
        await page.mouse.move(title.x+70,title.y+28);
        await page.mouse.up();
        const after=await windowBox.boundingBox();
        assert.ok(after.x>before.x,"标题栏拖动应移动窗口");
      }
      await page.locator("#telemetry-window-max").click();
      assert.ok((await windowBox.boundingBox()).width>=width-25);
      await page.locator("#telemetry-window-max").click();
      await page.locator("#telemetry-window-collapse").click();
      assert.equal(await page.locator(".telemetry-window-body").isVisible(),false);
      await page.locator("#telemetry-window-collapse").click();
      assert.equal(await page.locator(".telemetry-window-body").isVisible(),true);
      await page.locator("#telemetry-window-pause").click();
      await page.evaluate(()=>__telemetryFeed(2,98304));
      assert.equal(await page.locator("#telemetry-memory-value").textContent(),"96.0 KiB");
      assert.equal(await page.locator("#telemetry-memory-sub").textContent(),"30.0% 空闲");
      assert.match(await page.locator("#telemetry-window-pause").textContent(),/继续/);
      await page.locator("#telemetry-window-pause").click();
      await page.locator("#telemetry-window-close").click();
      await page.locator("#telemetry-page-pause").click();
      await page.evaluate(()=>__telemetryFeed(3,114688));
      assert.equal(await page.locator("#telemetry-memory-value").textContent(),"96.0 KiB");
      await page.locator("#telemetry-page-pause").click();
      assert.equal(await page.locator("#telemetry-memory-value").textContent(),"112.0 KiB");
      assert.equal(await page.locator("#telemetry-memory-sub").textContent(),"35.0% 空闲");
      assert.ok(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1),"不得横向溢出");
      await page.screenshot({path:`/tmp/bikepower-device-status-${width}-${theme}.png`,animations:"disabled"});
      await page.evaluate(()=>window.BikePowerStatus.detach());
      assert.equal(await page.locator("#telemetry-memory-value").textContent(),"—");
      assert.deepEqual(errors,[]);
      console.log(`PASS: ${theme} ${width}px 路由、百分比、独立暂停、恢复、断开、布局`);
      await page.close();
    }
  }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
